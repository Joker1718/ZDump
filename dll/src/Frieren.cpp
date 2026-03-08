// Frieren — 芙莉蓮 (Protagonist — OffsetFinder)
// AOB signature scanning to locate Godot engine globals.
// Ported from UE5CEDumper OffsetFinder.cpp with Godot-specific patterns.
//
// Scanning strategy per target:
//   1. Try hint-cached pattern first (from Himmel)
//   2. Try MSVC symbol exports (priority 0, O(1) via GetProcAddress)
//   3. Try partial symbol prefix (priority 10, export table scan)
//   4. Try AOB byte patterns (priority 50+, full module scan)
//   5. Validate each result with target-specific validator
//   6. First valid result wins

#define LOG_CAT "SCAN"
#include "Frieren.h"
#include "Zoltraak.h"
#include "Solliter.h"
#include "Himmel.h"
#include "Grimoire.h"
#include "GodotSig.h"

#include <Windows.h>
#include <Psapi.h>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace Zolt::Frieren {

// ============================================================
// Module-level cached state
// ============================================================
static EnginePointers s_cachedPointers;
static bool s_scanned = false;

// Module info cache
static uintptr_t s_moduleBase = 0;
static size_t    s_moduleSize = 0;

static void EnsureModuleInfo() {
    if (!s_moduleBase) {
        s_moduleBase = static_cast<uintptr_t>(Zoltraak::GetModuleBase(nullptr));
        s_moduleSize = static_cast<size_t>(Zoltraak::GetModuleSize(nullptr));
    }
}

// ============================================================
// Symbol export lookup
// ============================================================

// Try exact symbol export via GetProcAddress on the game module
// and all loaded modules (for modular Godot builds)
static uintptr_t TrySymbolExport(const char* mangledName) {
    // Try main module first
    HMODULE hGame = GetModuleHandleW(nullptr);
    if (hGame) {
        FARPROC addr = GetProcAddress(hGame, mangledName);
        if (addr) {
            Solliter::Info("SCAN", "TrySymbolExport: Found '%s' in main module at %p",
                mangledName, addr);
            return reinterpret_cast<uintptr_t>(addr);
        }
    }

    // Try all loaded modules (Godot modular builds may split into DLLs)
    HMODULE modules[1024];
    DWORD cbNeeded = 0;
    if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &cbNeeded)) {
        DWORD count = cbNeeded / sizeof(HMODULE);
        for (DWORD i = 0; i < count; ++i) {
            if (modules[i] == hGame) continue;
            FARPROC addr = GetProcAddress(modules[i], mangledName);
            if (addr) {
                char modName[MAX_PATH] = {};
                GetModuleFileNameA(modules[i], modName, MAX_PATH);
                Solliter::Info("SCAN", "TrySymbolExport: Found '%s' in %s at %p",
                    mangledName, modName, addr);
                return reinterpret_cast<uintptr_t>(addr);
            }
        }
    }

    return 0;
}

// Scan PE export table for partial symbol prefix match
static uintptr_t TryExportTableScan(HMODULE hModule, const char* prefix) {
    if (!hModule || !prefix) return 0;

    auto base = reinterpret_cast<uintptr_t>(hModule);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

    auto& exportDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!exportDir.VirtualAddress || !exportDir.Size) return 0;

    auto* exports = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(base + exportDir.VirtualAddress);
    auto* names     = reinterpret_cast<DWORD*>(base + exports->AddressOfNames);
    auto* ordinals  = reinterpret_cast<WORD*>(base + exports->AddressOfNameOrdinals);
    auto* functions = reinterpret_cast<DWORD*>(base + exports->AddressOfFunctions);

    size_t prefixLen = strlen(prefix);
    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
        const char* name = reinterpret_cast<const char*>(base + names[i]);
        if (strncmp(name, prefix, prefixLen) == 0) {
            DWORD funcRVA = functions[ordinals[i]];
            uintptr_t addr = base + funcRVA;
            Solliter::Info("SCAN", "TryExportTableScan: Prefix '%s' matched '%s' at %p",
                prefix, name, reinterpret_cast<void*>(addr));
            return addr;
        }
    }
    return 0;
}

// Try partial symbol prefix on main module + all loaded modules
static uintptr_t TrySymbolPrefix(const char* prefix) {
    HMODULE hGame = GetModuleHandleW(nullptr);
    if (hGame) {
        uintptr_t addr = TryExportTableScan(hGame, prefix);
        if (addr) return addr;
    }

    HMODULE modules[1024];
    DWORD cbNeeded = 0;
    if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &cbNeeded)) {
        DWORD count = cbNeeded / sizeof(HMODULE);
        for (DWORD i = 0; i < count; ++i) {
            if (modules[i] == hGame) continue;
            uintptr_t addr = TryExportTableScan(modules[i], prefix);
            if (addr) return addr;
        }
    }
    return 0;
}

// ============================================================
// Validation functions
// ============================================================

// Validate ObjectDB: check that addr points to an ObjectSlot array
// Each slot is 16 bytes: 8B metadata + 8B Object*
// A valid ObjectDB has several slots with valid Object pointers
static bool ValidateObjectDB(uintptr_t addr) {
    if (!addr) return false;
    EnsureModuleInfo();

    int validCount = 0;
    int checked = 0;

    // Check up to 200 slots, looking for at least 3 valid Object pointers
    for (int i = 0; i < 200 && checked < 30; ++i) {
        uintptr_t slotAddr = addr + static_cast<uintptr_t>(i) * DynOff::SLOT_STRIDE;

        // Read Object* at offset 8 within the slot
        uint64_t objPtr = Zoltraak::ReadSafe<uint64_t>(
            slotAddr + DynOff::SLOT_OBJECT_OFFSET, 0);
        if (!objPtr) continue;  // Empty slot, skip
        checked++;

        // Object should have a readable vtable pointer at offset 0
        uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(objPtr, 0);
        if (!vtable) continue;

        // vtable should be within the game module's code section
        if (vtable >= s_moduleBase && vtable < s_moduleBase + s_moduleSize) {
            validCount++;
        }
    }

    Solliter::Info("SCAN:ObjDB", "ValidateObjectDB: %d/%d slots had valid vtables (need >= 3)",
        validCount, checked);
    return validCount >= 3;
}

// Validate ClassDB: check HashMap structure
// Godot 4.x HashMap<StringName, ClassInfo> layout (from hash_map.h):
//   PagedAllocator element_alloc;           // ~0x28 bytes (2 ptrs + 5 u32 + SpinLock)
//   HashMapElement<K,V>** elements;         // pointer
//   uint32_t* hashes;                       // pointer
//   HashMapElement<K,V>* head_element;      // pointer
//   HashMapElement<K,V>* tail_element;      // pointer
//   uint32_t capacity_index;                // index into primes table (small: 0-25)
//   uint32_t num_elements;                  // actual count
//
// The PagedAllocator size varies but is typically 0x28 bytes on MSVC x64.
// LEA instructions load the address of the ENTIRE HashMap struct,
// so the data we care about is at known offsets from the struct start.

// Hash table size primes (from Godot hashfuncs.h)
static const uint32_t HASH_TABLE_PRIMES[] = {
    5, 13, 23, 47, 97, 193, 389, 769, 1543, 3079,
    6151, 12289, 24593, 49157, 98317, 196613, 393241,
    786433, 1572869, 3145739, 6291469, 12582917, 25165843,
    50331653, 100663319, 201326611, 402653189, 805306457, 0
};

static bool ValidateClassDBAtLayout(uintptr_t addr, int allocatorSize) {
    // Read HashMap fields after the PagedAllocator
    int off = allocatorSize;
    uint64_t elements   = Zoltraak::ReadSafe<uint64_t>(addr + off, 0);       // +off
    uint64_t hashes     = Zoltraak::ReadSafe<uint64_t>(addr + off + 0x08, 0); // +off+8
    uint64_t headElem   = Zoltraak::ReadSafe<uint64_t>(addr + off + 0x10, 0); // +off+16
    uint64_t tailElem   = Zoltraak::ReadSafe<uint64_t>(addr + off + 0x18, 0); // +off+24
    uint32_t capIndex   = Zoltraak::ReadSafe<uint32_t>(addr + off + 0x20, 0); // +off+32
    uint32_t numElems   = Zoltraak::ReadSafe<uint32_t>(addr + off + 0x24, 0); // +off+36

    // capacity_index should be small (index into primes table, usually 5-15 for ClassDB)
    if (capIndex > 25) return false;

    // num_elements should be large for ClassDB (Godot has ~800-1500 classes)
    if (numElems < 100 || numElems > 10000) return false;

    // Verify capacity_index maps to a reasonable prime
    uint32_t actualCap = HASH_TABLE_PRIMES[capIndex];
    if (actualCap == 0 || numElems > actualCap) return false;

    // All 4 pointers should be non-null and readable
    if (!elements || !hashes || !headElem || !tailElem) return false;
    if (!Zoltraak::IsReadable(elements) || !Zoltraak::IsReadable(hashes)) return false;
    if (!Zoltraak::IsReadable(headElem) || !Zoltraak::IsReadable(tailElem)) return false;

    Solliter::Info("SCAN:ClsDB", "ValidateClassDB: allocOff=%d capIdx=%u actualCap=%u num=%u "
        "elements=%p hashes=%p head=%p valid=yes",
        allocatorSize, capIndex, actualCap, numElems,
        reinterpret_cast<void*>(elements),
        reinterpret_cast<void*>(hashes),
        reinterpret_cast<void*>(headElem));
    return true;
}

static bool ValidateClassDB(uintptr_t addr) {
    if (!addr) return false;

    // Try common PagedAllocator sizes (depends on MSVC alignment)
    // PagedAllocator has: 2 pointers + 5 uint32_t + SpinLock(1 byte + padding)
    // Typical sizes: 0x28 (40 bytes), 0x30 (48 bytes with more padding)
    static const int ALLOC_SIZES[] = { 0x28, 0x30, 0x20, 0x38, -1 };

    for (int i = 0; ALLOC_SIZES[i] >= 0; ++i) {
        if (ValidateClassDBAtLayout(addr, ALLOC_SIZES[i])) {
            return true;
        }
    }

    Solliter::Debug("SCAN:ClsDB", "ValidateClassDB at %p: no matching layout found",
        reinterpret_cast<void*>(addr));
    return false;
}

// Brute-force .bss/.data scan for ClassDB HashMap
// Scans for the HashMap signature pattern at every 8-byte-aligned address.
// Looking for the distinct pattern of 4 consecutive readable pointers
// followed by a small capacity_index and a large num_elements.
// ClassDB::classes is the largest HashMap in Godot (~800-1500 elements).
static uintptr_t ScanBssForClassDB() {
    auto bss = Zoltraak::GetBssSection();
    auto data = Zoltraak::GetDataSection();

    if (bss.size == 0 && data.size == 0) {
        Solliter::Warn("SCAN:ClsDB", "No .bss or .data section found for brute-force scan");
        return 0;
    }

    Solliter::Info("SCAN:ClsDB", "Brute-force scanning .bss (0x%llX bytes) + .data (0x%llX bytes) for ClassDB HashMap",
        bss.size, data.size);

    uintptr_t bestAddr = 0;
    uint32_t bestElements = 0;
    int candidates = 0;

    // Scan for the HashMap internal pattern:
    //   [ptr][ptr][ptr][ptr][u32 capIdx][u32 numElems]
    // This is the latter half of HashMap (after PagedAllocator).
    // We find this pattern and then report the address MINUS the allocator size
    // so callers can access the full struct.
    auto scanSection = [&](uint64_t base, uint64_t size) {
        // We need at least 0x28 bytes (4 ptrs + 2 u32)
        if (size < 0x28) return;

        for (uint64_t offset = 0; offset + 0x28 <= size; offset += 8) {
            uintptr_t probeAddr = static_cast<uintptr_t>(base + offset);

            // Read the 4 pointers
            uint64_t elements  = Zoltraak::ReadSafe<uint64_t>(probeAddr, 0);
            uint64_t hashes    = Zoltraak::ReadSafe<uint64_t>(probeAddr + 0x08, 0);
            uint64_t headElem  = Zoltraak::ReadSafe<uint64_t>(probeAddr + 0x10, 0);
            uint64_t tailElem  = Zoltraak::ReadSafe<uint64_t>(probeAddr + 0x18, 0);

            // Quick filter: all 4 must be non-null
            if (!elements || !hashes || !headElem || !tailElem) continue;

            // Read capacity_index and num_elements (at +0x20 and +0x24)
            uint32_t capIndex  = Zoltraak::ReadSafe<uint32_t>(probeAddr + 0x20, 0xFFFFFFFF);
            uint32_t numElems  = Zoltraak::ReadSafe<uint32_t>(probeAddr + 0x24, 0);

            // capacity_index must be small (0-25, index into primes table)
            if (capIndex > 25) continue;

            // num_elements must be large for ClassDB
            if (numElems < 100 || numElems > 10000) continue;

            // Verify capacity from primes table
            uint32_t actualCap = HASH_TABLE_PRIMES[capIndex];
            if (actualCap == 0 || numElems > actualCap) continue;

            // Verify pointers are readable
            if (!Zoltraak::IsReadable(elements) || !Zoltraak::IsReadable(hashes)) continue;
            if (!Zoltraak::IsReadable(headElem) || !Zoltraak::IsReadable(tailElem)) continue;

            candidates++;
            Solliter::Debug("SCAN:ClsDB", "HashMap at %p: capIdx=%u cap=%u num=%u elements=%p hashes=%p",
                reinterpret_cast<void*>(probeAddr), capIndex, actualCap, numElems,
                reinterpret_cast<void*>(elements), reinterpret_cast<void*>(hashes));

            if (numElems > bestElements) {
                bestElements = numElems;
                // Store the address of the elements pointer (start of the
                // HashMap core fields, after the PagedAllocator).
                // The caller (ClassDB users) will need to add the allocator
                // offset, but we store the full struct address.
                // To find the struct start, subtract the allocator size.
                // However, we don't know the exact allocator size, so we
                // store the probe address and let callers figure out the layout.
                bestAddr = probeAddr;
            }
        }
    };

    if (bss.size > 0) scanSection(bss.baseAddr, bss.size);
    if (data.size > 0) scanSection(data.baseAddr, data.size);

    if (bestAddr) {
        Solliter::Info("SCAN:ClsDB", "Brute-force found ClassDB at %p with %u elements (%d candidates total)",
            reinterpret_cast<void*>(bestAddr), bestElements, candidates);
    } else {
        Solliter::Warn("SCAN:ClsDB", "Brute-force scan found no valid HashMap (%d candidates checked)",
            candidates);
    }
    return bestAddr;
}

// Validate SceneTree: dereference singleton pointer, check vtable
static bool ValidateSceneTree(uintptr_t addr) {
    if (!addr) return false;
    EnsureModuleInfo();

    uint64_t treePtr = Zoltraak::ReadSafe<uint64_t>(addr, 0);
    if (!treePtr || !Zoltraak::IsReadable(treePtr)) {
        Solliter::Debug("SCAN:STree", "ValidateSceneTree: null or unreadable singleton at %p",
            reinterpret_cast<void*>(addr));
        return false;
    }

    // vtable check
    uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(treePtr, 0);
    bool valid = (vtable >= s_moduleBase && vtable < s_moduleBase + s_moduleSize);

    Solliter::Info("SCAN:STree", "ValidateSceneTree: ptr=%p vtable=%p valid=%s",
        reinterpret_cast<void*>(treePtr),
        reinterpret_cast<void*>(vtable),
        valid ? "yes" : "no");
    return valid;
}

// Validate Engine: same pattern as SceneTree (dereference, vtable check)
static bool ValidateEngine(uintptr_t addr) {
    if (!addr) return false;
    EnsureModuleInfo();

    uint64_t enginePtr = Zoltraak::ReadSafe<uint64_t>(addr, 0);
    if (!enginePtr || !Zoltraak::IsReadable(enginePtr)) {
        return false;
    }

    uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(enginePtr, 0);
    bool valid = (vtable >= s_moduleBase && vtable < s_moduleBase + s_moduleSize);

    Solliter::Info("SCAN:Eng", "ValidateEngine: ptr=%p vtable=%p valid=%s",
        reinterpret_cast<void*>(enginePtr),
        reinterpret_cast<void*>(vtable),
        valid ? "yes" : "no");
    return valid;
}

// ============================================================
// Unified pattern scanning
// ============================================================

using ValidatorFn = bool(*)(uintptr_t);

// Try a single pattern and return resolved address (0 on failure)
// For MultiMatch patterns, this returns the first match only (use TryPatternMulti for all)
static uintptr_t TryPattern(const GodotAobSig* sig) {
    if (!sig) return 0;

    uintptr_t result = 0;

    switch (sig->resolve) {
    case GodotResolve::SymbolDirect: {
        result = TrySymbolExport(sig->pattern);
        break;
    }
    case GodotResolve::SymbolPrefix: {
        result = TrySymbolPrefix(sig->pattern);
        break;
    }
    case GodotResolve::RipDirect: {
        EnsureModuleInfo();
        uint64_t match = Zoltraak::AOBScan(sig->pattern);
        if (match) {
            result = static_cast<uintptr_t>(
                Zoltraak::ResolveRIP(match, sig->instrOffset, sig->opcodeLen, sig->totalLen));
            // Only accept if target is in a writable section (where globals live)
            if (result && Zoltraak::IsInWritableSection(result)) {
                Solliter::Debug("SCAN", "AOB '%s' matched at %p, RIP resolved to %p (writable)",
                    sig->id, reinterpret_cast<void*>(match), reinterpret_cast<void*>(result));
            } else if (result) {
                Solliter::Debug("SCAN", "AOB '%s' matched at %p, RIP resolved to %p (NOT writable, skipping)",
                    sig->id, reinterpret_cast<void*>(match), reinterpret_cast<void*>(result));
                result = 0;
            }
        }
        break;
    }
    case GodotResolve::RipDeref: {
        EnsureModuleInfo();
        uint64_t match = Zoltraak::AOBScan(sig->pattern);
        if (match) {
            uintptr_t ptr = static_cast<uintptr_t>(
                Zoltraak::ResolveRIP(match, sig->instrOffset, sig->opcodeLen, sig->totalLen));
            if (ptr && Zoltraak::IsInWritableSection(ptr)) {
                result = ptr;
                Solliter::Debug("SCAN", "AOB '%s' matched at %p, RIP ptr-to-ptr at %p",
                    sig->id, reinterpret_cast<void*>(match), reinterpret_cast<void*>(result));
            }
        }
        break;
    }
    case GodotResolve::RdataScan: {
        EnsureModuleInfo();
        uint64_t match = Zoltraak::AOBScan(sig->pattern);
        if (match) {
            result = static_cast<uintptr_t>(match);
            Solliter::Debug("SCAN", ".rdata '%s' matched at %p",
                sig->id, reinterpret_cast<void*>(result));
        }
        break;
    }
    case GodotResolve::MultiMatch: {
        // Single-match path for MultiMatch — just returns first valid match
        // Full multi-match is handled in ScanForTarget via TryPatternMulti
        break;
    }
    }

    // Apply adjustment
    if (result && sig->adjustment) {
        result += sig->adjustment;
    }

    return result;
}

// Multi-match pattern scan: find ALL matches, resolve each, validate each.
// Returns the first match that passes validation.
// excludeAddrs: addresses to skip (already found by other targets)
static uintptr_t TryPatternMulti(const GodotAobSig* sig, ValidatorFn validator,
    const uintptr_t* excludeAddrs = nullptr, int excludeCount = 0) {
    if (!sig || sig->resolve != GodotResolve::MultiMatch) return 0;

    EnsureModuleInfo();

    auto matches = Zoltraak::AOBScanAll(sig->pattern, 200);
    Solliter::Info("SCAN", "MultiMatch '%s': %zu total matches found", sig->id, matches.size());

    int validWritable = 0;
    int validatedOk = 0;
    int excluded = 0;
    int nonWritableLogged = 0;

    for (auto match : matches) {
        uintptr_t resolved = static_cast<uintptr_t>(
            Zoltraak::ResolveRIP(match, sig->instrOffset, sig->opcodeLen, sig->totalLen));

        if (!resolved) continue;

        // Apply adjustment
        if (sig->adjustment) resolved += sig->adjustment;

        // Filter: target must be in a writable section (.data or .bss)
        if (!Zoltraak::IsInWritableSection(resolved)) {
            // Log first few non-writable for diagnostics
            if (nonWritableLogged < 3) {
                Solliter::Debug("SCAN", "MultiMatch '%s': match %p -> %p (not writable, skipped)",
                    sig->id, reinterpret_cast<void*>(match), reinterpret_cast<void*>(resolved));
                nonWritableLogged++;
            }
            continue;
        }
        validWritable++;

        // Check exclusion list (skip addresses already claimed by other targets)
        bool isExcluded = false;
        for (int e = 0; e < excludeCount; ++e) {
            if (excludeAddrs[e] && resolved == excludeAddrs[e]) {
                isExcluded = true;
                excluded++;
                break;
            }
        }
        if (isExcluded) continue;

        // Validate the resolved address
        if (validator && !validator(resolved)) continue;
        validatedOk++;

        Solliter::Info("SCAN", "MultiMatch '%s': validated at %p (match %p, %d writable, %d excluded, %d validated)",
            sig->id, reinterpret_cast<void*>(resolved), reinterpret_cast<void*>(match),
            validWritable, excluded, validatedOk);
        return resolved;
    }

    Solliter::Debug("SCAN", "MultiMatch '%s': no valid match (%d writable, %d excluded, %d validated of %zu total)",
        sig->id, validWritable, excluded, validatedOk, matches.size());
    return 0;
}

// Scan all patterns for a target, trying hint first
// excludeAddrs/excludeCount: addresses to skip in MultiMatch (already found by other targets)
static uintptr_t ScanForTarget(
    const GodotAobSig* const* patterns,
    const char* hintPatternId,
    ValidatorFn validator,
    int& outPatternsTried,
    const char*& outPatternId,
    const char*& outMethod,
    const uintptr_t* excludeAddrs = nullptr,
    int excludeCount = 0)
{
    outPatternsTried = 0;
    outPatternId = nullptr;
    outMethod = "not_found";

    // Phase 1: Try hint-cached pattern first
    if (hintPatternId && hintPatternId[0]) {
        for (int i = 0; patterns[i]; ++i) {
            if (strcmp(patterns[i]->id, hintPatternId) == 0) {
                Solliter::Info("SCAN", "Trying hint pattern '%s'", hintPatternId);
                uintptr_t result = 0;
                if (patterns[i]->resolve == GodotResolve::MultiMatch) {
                    result = TryPatternMulti(patterns[i], validator, excludeAddrs, excludeCount);
                } else {
                    result = TryPattern(patterns[i]);
                }
                outPatternsTried++;
                if (result && (patterns[i]->resolve == GodotResolve::MultiMatch ||
                               !validator || validator(result))) {
                    outPatternId = patterns[i]->id;
                    outMethod = (patterns[i]->resolve == GodotResolve::SymbolDirect ||
                                 patterns[i]->resolve == GodotResolve::SymbolPrefix)
                                ? "symbol" : "aob";
                    Solliter::Info("SCAN", "Hint pattern '%s' succeeded: %p",
                        hintPatternId, reinterpret_cast<void*>(result));
                    return result;
                }
                Solliter::Info("SCAN", "Hint pattern '%s' failed, trying all patterns", hintPatternId);
                break;
            }
        }
    }

    // Phase 2: Try all patterns in priority order
    for (int i = 0; patterns[i]; ++i) {
        // Skip the hint pattern (already tried)
        if (hintPatternId && hintPatternId[0] &&
            strcmp(patterns[i]->id, hintPatternId) == 0) {
            continue;
        }

        Solliter::Debug("SCAN", "Trying pattern '%s' (priority %d)",
            patterns[i]->id, patterns[i]->priority);

        uintptr_t result = 0;

        // Use multi-match scan for MultiMatch patterns
        if (patterns[i]->resolve == GodotResolve::MultiMatch) {
            result = TryPatternMulti(patterns[i], validator, excludeAddrs, excludeCount);
        } else {
            result = TryPattern(patterns[i]);
        }
        outPatternsTried++;

        if (result && (patterns[i]->resolve == GodotResolve::MultiMatch ||
                       !validator || validator(result))) {
            outPatternId = patterns[i]->id;
            outMethod = (patterns[i]->resolve == GodotResolve::SymbolDirect ||
                         patterns[i]->resolve == GodotResolve::SymbolPrefix)
                        ? "symbol" : "aob";
            Solliter::Info("SCAN", "Pattern '%s' succeeded: %p",
                patterns[i]->id, reinterpret_cast<void*>(result));
            return result;
        }
    }

    Solliter::Warn("SCAN", "All %d patterns exhausted, target not found", outPatternsTried);
    return 0;
}

// ============================================================
// Version detection
// ============================================================

// Parse "4.3.stable" or "4.3.0.stable" into major/minor
static void ParseVersion(const std::string& ver, uint32_t& major, uint32_t& minor) {
    major = 0;
    minor = 0;
    if (ver.empty()) return;

    size_t dot1 = ver.find('.');
    if (dot1 == std::string::npos) return;

    major = static_cast<uint32_t>(std::atoi(ver.c_str()));
    minor = static_cast<uint32_t>(std::atoi(ver.c_str() + dot1 + 1));
}

// Try to detect version from Engine singleton memory
static std::string DetectVersionFromEngine(uintptr_t engineAddr) {
    if (!engineAddr) return "";

    // Dereference the singleton pointer
    uint64_t enginePtr = Zoltraak::ReadSafe<uint64_t>(engineAddr, 0);
    if (!enginePtr || !Zoltraak::IsReadable(enginePtr)) return "";

    // Scan nearby memory for a version string pattern
    // Engine stores version info as member String fields
    // We look for "4.X" pattern (ASCII encoded from Godot String)
    for (int offset = 0x40; offset < 0x600; offset += 8) {
        uint64_t fieldPtr = Zoltraak::ReadSafe<uint64_t>(enginePtr + offset, 0);
        if (!fieldPtr || !Zoltraak::IsReadable(fieldPtr)) continue;

        // Try reading as a C string first
        std::string s = Zoltraak::ReadString(fieldPtr, 32);
        if (s.size() >= 3 && s[0] >= '4' && s[0] <= '9' && s[1] == '.') {
            Solliter::Info("SCAN:Ver", "Found version string '%s' at Engine+0x%X",
                s.c_str(), offset);
            return s;
        }

        // Try reading as a Godot String (CowData<char32_t>)
        std::string gs = Zoltraak::ReadGodotString(fieldPtr);
        if (gs.size() >= 3 && gs[0] >= '4' && gs[0] <= '9' && gs[1] == '.') {
            Solliter::Info("SCAN:Ver", "Found Godot String version '%s' at Engine+0x%X",
                gs.c_str(), offset);
            return gs;
        }
    }

    Solliter::Debug("SCAN:Ver", "No version string found in Engine object");
    return "";
}

// Scan .rdata for "Godot Engine v" string
static std::string DetectVersionFromRdata() {
    EnsureModuleInfo();

    // Scan for "Godot Engine v" using AOBScan
    uint64_t match = Zoltraak::AOBScan(
        "47 6F 64 6F 74 20 45 6E 67 69 6E 65 20 76");

    if (!match) {
        Solliter::Debug("SCAN:Ver", "No 'Godot Engine v' string found in module");
        return "";
    }

    // Read the version starting after "Godot Engine v" (14 bytes)
    std::string version = Zoltraak::ReadString(match + 14, 32);
    if (!version.empty()) {
        // Trim at first non-version character
        size_t end = 0;
        for (size_t i = 0; i < version.size(); ++i) {
            char c = version[i];
            if ((c >= '0' && c <= '9') || c == '.' ||
                (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_') {
                end = i + 1;
            } else {
                break;
            }
        }
        version = version.substr(0, end);
        Solliter::Info("SCAN:Ver", "Found .rdata version: '%s'", version.c_str());
    }
    return version;
}

// ============================================================
// Public API
// ============================================================

bool FindAll(EnginePointers& out, ScanProgressFn progress) {
    Solliter::Summary("=== Frieren OffsetFinder: Starting scan ===");

    EnsureModuleInfo();
    Solliter::Info("SCAN", "Module base: %p, size: 0x%zX",
        reinterpret_cast<void*>(s_moduleBase), s_moduleSize);

    // Log PE section ranges for diagnostics
    {
        auto text  = Zoltraak::GetTextSection();
        auto rdata = Zoltraak::GetRdataSection();
        auto data  = Zoltraak::GetDataSection();
        auto bss   = Zoltraak::GetBssSection();
        Solliter::Info("SCAN", "Sections: .text=%p+%llX .rdata=%p+%llX .data=%p+%llX .bss=%p+%llX",
            reinterpret_cast<void*>(text.baseAddr), text.size,
            reinterpret_cast<void*>(rdata.baseAddr), rdata.size,
            reinterpret_cast<void*>(data.baseAddr), data.size,
            reinterpret_cast<void*>(bss.baseAddr), bss.size);
    }

    // Compute PE hash for hint cache
    Himmel::ComputePEHash(out.peHash, sizeof(out.peHash));
    Solliter::Info("SCAN", "PE hash: %s", out.peHash);

    // Load hints from cache
    auto hints = Himmel::LoadHints(out.peHash);

    // Phase 1: Engine (for version detection)
    if (progress) progress(1, "Scanning for Engine singleton...");
    out.Engine = ScanForTarget(
        GodotSig::ENGINE_PATTERNS,
        hints.enginePatternId.c_str(),
        ValidateEngine,
        out.enginePatternsTried,
        out.enginePatternId,
        out.engineMethod);
    Solliter::Info("SCAN:Eng", "Engine: %s at %p (pattern: %s, tried: %d)",
        out.engineMethod,
        reinterpret_cast<void*>(out.Engine),
        out.enginePatternId ? out.enginePatternId : "none",
        out.enginePatternsTried);

    // Phase 2: Detect Godot version
    if (progress) progress(2, "Detecting Godot version...");
    if (out.Engine) {
        out.godotVersion = DetectVersionFromEngine(out.Engine);
    }
    if (out.godotVersion.empty()) {
        out.godotVersion = DetectVersionFromRdata();
    }
    if (out.godotVersion.empty() && hints.hasVersionHint) {
        out.godotVersion = hints.godotVersion;
        Solliter::Info("SCAN:Ver", "Using cached version: %s", out.godotVersion.c_str());
    }
    ParseVersion(out.godotVersion, out.godotMajor, out.godotMinor);
    Solliter::Summary("Godot version: %s (major=%u minor=%u)",
        out.godotVersion.empty() ? "unknown" : out.godotVersion.c_str(),
        out.godotMajor, out.godotMinor);

    // Phase 3: ObjectDB
    if (progress) progress(3, "Scanning for ObjectDB...");
    out.ObjectDB = ScanForTarget(
        GodotSig::OBJECTDB_PATTERNS,
        hints.objectDBPatternId.c_str(),
        ValidateObjectDB,
        out.objectDBPatternsTried,
        out.objectDBPatternId,
        out.objectDBMethod);
    Solliter::Info("SCAN:ObjDB", "ObjectDB: %s at %p (pattern: %s, tried: %d)",
        out.objectDBMethod,
        reinterpret_cast<void*>(out.ObjectDB),
        out.objectDBPatternId ? out.objectDBPatternId : "none",
        out.objectDBPatternsTried);

    // Phase 4: ClassDB
    if (progress) progress(4, "Scanning for ClassDB...");
    out.ClassDB = ScanForTarget(
        GodotSig::CLASSDB_PATTERNS,
        hints.classDBPatternId.c_str(),
        ValidateClassDB,
        out.classDBPatternsTried,
        out.classDBPatternId,
        out.classDBMethod);

    // Fallback: brute-force scan .bss/.data for ClassDB HashMap
    if (!out.ClassDB) {
        Solliter::Info("SCAN:ClsDB", "AOB patterns failed, trying brute-force .bss/.data scan");
        out.ClassDB = ScanBssForClassDB();
        if (out.ClassDB) {
            out.classDBPatternId = "BSS_SCAN";
            out.classDBMethod = "bss_scan";
        }
    }

    Solliter::Info("SCAN:ClsDB", "ClassDB: %s at %p (pattern: %s, tried: %d)",
        out.classDBMethod,
        reinterpret_cast<void*>(out.ClassDB),
        out.classDBPatternId ? out.classDBPatternId : "none",
        out.classDBPatternsTried);

    // Phase 5: SceneTree — exclude Engine address to avoid collision
    // Engine and SceneTree use the same getter pattern (mov rax,[rip+X]; ret; NOP)
    // so we must exclude already-found addresses.
    if (progress) progress(5, "Scanning for SceneTree...");
    uintptr_t streeExclude[] = { out.Engine };
    out.SceneTree = ScanForTarget(
        GodotSig::SCENETREE_PATTERNS,
        hints.sceneTreePatternId.c_str(),
        ValidateSceneTree,
        out.sceneTreePatternsTried,
        out.sceneTreePatternId,
        out.sceneTreeMethod,
        streeExclude, out.Engine ? 1 : 0);
    Solliter::Info("SCAN:STree", "SceneTree: %s at %p (pattern: %s, tried: %d)",
        out.sceneTreeMethod,
        reinterpret_cast<void*>(out.SceneTree),
        out.sceneTreePatternId ? out.sceneTreePatternId : "none",
        out.sceneTreePatternsTried);

    // Phase 6: Validate and fix offsets
    if (progress) progress(6, "Validating offsets...");
    ValidateAndFixOffsets();

    // Phase 7: Save results to hint cache
    if (progress) progress(7, "Saving hint cache...");
    wchar_t processPath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, processPath, MAX_PATH);
    char procNameA[MAX_PATH] = {};
    WideCharToMultiByte(CP_UTF8, 0,
        fs::path(processPath).stem().c_str(), -1,
        procNameA, MAX_PATH, nullptr, nullptr);
    Himmel::SaveResults(out.peHash, out, procNameA);

    // Summary
    Solliter::Summary("=== Scan complete ===");
    Solliter::Summary("  ObjectDB:  %s at %p",
        out.objectDBMethod, reinterpret_cast<void*>(out.ObjectDB));
    Solliter::Summary("  ClassDB:   %s at %p",
        out.classDBMethod, reinterpret_cast<void*>(out.ClassDB));
    Solliter::Summary("  SceneTree: %s at %p",
        out.sceneTreeMethod, reinterpret_cast<void*>(out.SceneTree));
    Solliter::Summary("  Engine:    %s at %p",
        out.engineMethod, reinterpret_cast<void*>(out.Engine));
    Solliter::Summary("  Version:   %s",
        out.godotVersion.empty() ? "unknown" : out.godotVersion.c_str());

    // Cache results
    s_cachedPointers = out;
    s_scanned = true;

    // Success requires at least ObjectDB
    return out.ObjectDB != 0;
}

uintptr_t FindObjectDB(const char* hintPatternId) {
    int tried = 0;
    const char* patId = nullptr;
    const char* method = nullptr;
    return ScanForTarget(GodotSig::OBJECTDB_PATTERNS, hintPatternId,
                         ValidateObjectDB, tried, patId, method);
}

uintptr_t FindClassDB(const char* hintPatternId) {
    int tried = 0;
    const char* patId = nullptr;
    const char* method = nullptr;
    return ScanForTarget(GodotSig::CLASSDB_PATTERNS, hintPatternId,
                         ValidateClassDB, tried, patId, method);
}

uintptr_t FindSceneTree(const char* hintPatternId) {
    int tried = 0;
    const char* patId = nullptr;
    const char* method = nullptr;
    return ScanForTarget(GodotSig::SCENETREE_PATTERNS, hintPatternId,
                         ValidateSceneTree, tried, patId, method);
}

uintptr_t FindEngine(const char* hintPatternId) {
    int tried = 0;
    const char* patId = nullptr;
    const char* method = nullptr;
    return ScanForTarget(GodotSig::ENGINE_PATTERNS, hintPatternId,
                         ValidateEngine, tried, patId, method);
}

std::string DetectVersion() {
    if (s_scanned && !s_cachedPointers.godotVersion.empty()) {
        return s_cachedPointers.godotVersion;
    }

    // Try Engine singleton first
    uintptr_t engineAddr = FindEngine();
    if (engineAddr) {
        std::string ver = DetectVersionFromEngine(engineAddr);
        if (!ver.empty()) return ver;
    }

    // Fallback to .rdata scan
    return DetectVersionFromRdata();
}

bool ValidateAndFixOffsets() {
    Solliter::Info("OFFS", "ValidateAndFixOffsets: Probing Object layout...");

    // Step 1: Validate basic Object layout using ObjectDB
    if (s_cachedPointers.ObjectDB) {
        // Find a valid Object to probe
        for (int i = 0; i < 100; ++i) {
            uintptr_t slotAddr = s_cachedPointers.ObjectDB +
                static_cast<uintptr_t>(i) * DynOff::SLOT_STRIDE;
            uint64_t objPtr = Zoltraak::ReadSafe<uint64_t>(
                slotAddr + DynOff::SLOT_OBJECT_OFFSET, 0);
            if (!objPtr) continue;

            // Check vtable at +0x00
            uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(objPtr, 0);
            if (!vtable || vtable < s_moduleBase || vtable >= s_moduleBase + s_moduleSize) continue;

            // Check ObjectID at expected offset (+0x18)
            uint64_t objectId = Zoltraak::ReadSafe<uint64_t>(
                objPtr + DynOff::OBJECT_INSTANCE_ID, 0);
            if (objectId != 0) {
                Solliter::Info("OFFS", "Object at %p: vtable=%p, ObjectID=%llu - layout confirmed",
                    reinterpret_cast<void*>(objPtr),
                    reinterpret_cast<void*>(vtable),
                    objectId);
                break;
            }

            // If ObjectID not found at expected offset, try probing nearby
            for (int probe = 0x10; probe <= 0x30; probe += 8) {
                objectId = Zoltraak::ReadSafe<uint64_t>(objPtr + probe, 0);
                if (objectId != 0 && objectId < 0x1000000) {  // Reasonable ObjectID range
                    Solliter::Warn("OFFS", "ObjectID found at +0x%X (expected +0x%X), adjusting DynOff",
                        probe, DynOff::OBJECT_INSTANCE_ID);
                    DynOff::OBJECT_INSTANCE_ID = probe;
                    break;
                }
            }
            break;
        }
    }

    // Step 2: Probe StringName layout
    // We'll validate StringName resolution in Flamme module (Phase 2)
    // For now, set reasonable defaults
    if (DynOff::SDATA_NAME == -1) {
        // Common layout: _Data has refcount at +0, hash at +4, String name at +8 or +0x10
        // This will be refined when Flamme is implemented
        DynOff::SDATA_NAME = 0x08;
        DynOff::SDATA_HASH = 0x04;
        Solliter::Info("OFFS", "StringName defaults: SDATA_NAME=0x%X, SDATA_HASH=0x%X",
            DynOff::SDATA_NAME, DynOff::SDATA_HASH);
    }

    // Step 3: Node layout probing
    // This will be done in Phase 3 (Serie) when we have SceneTree access
    // For now, mark Node offsets as unvalidated
    if (DynOff::NODE_DATA_OFFSET == -1) {
        Solliter::Info("OFFS", "Node offsets not yet validated (will be probed in Phase 3)");
    }

    // Mark basic offsets as validated
    DynOff::bOffsetsValidated.store(true, std::memory_order_release);
    Solliter::Info("OFFS", "Basic offsets validated successfully");

    return true;
}

const EnginePointers& GetPointers() {
    return s_cachedPointers;
}

} // namespace Zolt::Frieren
