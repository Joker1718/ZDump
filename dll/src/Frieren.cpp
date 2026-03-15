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
#include "Solitaer.h"
#include "Himmel.h"
#include "Flamme.h"
#include "Grimoire.h"
#include "GodotSig.h"

#include <Windows.h>
#include <Psapi.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

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
            Solitaer::Info("SCAN", "TrySymbolExport: Found '%s' in main module at %p",
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
                Solitaer::Info("SCAN", "TrySymbolExport: Found '%s' in %s at %p",
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
            Solitaer::Info("SCAN", "TryExportTableScan: Prefix '%s' matched '%s' at %p",
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

// Validate ObjectDB: addr is the address of the object_slots pointer variable (.bss/.data).
// Dereference to get the actual ObjectSlot array, then check for valid Object pointers.
// Each slot is 16 bytes: 8B metadata + 8B Object*
static bool ValidateObjectDB(uintptr_t addr) {
    if (!addr) return false;
    EnsureModuleInfo();

    // addr = address of object_slots variable (a pointer in .bss)
    // Dereference to get the actual slot array address
    uint64_t arrayAddr = Zoltraak::ReadSafe<uint64_t>(addr, 0);
    if (!arrayAddr || !Zoltraak::IsReadable(arrayAddr)) {
        Solitaer::Debug("SCAN:ObjDB", "ValidateObjectDB: null or unreadable array ptr at %p",
            reinterpret_cast<void*>(addr));
        return false;
    }

    // Array should be in heap (not in module range)
    if (arrayAddr >= s_moduleBase && arrayAddr < s_moduleBase + s_moduleSize) {
        Solitaer::Debug("SCAN:ObjDB", "ValidateObjectDB: array at %p is inside module",
            reinterpret_cast<void*>(arrayAddr));
        return false;
    }

    int validCount = 0;
    int checked = 0;

    // Check up to 200 slots, looking for at least 3 valid Object pointers
    for (int i = 0; i < 200 && checked < 30; ++i) {
        uintptr_t slotAddr = static_cast<uintptr_t>(arrayAddr) +
            static_cast<uintptr_t>(i) * DynOff::SLOT_STRIDE;

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

    Solitaer::Info("SCAN:ObjDB", "ValidateObjectDB: array=%p %d/%d slots had valid vtables (need >= 3)",
        reinterpret_cast<void*>(arrayAddr), validCount, checked);
    return validCount >= 3;
}

// Relaxed ObjectDB validator: accepts empty but allocated arrays.
// Used during deferred retry when ObjectDB may be allocated but no objects created yet.
static bool ValidateObjectDBRelaxed(uintptr_t addr) {
    if (!addr) return false;
    EnsureModuleInfo();

    uint64_t arrayAddr = Zoltraak::ReadSafe<uint64_t>(addr, 0);
    if (!arrayAddr || !Zoltraak::IsReadable(arrayAddr)) return false;

    // Array should be in heap (not in module)
    if (arrayAddr >= s_moduleBase && arrayAddr < s_moduleBase + s_moduleSize) return false;

    // Try strict validation first
    int validCount = 0;
    int checked = 0;
    for (int i = 0; i < 200 && checked < 30; ++i) {
        uintptr_t slotAddr = static_cast<uintptr_t>(arrayAddr) +
            static_cast<uintptr_t>(i) * DynOff::SLOT_STRIDE;
        uint64_t objPtr = Zoltraak::ReadSafe<uint64_t>(
            slotAddr + DynOff::SLOT_OBJECT_OFFSET, 0);
        if (!objPtr) continue;
        checked++;
        uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(objPtr, 0);
        if (vtable >= s_moduleBase && vtable < s_moduleBase + s_moduleSize) {
            validCount++;
        }
    }

    if (validCount >= 3) {
        Solitaer::Info("SCAN:ObjDB", "ValidateObjectDBRelaxed: strict pass array=%p %d valid",
            reinterpret_cast<void*>(arrayAddr), validCount);
        return true;
    }

    // Relaxed: accept if array is readable and looks like a valid allocation
    // Verify that multiple slot-stride-aligned reads are possible (array is large enough)
    bool allReadable = true;
    for (int i = 0; i < 8; ++i) {
        if (!Zoltraak::IsReadable(arrayAddr + i * DynOff::SLOT_STRIDE)) {
            allReadable = false;
            break;
        }
    }

    if (allReadable) {
        Solitaer::Warn("SCAN:ObjDB", "ValidateObjectDBRelaxed: accepting empty array at %p "
            "(heap ptr readable, %d slots checked, %d valid)",
            reinterpret_cast<void*>(arrayAddr), checked, validCount);
        return true;
    }

    return false;
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

    Solitaer::Info("SCAN:ClsDB", "ValidateClassDB: allocOff=%d capIdx=%u actualCap=%u num=%u "
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
            // Store the working allocator offset for Diagoldze to use
            DynOff::CLASSDB_ALLOC_OFFSET = ALLOC_SIZES[i];
            return true;
        }
    }

    Solitaer::Debug("SCAN:ClsDB", "ValidateClassDB at %p: no matching layout found",
        reinterpret_cast<void*>(addr));
    return false;
}

// Brute-force .bss/.data scan for ClassDB HashMap
// Scans for the HashMap signature pattern at every 8-byte-aligned address.
// Looking for the distinct pattern of 4 consecutive readable pointers
// followed by a small capacity_index and a large num_elements.
// ClassDB::classes is the largest HashMap in Godot (~800-1500 elements).
// Check if a resolved name looks like a Godot CamelCase class name.
// Class names: start with uppercase, contain lowercase letters (e.g. "Node2D", "Sprite2D").
// Reject ALL_CAPS_UNDERSCORED names (e.g. "SIDE_LEFT", "VERTICAL_ALIGNMENT_TOP").
static bool LooksLikeClassName(const std::string& s) {
    if (s.size() < 2 || s.size() > 128) return false;
    if (s[0] < 'A' || s[0] > 'Z') return false;  // Must start uppercase

    bool hasLower = false;
    for (char c : s) {
        if (c >= 'a' && c <= 'z') hasLower = true;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return hasLower;  // Must have at least one lowercase (CamelCase, not ALL_CAPS)
}

// Sample StringName keys from a HashMap's linked list.
// Returns count of keys that look like CamelCase Godot class names.
static int SampleHashMapClassNames(uint64_t headElem, int maxSamples) {
    int classLike = 0;
    uint64_t current = headElem;
    int count = 0;

    while (current && count < maxSamples) {
        if (!Zoltraak::IsReadable(current)) break;

        // HashMapElement: next(8B) + prev(8B) + KeyValue { StringName key(8B), ... }
        uint64_t keyAddr = current + 0x10;
        uint64_t dataPtr = Zoltraak::ReadSafe<uint64_t>(keyAddr, 0);

        if (dataPtr && Zoltraak::IsReadable(dataPtr)) {
            std::string name = Flamme::ResolveAt(dataPtr);
            if (LooksLikeClassName(name)) {
                classLike++;
            }
        }

        uint64_t next = Zoltraak::ReadSafe<uint64_t>(current, 0);
        if (next == current) break;
        current = next;
        count++;
    }
    return classLike;
}

static uintptr_t ScanBssForClassDB() {
    auto bss = Zoltraak::GetBssSection();
    auto data = Zoltraak::GetDataSection();

    if (bss.size == 0 && data.size == 0) {
        Solitaer::Warn("SCAN:ClsDB", "No .bss or .data section found for brute-force scan");
        return 0;
    }

    Solitaer::Info("SCAN:ClsDB", "Brute-force scanning .bss (0x%llX bytes) + .data (0x%llX bytes) for ClassDB HashMap",
        bss.size, data.size);

    // Collect all valid HashMap candidates
    struct Candidate {
        uintptr_t addr;
        uint32_t  numElems;
        uint64_t  headElem;
    };
    std::vector<Candidate> candidateList;

    // Scan for the HashMap internal pattern:
    //   [ptr][ptr][ptr][ptr][u32 capIdx][u32 numElems]
    // This is the latter half of HashMap (after PagedAllocator).
    auto scanSection = [&](uint64_t base, uint64_t size) {
        if (size < 0x28) return;

        for (uint64_t offset = 0; offset + 0x28 <= size; offset += 8) {
            uintptr_t probeAddr = static_cast<uintptr_t>(base + offset);

            uint64_t elements  = Zoltraak::ReadSafe<uint64_t>(probeAddr, 0);
            uint64_t hashes    = Zoltraak::ReadSafe<uint64_t>(probeAddr + 0x08, 0);
            uint64_t headElem  = Zoltraak::ReadSafe<uint64_t>(probeAddr + 0x10, 0);
            uint64_t tailElem  = Zoltraak::ReadSafe<uint64_t>(probeAddr + 0x18, 0);

            if (!elements || !hashes || !headElem || !tailElem) continue;

            uint32_t capIndex  = Zoltraak::ReadSafe<uint32_t>(probeAddr + 0x20, 0xFFFFFFFF);
            uint32_t numElems  = Zoltraak::ReadSafe<uint32_t>(probeAddr + 0x24, 0);

            if (capIndex > 25) continue;
            if (numElems < 100 || numElems > 10000) continue;

            uint32_t actualCap = HASH_TABLE_PRIMES[capIndex];
            if (actualCap == 0 || numElems > actualCap) continue;

            if (!Zoltraak::IsReadable(elements) || !Zoltraak::IsReadable(hashes)) continue;
            if (!Zoltraak::IsReadable(headElem) || !Zoltraak::IsReadable(tailElem)) continue;

            Solitaer::Debug("SCAN:ClsDB", "HashMap at %p: capIdx=%u cap=%u num=%u elements=%p hashes=%p",
                reinterpret_cast<void*>(probeAddr), capIndex, actualCap, numElems,
                reinterpret_cast<void*>(elements), reinterpret_cast<void*>(hashes));

            candidateList.push_back({ probeAddr, numElems, headElem });
        }
    };

    if (bss.size > 0) scanSection(bss.baseAddr, bss.size);
    if (data.size > 0) scanSection(data.baseAddr, data.size);

    if (candidateList.empty()) {
        Solitaer::Warn("SCAN:ClsDB", "Brute-force scan found no valid HashMap candidates");
        return 0;
    }

    // Sort candidates by element count descending
    std::sort(candidateList.begin(), candidateList.end(),
        [](const Candidate& a, const Candidate& b) { return a.numElems > b.numElems; });

    // Try each candidate: sample StringName keys and check for CamelCase class names.
    // ClassDB keys are CamelCase (Object, Node2D, Sprite2D), not ALL_CAPS enums.
    for (auto& c : candidateList) {
        int classLike = SampleHashMapClassNames(c.headElem, 10);
        Solitaer::Debug("SCAN:ClsDB", "  Candidate %p (%u elems): %d/10 CamelCase class names",
            reinterpret_cast<void*>(c.addr), c.numElems, classLike);

        if (classLike >= 5) {
            Solitaer::Info("SCAN:ClsDB", "Brute-force found ClassDB at %p with %u elements (%d/%d candidates, %d/10 class names)",
                reinterpret_cast<void*>(c.addr), c.numElems,
                static_cast<int>(&c - &candidateList[0]) + 1,
                static_cast<int>(candidateList.size()), classLike);
            return c.addr;
        }
    }

    // Fallback: pick the largest HashMap (no class name validation passed)
    auto& best = candidateList[0];
    Solitaer::Warn("SCAN:ClsDB", "No candidate passed class name validation, using largest: %p (%u elements, %d candidates)",
        reinterpret_cast<void*>(best.addr), best.numElems,
        static_cast<int>(candidateList.size()));
    return best.addr;
}

// ============================================================
// 3.x ClassDB scanning — bucket-chain HashMap (16B inline)
// Layout: hash_table*(8B) + power(1B+3B pad) + elements(4B)
// ============================================================

// Validate a 3.x bucket-chain HashMap as ClassDB
// Note: ValidateClassDB3x is not currently called via AOB ScanForTarget (3.x uses BSS scan directly)
// but is kept for potential future AOB pattern support.
[[maybe_unused]] static bool ValidateClassDB3x(uintptr_t addr) {
    if (!addr) return false;

    uint64_t hashTable = Zoltraak::ReadSafe<uint64_t>(addr, 0);
    uint8_t  power     = Zoltraak::ReadSafe<uint8_t>(addr + 0x08, 0);
    uint32_t elements  = Zoltraak::ReadSafe<uint32_t>(addr + 0x0C, 0);

    // power should be reasonable (4-20 → 16 to 1M buckets)
    if (power < 4 || power > 20) return false;

    // 3.x ClassDB typically has 400-800 classes
    if (elements < 50 || elements > 5000) return false;

    // hash_table must be a valid heap pointer
    if (!hashTable || !Zoltraak::IsReadable(hashTable)) return false;

    // Verify some buckets: should have mix of null and readable element pointers
    int validBuckets = 0;
    int nullBuckets = 0;
    int bucketCount = 1 << power;
    int checkCount = (bucketCount < 16) ? bucketCount : 16;

    for (int i = 0; i < checkCount; ++i) {
        uint64_t elem = Zoltraak::ReadSafe<uint64_t>(hashTable + i * 8, 0);
        if (!elem) { nullBuckets++; continue; }
        if (Zoltraak::IsReadable(elem)) validBuckets++;
    }

    // Hash table should be sparse (some null, some valid)
    if (validBuckets < 2) return false;

    Solitaer::Info("SCAN:ClsDB", "ValidateClassDB3x: power=%u buckets=%d elements=%u "
        "valid=%d null=%d hashTable=%p",
        power, bucketCount, elements, validBuckets, nullBuckets,
        reinterpret_cast<void*>(hashTable));
    return true;
}

// Sample class names from a 3.x bucket-chain HashMap
static int SampleHashMapClassNames3x(uint64_t hashTable, uint8_t power, int maxSamples) {
    int classLike = 0;
    int sampled = 0;
    uint32_t bucketCount = 1u << power;
    // Cap bucket scan to prevent slow iteration on high-power HashMaps
    int scanBuckets = static_cast<int>(std::min(bucketCount, 2048u));

    for (int b = 0; b < scanBuckets && sampled < maxSamples; ++b) {
        uint64_t elem = Zoltraak::ReadSafe<uint64_t>(hashTable + b * 8, 0);
        int chainDepth = 0;
        while (elem && sampled < maxSamples && chainDepth < 256) {
            if (!Zoltraak::IsReadable(elem)) break;

            // 3.x Element: hash(4B)+pad(4B)+next(8B)+key(8B StringName)+value(ClassInfo...)
            uint64_t dataPtr = Zoltraak::ReadSafe<uint64_t>(elem + DynOff::ELEM_3X_KEY_OFF, 0);
            if (dataPtr && Zoltraak::IsReadable(dataPtr)) {
                std::string name = Flamme::ResolveAt(dataPtr);
                if (LooksLikeClassName(name)) classLike++;
            }
            sampled++;
            chainDepth++;

            uint64_t next = Zoltraak::ReadSafe<uint64_t>(elem + DynOff::ELEM_3X_NEXT_OFF, 0);
            if (next == elem) break;
            elem = next;
        }
    }
    return classLike;
}

// Brute-force .bss/.data scan for 3.x ClassDB HashMap (16B inline struct)
static uintptr_t ScanBssForClassDB3x() {
    auto bss = Zoltraak::GetBssSection();
    auto data = Zoltraak::GetDataSection();

    Solitaer::Info("SCAN:ClsDB", "Brute-force scanning for 3.x ClassDB HashMap (16B inline)");

    struct Candidate { uintptr_t addr; uint32_t elements; uint64_t hashTable; uint8_t power; };
    std::vector<Candidate> candidateList;

    auto scanSection = [&](uint64_t base, uint64_t size) {
        if (size < 16) return;
        for (uint64_t offset = 0; offset + 16 <= size; offset += 8) {
            uintptr_t probeAddr = static_cast<uintptr_t>(base + offset);
            uint64_t hashTable = Zoltraak::ReadSafe<uint64_t>(probeAddr, 0);
            uint8_t  power     = Zoltraak::ReadSafe<uint8_t>(probeAddr + 0x08, 0);
            uint32_t elements  = Zoltraak::ReadSafe<uint32_t>(probeAddr + 0x0C, 0);

            if (power < 4 || power > 20) continue;
            if (elements < 50 || elements > 5000) continue;
            if (!hashTable || !Zoltraak::IsReadable(hashTable)) continue;

            // Load factor sanity: reject if power is much too high for element count
            uint32_t bucketCount = 1u << power;
            if (power >= 10 && elements * 4 < bucketCount) continue;

            // Quick bucket check: at least 2 non-null readable buckets
            int valid = 0;
            int check = bucketCount < 16 ? static_cast<int>(bucketCount) : 16;
            for (int i = 0; i < check; ++i) {
                uint64_t e = Zoltraak::ReadSafe<uint64_t>(hashTable + i * 8, 0);
                if (e && Zoltraak::IsReadable(e)) valid++;
            }
            if (valid < 2) continue;

            Solitaer::Debug("SCAN:ClsDB", "3x HashMap at %p: power=%u elements=%u hashTable=%p",
                reinterpret_cast<void*>(probeAddr), power, elements,
                reinterpret_cast<void*>(hashTable));
            candidateList.push_back({ probeAddr, elements, hashTable, power });
        }
    };

    if (bss.size > 0) scanSection(bss.baseAddr, bss.size);
    if (data.size > 0) scanSection(data.baseAddr, data.size);

    if (candidateList.empty()) {
        Solitaer::Warn("SCAN:ClsDB", "3x brute-force scan found no valid HashMap candidates");
        return 0;
    }

    // Sort by element count descending
    std::sort(candidateList.begin(), candidateList.end(),
        [](const Candidate& a, const Candidate& b) { return a.elements > b.elements; });

    // Validate with class name sampling
    for (auto& c : candidateList) {
        int classLike = SampleHashMapClassNames3x(c.hashTable, c.power, 10);
        Solitaer::Debug("SCAN:ClsDB", "  3x Candidate %p (%u elems): %d/10 CamelCase class names",
            reinterpret_cast<void*>(c.addr), c.elements, classLike);

        if (classLike >= 5) {
            Solitaer::Info("SCAN:ClsDB", "3x brute-force found ClassDB at %p with %u elements (%d/10 class names)",
                reinterpret_cast<void*>(c.addr), c.elements, classLike);
            return c.addr;
        }
    }

    // Fallback: use largest candidate
    auto& best = candidateList[0];
    Solitaer::Warn("SCAN:ClsDB", "3x: using largest candidate %p (%u elements)",
        reinterpret_cast<void*>(best.addr), best.elements);
    return best.addr;
}

// ============================================================
// 3.x ObjectDB validation and scanning
// ============================================================

// Validate a 3.x ObjectDB HashMap<uint64_t, Object*> at the given address.
// Layout: hash_table(8B) + power(1B+3B pad) + elements(4B) = 16 bytes
// Note: ValidateObjectDB3x is not currently called via AOB ScanForTarget (3.x uses BSS scan directly)
// but is kept for potential future AOB pattern support.
[[maybe_unused]] static bool ValidateObjectDB3x(uintptr_t addr) {
    if (!addr) return false;
    EnsureModuleInfo();

    uint64_t hashTable = Zoltraak::ReadSafe<uint64_t>(addr, 0);
    uint8_t  power     = Zoltraak::ReadSafe<uint8_t>(addr + 0x08, 0);
    uint32_t elements  = Zoltraak::ReadSafe<uint32_t>(addr + 0x0C, 0);

    // power 4-20 → 16 to 1M buckets
    if (power < 4 || power > 20) return false;

    // 3.x ObjectDB typically has hundreds to thousands of objects
    if (elements < 10 || elements > 500000) return false;

    // hash_table must be a valid heap pointer
    if (!hashTable || !Zoltraak::IsReadable(hashTable)) return false;

    // Sample some buckets → follow chains → validate Object* vtable pointers
    int validVtables = 0;
    int sampled = 0;
    int bucketCount = 1 << power;

    for (int b = 0; b < bucketCount && sampled < 50; ++b) {
        uint64_t elem = Zoltraak::ReadSafe<uint64_t>(hashTable + b * 8, 0);
        while (elem && sampled < 50) {
            if (!Zoltraak::IsReadable(elem)) break;

            // 3.x HashMap<ObjectID, Object*>::Element:
            //   hash(4B)+pad(4B)+next*(8B)+key(8B ObjectID)+value(8B Object*)
            uint64_t objPtr = Zoltraak::ReadSafe<uint64_t>(elem + DynOff::ELEM_3X_VALUE_OFF, 0);
            if (objPtr && Zoltraak::IsReadable(objPtr)) {
                uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(objPtr, 0);
                if (vtable >= s_moduleBase && vtable < s_moduleBase + s_moduleSize) {
                    validVtables++;
                }
            }
            sampled++;

            uint64_t next = Zoltraak::ReadSafe<uint64_t>(elem + DynOff::ELEM_3X_NEXT_OFF, 0);
            if (next == elem) break;
            elem = next;
        }
    }

    if (validVtables < 3) return false;

    Solitaer::Info("SCAN:ObjDB", "ValidateObjectDB3x: power=%u elements=%u "
        "sampled=%d validVtables=%d hashTable=%p",
        power, elements, sampled, validVtables,
        reinterpret_cast<void*>(hashTable));
    return true;
}

// Brute-force .bss/.data scan for 3.x ObjectDB HashMap<ObjectID, Object*> (16B inline)
static uintptr_t ScanBssForObjectDB3x() {
    auto bss  = Zoltraak::GetBssSection();
    auto data = Zoltraak::GetDataSection();
    EnsureModuleInfo();

    Solitaer::Info("SCAN:ObjDB", "Brute-force scanning for 3.x ObjectDB HashMap (16B inline)");

    struct Candidate { uintptr_t addr; uint32_t elements; int validVtables; };
    std::vector<Candidate> candidateList;

    auto scanSection = [&](uint64_t base, uint64_t size) {
        if (size < 16) return;
        for (uint64_t offset = 0; offset + 16 <= size; offset += 8) {
            uintptr_t probeAddr = static_cast<uintptr_t>(base + offset);
            uint64_t hashTable = Zoltraak::ReadSafe<uint64_t>(probeAddr, 0);
            uint8_t  power     = Zoltraak::ReadSafe<uint8_t>(probeAddr + 0x08, 0);
            uint32_t elements  = Zoltraak::ReadSafe<uint32_t>(probeAddr + 0x0C, 0);

            if (power < 4 || power > 20) continue;
            if (elements < 10 || elements > 500000) continue;
            if (!hashTable || !Zoltraak::IsReadable(hashTable)) continue;

            // Load factor sanity: Godot 3.x doubles capacity when elements >= capacity,
            // so a legitimate HashMap should have elements >= 25% of bucket count.
            // power=16 with 503 elements (0.77% load) is a false positive.
            uint32_t bucketCount = 1u << power;
            if (power >= 10 && elements * 4 < bucketCount) continue;

            // Sample buckets → validate Object* vtables
            // Cap bucket scan to prevent slow iteration on high-power false positives
            int validVtables = 0;
            int sampled = 0;
            int scanBuckets = static_cast<int>(std::min(bucketCount, 2048u));

            for (int b = 0; b < scanBuckets && sampled < 30; ++b) {
                uint64_t elem = Zoltraak::ReadSafe<uint64_t>(hashTable + b * 8, 0);
                int chainDepth = 0;
                while (elem && sampled < 30 && chainDepth < 256) {
                    if (!Zoltraak::IsReadable(elem)) break;

                    uint64_t objPtr = Zoltraak::ReadSafe<uint64_t>(
                        elem + DynOff::ELEM_3X_VALUE_OFF, 0);
                    if (objPtr && Zoltraak::IsReadable(objPtr)) {
                        uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(objPtr, 0);
                        if (vtable >= s_moduleBase && vtable < s_moduleBase + s_moduleSize) {
                            validVtables++;
                        }
                    }
                    sampled++;
                    chainDepth++;

                    uint64_t next = Zoltraak::ReadSafe<uint64_t>(
                        elem + DynOff::ELEM_3X_NEXT_OFF, 0);
                    if (next == elem) break;
                    elem = next;
                }
            }

            if (validVtables >= 3) {
                Solitaer::Debug("SCAN:ObjDB", "3x ObjectDB candidate at %p: power=%u elements=%u vtables=%d",
                    reinterpret_cast<void*>(probeAddr), power, elements, validVtables);
                candidateList.push_back({ probeAddr, elements, validVtables });
            }
        }
    };

    if (bss.size > 0) scanSection(bss.baseAddr, bss.size);
    if (data.size > 0) scanSection(data.baseAddr, data.size);

    if (candidateList.empty()) {
        Solitaer::Warn("SCAN:ObjDB", "3x brute-force scan found no valid ObjectDB candidates");
        return 0;
    }

    // Sort by element count descending (largest is most likely the real ObjectDB)
    std::sort(candidateList.begin(), candidateList.end(),
        [](const Candidate& a, const Candidate& b) { return a.elements > b.elements; });

    auto& best = candidateList[0];
    Solitaer::Info("SCAN:ObjDB", "3x brute-force found ObjectDB at %p with %u elements (%d vtables, %d candidates)",
        reinterpret_cast<void*>(best.addr), best.elements, best.validVtables,
        static_cast<int>(candidateList.size()));
    return best.addr;
}

// Brute-force .bss/.data scan for ObjectDB object_slots pointer.
// Scans for an 8-byte value that is a heap pointer to a valid ObjectSlot array.
// Returns the address of the pointer variable (in .bss/.data), consistent with AOB results.
static uintptr_t ScanBssForObjectDB() {
    auto bss = Zoltraak::GetBssSection();
    auto data = Zoltraak::GetDataSection();

    if (bss.size == 0 && data.size == 0) {
        Solitaer::Warn("SCAN:ObjDB", "No .bss or .data section found for ObjectDB brute-force scan");
        return 0;
    }

    EnsureModuleInfo();
    Solitaer::Info("SCAN:ObjDB", "Brute-force scanning .bss + .data for ObjectDB");

    uintptr_t bestAddr = 0;
    int bestValidCount = 0;
    int candidates = 0;

    auto scanSection = [&](uint64_t base, uint64_t size) {
        if (size < 8) return;

        for (uint64_t offset = 0; offset + 8 <= size; offset += 8) {
            uintptr_t varAddr = static_cast<uintptr_t>(base + offset);
            uint64_t arrayPtr = Zoltraak::ReadSafe<uint64_t>(varAddr, 0);

            // Must be non-null, not in module (heap), and readable
            if (!arrayPtr || arrayPtr < 0x10000) continue;
            if (arrayPtr >= s_moduleBase && arrayPtr < s_moduleBase + s_moduleSize) continue;
            if (!Zoltraak::IsReadable(arrayPtr)) continue;

            // Check if it looks like an ObjectSlot array (16-byte stride)
            int validCount = 0;
            int checkedNonNull = 0;
            for (int i = 0; i < 200 && checkedNonNull < 30; ++i) {
                uintptr_t slotAddr = static_cast<uintptr_t>(arrayPtr) +
                    static_cast<uintptr_t>(i) * DynOff::SLOT_STRIDE;
                uint64_t objPtr = Zoltraak::ReadSafe<uint64_t>(
                    slotAddr + DynOff::SLOT_OBJECT_OFFSET, 0);
                if (!objPtr) continue;
                checkedNonNull++;

                uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(objPtr, 0);
                if (vtable >= s_moduleBase && vtable < s_moduleBase + s_moduleSize) {
                    validCount++;
                }
            }

            if (validCount >= 3 && validCount > bestValidCount) {
                bestValidCount = validCount;
                bestAddr = varAddr;
                candidates++;
                Solitaer::Debug("SCAN:ObjDB", "ObjectDB candidate at %p: array=%p %d valid objects",
                    reinterpret_cast<void*>(varAddr),
                    reinterpret_cast<void*>(arrayPtr), validCount);
            }
        }
    };

    if (bss.size > 0) scanSection(bss.baseAddr, bss.size);
    if (data.size > 0) scanSection(data.baseAddr, data.size);

    if (bestAddr) {
        Solitaer::Info("SCAN:ObjDB", "Brute-force found ObjectDB at %p with %d valid objects (%d candidates)",
            reinterpret_cast<void*>(bestAddr), bestValidCount, candidates);
    } else {
        Solitaer::Warn("SCAN:ObjDB", "Brute-force ObjectDB scan found no valid array");
    }
    return bestAddr;
}

// Validate SceneTree: dereference singleton pointer, check vtable
static bool ValidateSceneTree(uintptr_t addr) {
    if (!addr) return false;
    EnsureModuleInfo();

    uint64_t treePtr = Zoltraak::ReadSafe<uint64_t>(addr, 0);
    if (!treePtr || !Zoltraak::IsReadable(treePtr)) {
        Solitaer::Debug("SCAN:STree", "ValidateSceneTree: null or unreadable singleton at %p",
            reinterpret_cast<void*>(addr));
        return false;
    }

    // vtable check
    uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(treePtr, 0);
    bool valid = (vtable >= s_moduleBase && vtable < s_moduleBase + s_moduleSize);

    Solitaer::Info("SCAN:STree", "ValidateSceneTree: ptr=%p vtable=%p valid=%s",
        reinterpret_cast<void*>(treePtr),
        reinterpret_cast<void*>(vtable),
        valid ? "yes" : "no");
    return valid;
}

// Relaxed SceneTree validation: accepts BSS addresses even when singleton pointer is NULL.
// The AOB pattern correctly finds the .bss variable for SceneTree::singleton,
// but Main::start() may not have populated it yet. Accept the address for
// deferred resolution via Serie::GetRoot() lazy re-deref.
static bool ValidateSceneTreeRelaxed(uintptr_t addr) {
    if (!addr) return false;
    EnsureModuleInfo();

    // Must be in a writable section (.bss/.data where singleton variables live)
    if (!Zoltraak::IsInWritableSection(addr)) return false;

    uint64_t treePtr = Zoltraak::ReadSafe<uint64_t>(addr, 0);

    // If non-null, apply strict validation
    if (treePtr) {
        if (!Zoltraak::IsReadable(treePtr)) return false;
        uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(treePtr, 0);
        bool valid = (vtable >= s_moduleBase && vtable < s_moduleBase + s_moduleSize);
        if (valid) {
            Solitaer::Info("SCAN:STree", "ValidateSceneTreeRelaxed: strict pass ptr=%p vtable=%p",
                reinterpret_cast<void*>(treePtr), reinterpret_cast<void*>(vtable));
        }
        return valid;
    }

    // Pointer is NULL — accept the BSS address for deferred resolution
    Solitaer::Info("SCAN:STree", "ValidateSceneTreeRelaxed: accepting NULL singleton at %p (deferred)",
        reinterpret_cast<void*>(addr));
    return true;
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

    Solitaer::Info("SCAN:Eng", "ValidateEngine: ptr=%p vtable=%p valid=%s",
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
                Solitaer::Debug("SCAN", "AOB '%s' matched at %p, RIP resolved to %p (writable)",
                    sig->id, reinterpret_cast<void*>(match), reinterpret_cast<void*>(result));
            } else if (result) {
                Solitaer::Debug("SCAN", "AOB '%s' matched at %p, RIP resolved to %p (NOT writable, skipping)",
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
                Solitaer::Debug("SCAN", "AOB '%s' matched at %p, RIP ptr-to-ptr at %p",
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
            Solitaer::Debug("SCAN", ".rdata '%s' matched at %p",
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
    Solitaer::Info("SCAN", "MultiMatch '%s': %zu total matches found", sig->id, matches.size());

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
                Solitaer::Debug("SCAN", "MultiMatch '%s': match %p -> %p (not writable, skipped)",
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

        Solitaer::Info("SCAN", "MultiMatch '%s': validated at %p (match %p, %d writable, %d excluded, %d validated)",
            sig->id, reinterpret_cast<void*>(resolved), reinterpret_cast<void*>(match),
            validWritable, excluded, validatedOk);
        return resolved;
    }

    Solitaer::Debug("SCAN", "MultiMatch '%s': no valid match (%d writable, %d excluded, %d validated of %zu total)",
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
                Solitaer::Info("SCAN", "Trying hint pattern '%s'", hintPatternId);
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
                    Solitaer::Info("SCAN", "Hint pattern '%s' succeeded: %p",
                        hintPatternId, reinterpret_cast<void*>(result));
                    return result;
                }
                Solitaer::Info("SCAN", "Hint pattern '%s' failed, trying all patterns", hintPatternId);
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

        Solitaer::Debug("SCAN", "Trying pattern '%s' (priority %d)",
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
            Solitaer::Info("SCAN", "Pattern '%s' succeeded: %p",
                patterns[i]->id, reinterpret_cast<void*>(result));
            return result;
        }
    }

    Solitaer::Warn("SCAN", "All %d patterns exhausted, target not found", outPatternsTried);
    return 0;
}

// ============================================================
// Version detection
// ============================================================

// Parse "4.3.stable" or "4.3.0.stable" into major/minor
static void ParseVersion(const std::string& ver, uint32_t& major, uint32_t& minor, uint32_t& patch) {
    major = 0;
    minor = 0;
    patch = 0;
    if (ver.empty()) return;

    size_t dot1 = ver.find('.');
    if (dot1 == std::string::npos) return;

    major = static_cast<uint32_t>(std::atoi(ver.c_str()));
    minor = static_cast<uint32_t>(std::atoi(ver.c_str() + dot1 + 1));

    // Extract patch version (e.g. "4.3.1.stable" → patch=1)
    size_t dot2 = ver.find('.', dot1 + 1);
    if (dot2 != std::string::npos) {
        const char* patchStr = ver.c_str() + dot2 + 1;
        // Only parse if next char is a digit (not "stable", "dev", etc.)
        if (*patchStr >= '0' && *patchStr <= '9') {
            patch = static_cast<uint32_t>(std::atoi(patchStr));
        }
    }
}

// Store parsed version in global GodotVer state for other modules
static void StoreGodotVersion(uint32_t major, uint32_t minor, uint32_t patch) {
    GodotVer::Major = major;
    GodotVer::Minor = minor;
    GodotVer::Patch = patch;
}

// Try to detect version from Engine singleton memory
static std::string DetectVersionFromEngine(uintptr_t engineAddr) {
    if (!engineAddr) return "";

    // Dereference the singleton pointer
    uint64_t enginePtr = Zoltraak::ReadSafe<uint64_t>(engineAddr, 0);
    if (!enginePtr || !Zoltraak::IsReadable(enginePtr)) return "";

    // Scan nearby memory for a version string pattern
    // Engine stores version info as member String fields
    // We look for "X.Y" pattern where X is 1-9 (supports Godot 3.x and 4.x)
    for (int offset = 0x40; offset < 0x600; offset += 8) {
        uint64_t fieldPtr = Zoltraak::ReadSafe<uint64_t>(enginePtr + offset, 0);
        if (!fieldPtr || !Zoltraak::IsReadable(fieldPtr)) continue;

        // Try reading as a C string first
        std::string s = Zoltraak::ReadString(fieldPtr, 32);
        if (s.size() >= 3 && s[0] >= '1' && s[0] <= '9' && s[1] == '.') {
            Solitaer::Info("SCAN:Ver", "Found version string '%s' at Engine+0x%X",
                s.c_str(), offset);
            return s;
        }

        // Try reading as a Godot String (CowData<char32_t>)
        std::string gs = Zoltraak::ReadGodotString(fieldPtr);
        if (gs.size() >= 3 && gs[0] >= '1' && gs[0] <= '9' && gs[1] == '.') {
            Solitaer::Info("SCAN:Ver", "Found Godot String version '%s' at Engine+0x%X",
                gs.c_str(), offset);
            return gs;
        }
    }

    Solitaer::Debug("SCAN:Ver", "No version string found in Engine object");
    return "";
}

// Trim a version string at the first non-version character
static std::string TrimVersionString(const std::string& raw) {
    size_t end = 0;
    for (size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if ((c >= '0' && c <= '9') || c == '.' ||
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_') {
            end = i + 1;
        } else {
            break;
        }
    }
    return raw.substr(0, end);
}

// Scan .rdata for version strings (supports Godot 3.x and 4.x)
static std::string DetectVersionFromRdata() {
    EnsureModuleInfo();

    // Try 1: "Godot Engine v" (works for both 3.x and 4.x when present)
    uint64_t match = Zoltraak::AOBScan(
        "47 6F 64 6F 74 20 45 6E 67 69 6E 65 20 76");

    if (match) {
        // Read the version starting after "Godot Engine v" (14 bytes)
        std::string version = TrimVersionString(Zoltraak::ReadString(match + 14, 32));
        if (!version.empty()) {
            Solitaer::Info("SCAN:Ver", "Found .rdata version (Godot Engine v): '%s'", version.c_str());
            return version;
        }
    }

    // Try 2: "4.?.?.stable" pattern (Godot 4.x stripped builds)
    match = Zoltraak::AOBScan("34 2E ?? 2E ?? 2E 73 74 61 62 6C 65");
    if (match) {
        std::string version = TrimVersionString(Zoltraak::ReadString(match, 32));
        if (!version.empty() && version[0] == '4') {
            Solitaer::Info("SCAN:Ver", "Found .rdata version (4.x.stable): '%s'", version.c_str());
            return version;
        }
    }

    // Try 3: "3.?.?.stable" pattern (Godot 3.x stable builds)
    match = Zoltraak::AOBScan("33 2E ?? 2E ?? 2E 73 74 61 62 6C 65");
    if (match) {
        std::string version = TrimVersionString(Zoltraak::ReadString(match, 32));
        if (!version.empty() && version[0] == '3') {
            Solitaer::Info("SCAN:Ver", "Found .rdata version (3.x.stable): '%s'", version.c_str());
            return version;
        }
    }

    // Try 4: "3.?.dev" pattern (Godot 3.x development builds)
    match = Zoltraak::AOBScan("33 2E ?? 2E 64 65 76");
    if (match) {
        std::string version = TrimVersionString(Zoltraak::ReadString(match, 32));
        if (!version.empty() && version[0] == '3') {
            Solitaer::Info("SCAN:Ver", "Found .rdata version (3.x.dev): '%s'", version.c_str());
            return version;
        }
    }

    Solitaer::Debug("SCAN:Ver", "No version string found in .rdata");
    return "";
}

// ============================================================
// Public API
// ============================================================

bool FindAll(EnginePointers& out, ScanProgressFn progress) {
    Solitaer::Summary("=== Frieren OffsetFinder: Starting scan ===");

    EnsureModuleInfo();
    Solitaer::Info("SCAN", "Module base: %p, size: 0x%zX",
        reinterpret_cast<void*>(s_moduleBase), s_moduleSize);

    // Log PE section ranges for diagnostics
    {
        auto text  = Zoltraak::GetTextSection();
        auto rdata = Zoltraak::GetRdataSection();
        auto data  = Zoltraak::GetDataSection();
        auto bss   = Zoltraak::GetBssSection();
        Solitaer::Info("SCAN", "Sections: .text=%p+%llX .rdata=%p+%llX .data=%p+%llX .bss=%p+%llX",
            reinterpret_cast<void*>(text.baseAddr), text.size,
            reinterpret_cast<void*>(rdata.baseAddr), rdata.size,
            reinterpret_cast<void*>(data.baseAddr), data.size,
            reinterpret_cast<void*>(bss.baseAddr), bss.size);
    }

    // Compute PE hash for hint cache
    Himmel::ComputePEHash(out.peHash, sizeof(out.peHash));
    Solitaer::Info("SCAN", "PE hash: %s", out.peHash);

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
    Solitaer::Info("SCAN:Eng", "Engine: %s at %p (pattern: %s, tried: %d)",
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
        Solitaer::Info("SCAN:Ver", "Using cached version: %s", out.godotVersion.c_str());
    }
    ParseVersion(out.godotVersion, out.godotMajor, out.godotMinor, out.godotPatch);
    StoreGodotVersion(out.godotMajor, out.godotMinor, out.godotPatch);
    Solitaer::Summary("Godot version: %s (major=%u minor=%u patch=%u)",
        out.godotVersion.empty() ? "unknown" : out.godotVersion.c_str(),
        out.godotMajor, out.godotMinor, out.godotPatch);

    // Apply version-specific StringName layout BEFORE Phase 3/4 scanning.
    // The BSS scan's SampleHashMapClassNames calls Flamme::ResolveAt,
    // which needs correct SDATA offsets to resolve class names.
    // Flamme::ProbeLayout runs later (Phase 7), so we must set this early.
    if (GodotVer::IsGodot45Plus()) {
        DynOff::SDATA_CNAME = 0x08; // cname removed in 4.5+; String name at +0x08
        DynOff::SDATA_NAME  = 0x08;
        Solitaer::Info("SCAN:Ver", "Applied Godot 4.5+ StringName offsets: cname=name=+0x08");
    }

    // Phase 3: ObjectDB
    if (progress) progress(3, "Scanning for ObjectDB...");
    if (GodotVer::IsGodot3()) {
        // 3.x ObjectDB is HashMap<ObjectID, Object*> (16B bucket-chain HashMap)
        Solitaer::Info("SCAN:ObjDB", "Godot 3.x detected, using 3.x ObjectDB scanner");
        out.ObjectDB = ScanBssForObjectDB3x();
        if (out.ObjectDB) {
            out.objectDBPatternId = "BSS_SCAN_3X";
            out.objectDBMethod = "bss_scan_3x";
        }
    } else {
        out.ObjectDB = ScanForTarget(
            GodotSig::OBJECTDB_PATTERNS,
            hints.objectDBPatternId.c_str(),
            ValidateObjectDB,
            out.objectDBPatternsTried,
            out.objectDBPatternId,
            out.objectDBMethod);
    }
    Solitaer::Info("SCAN:ObjDB", "ObjectDB: %s at %p (pattern: %s, tried: %d)",
        out.objectDBMethod,
        reinterpret_cast<void*>(out.ObjectDB),
        out.objectDBPatternId ? out.objectDBPatternId : "none",
        out.objectDBPatternsTried);

    // Phase 4: ClassDB
    if (progress) progress(4, "Scanning for ClassDB...");
    if (GodotVer::IsGodot3()) {
        // 3.x ClassDB is HashMap<StringName, ClassInfo> (16B bucket-chain HashMap)
        Solitaer::Info("SCAN:ClsDB", "Godot 3.x detected, using 3.x ClassDB scanner");
        out.ClassDB = ScanBssForClassDB3x();
        if (out.ClassDB) {
            out.classDBPatternId = "BSS_SCAN_3X";
            out.classDBMethod = "bss_scan_3x";
            DynOff::CLASSDB_ALLOC_OFFSET = 0;
        }
    } else {
        out.ClassDB = ScanForTarget(
            GodotSig::CLASSDB_PATTERNS,
            hints.classDBPatternId.c_str(),
            ValidateClassDB,
            out.classDBPatternsTried,
            out.classDBPatternId,
            out.classDBMethod);

        // Fallback: brute-force scan .bss/.data for ClassDB HashMap
        if (!out.ClassDB) {
            Solitaer::Info("SCAN:ClsDB", "AOB patterns failed, trying brute-force .bss/.data scan");
            out.ClassDB = ScanBssForClassDB();
            if (out.ClassDB) {
                out.classDBPatternId = "BSS_SCAN";
                out.classDBMethod = "bss_scan";
                // Brute-force returns the address of core HashMap fields directly
                // (without PagedAllocator prefix), so set alloc offset to 0
                DynOff::CLASSDB_ALLOC_OFFSET = 0;
            }
        }
    }

    Solitaer::Info("SCAN:ClsDB", "ClassDB: %s at %p (pattern: %s, tried: %d)",
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
    // If strict validation failed (SceneTree singleton NULL), try relaxed:
    // accept the BSS address even when Main::start() hasn't populated it yet.
    if (!out.SceneTree) {
        Solitaer::Info("SCAN:STree", "Strict scan failed, trying relaxed validation (accept NULL singleton)...");
        uintptr_t streeExcludeRelaxed[] = { out.Engine };
        out.SceneTree = ScanForTarget(
            GodotSig::SCENETREE_PATTERNS,
            hints.sceneTreePatternId.c_str(),
            ValidateSceneTreeRelaxed,
            out.sceneTreePatternsTried,
            out.sceneTreePatternId,
            out.sceneTreeMethod,
            streeExcludeRelaxed, out.Engine ? 1 : 0);
        if (out.SceneTree) {
            out.sceneTreeMethod = "aob_relaxed";
        }
    }

    Solitaer::Info("SCAN:STree", "SceneTree: %s at %p (pattern: %s, tried: %d)",
        out.sceneTreeMethod,
        reinterpret_cast<void*>(out.SceneTree),
        out.sceneTreePatternId ? out.sceneTreePatternId : "none",
        out.sceneTreePatternsTried);

    // ================================================================
    // Phase 6: Deferred retry for missing singleton targets
    // If Engine, SceneTree, or ObjectDB are missing, the game may not
    // have initialized them yet. Wait and try again.
    // ================================================================
    {
        int missingCount = (!out.Engine ? 1 : 0) +
                           (!out.SceneTree ? 1 : 0) +
                           (!out.ObjectDB ? 1 : 0);

        if (missingCount > 0) {
            Solitaer::Summary("=== Deferred retry: %d targets missing, waiting 3s ===", missingCount);
            if (progress) progress(6, "Waiting for game initialization...");
            Sleep(3000);

            // Retry Engine
            if (!out.Engine) {
                Solitaer::Info("SCAN", "Retry: scanning for Engine...");
                out.Engine = ScanForTarget(
                    GodotSig::ENGINE_PATTERNS,
                    hints.enginePatternId.c_str(),
                    ValidateEngine,
                    out.enginePatternsTried,
                    out.enginePatternId,
                    out.engineMethod);
                if (out.Engine) {
                    Solitaer::Summary("  Retry Engine: found at %p",
                        reinterpret_cast<void*>(out.Engine));
                    // Also try version detection now
                    if (out.godotVersion.empty()) {
                        out.godotVersion = DetectVersionFromEngine(out.Engine);
                        if (!out.godotVersion.empty()) {
                            ParseVersion(out.godotVersion, out.godotMajor, out.godotMinor, out.godotPatch);
                            StoreGodotVersion(out.godotMajor, out.godotMinor, out.godotPatch);
                        }
                    }
                }
            }

            // Retry SceneTree (with Engine exclusion) — strict then relaxed
            if (!out.SceneTree) {
                Solitaer::Info("SCAN", "Retry: scanning for SceneTree (strict)...");
                uintptr_t streeExclude2[] = { out.Engine };
                out.SceneTree = ScanForTarget(
                    GodotSig::SCENETREE_PATTERNS,
                    hints.sceneTreePatternId.c_str(),
                    ValidateSceneTree,
                    out.sceneTreePatternsTried,
                    out.sceneTreePatternId,
                    out.sceneTreeMethod,
                    streeExclude2, out.Engine ? 1 : 0);
                if (out.SceneTree) {
                    Solitaer::Summary("  Retry SceneTree: found at %p",
                        reinterpret_cast<void*>(out.SceneTree));
                }
            }
            // Relaxed retry: accept NULL singleton for deferred resolution
            if (!out.SceneTree) {
                Solitaer::Info("SCAN", "Retry: scanning for SceneTree (relaxed)...");
                uintptr_t streeExclude3[] = { out.Engine };
                out.SceneTree = ScanForTarget(
                    GodotSig::SCENETREE_PATTERNS,
                    hints.sceneTreePatternId.c_str(),
                    ValidateSceneTreeRelaxed,
                    out.sceneTreePatternsTried,
                    out.sceneTreePatternId,
                    out.sceneTreeMethod,
                    streeExclude3, out.Engine ? 1 : 0);
                if (out.SceneTree) {
                    out.sceneTreeMethod = "aob_relaxed";
                    Solitaer::Summary("  Retry SceneTree (relaxed): address at %p (deferred)",
                        reinterpret_cast<void*>(out.SceneTree));
                }
            }

            // Retry ObjectDB (strict first, then relaxed)
            if (!out.ObjectDB) {
                if (GodotVer::IsGodot3()) {
                    Solitaer::Info("SCAN", "Retry: 3.x brute-force .bss scan for ObjectDB...");
                    out.ObjectDB = ScanBssForObjectDB3x();
                    if (out.ObjectDB) {
                        out.objectDBPatternId = "BSS_SCAN_3X";
                        out.objectDBMethod = "bss_scan_3x";
                    }
                } else {
                Solitaer::Info("SCAN", "Retry: scanning for ObjectDB (strict)...");
                out.ObjectDB = ScanForTarget(
                    GodotSig::OBJECTDB_PATTERNS,
                    hints.objectDBPatternId.c_str(),
                    ValidateObjectDB,
                    out.objectDBPatternsTried,
                    out.objectDBPatternId,
                    out.objectDBMethod);

                // Try brute-force .bss scan
                if (!out.ObjectDB) {
                    Solitaer::Info("SCAN", "Retry: brute-force .bss scan for ObjectDB...");
                    out.ObjectDB = ScanBssForObjectDB();
                    if (out.ObjectDB) {
                        out.objectDBPatternId = "BSS_SCAN";
                        out.objectDBMethod = "bss_scan";
                    }
                }

                // Last resort: relaxed validation (accept empty arrays)
                if (!out.ObjectDB) {
                    Solitaer::Info("SCAN", "Retry: scanning for ObjectDB (relaxed)...");
                    out.ObjectDB = ScanForTarget(
                        GodotSig::OBJECTDB_PATTERNS,
                        hints.objectDBPatternId.c_str(),
                        ValidateObjectDBRelaxed,
                        out.objectDBPatternsTried,
                        out.objectDBPatternId,
                        out.objectDBMethod);
                    if (out.ObjectDB) {
                        out.objectDBMethod = "aob_relaxed";
                    }
                }
                } // end else (4.x ObjectDB retry)

                if (out.ObjectDB) {
                    Solitaer::Summary("  Retry ObjectDB: found at %p (%s)",
                        reinterpret_cast<void*>(out.ObjectDB), out.objectDBMethod);
                }
            }
        }
    }

    // Cache results early so ValidateAndFixOffsets can access them
    s_cachedPointers = out;

    // Phase 7: Validate and fix offsets
    if (progress) progress(7, "Validating offsets...");
    ValidateAndFixOffsets();

    // Phase 8: Retry version detection if still unknown
    if (out.godotVersion.empty()) {
        if (out.Engine) {
            out.godotVersion = DetectVersionFromEngine(out.Engine);
        }
        if (out.godotVersion.empty()) {
            out.godotVersion = DetectVersionFromRdata();
        }
        if (!out.godotVersion.empty()) {
            ParseVersion(out.godotVersion, out.godotMajor, out.godotMinor, out.godotPatch);
            StoreGodotVersion(out.godotMajor, out.godotMinor, out.godotPatch);
        }
    }

    // Phase 9: Save results to hint cache
    if (progress) progress(8, "Saving hint cache...");
    wchar_t processPath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, processPath, MAX_PATH);
    char procNameA[MAX_PATH] = {};
    WideCharToMultiByte(CP_UTF8, 0,
        fs::path(processPath).stem().c_str(), -1,
        procNameA, MAX_PATH, nullptr, nullptr);
    Himmel::SaveResults(out.peHash, out, procNameA);

    // Summary
    Solitaer::Summary("=== Scan complete ===");
    Solitaer::Summary("  ObjectDB:  %s at %p",
        out.objectDBMethod, reinterpret_cast<void*>(out.ObjectDB));
    Solitaer::Summary("  ClassDB:   %s at %p",
        out.classDBMethod, reinterpret_cast<void*>(out.ClassDB));
    Solitaer::Summary("  SceneTree: %s at %p",
        out.sceneTreeMethod, reinterpret_cast<void*>(out.SceneTree));
    Solitaer::Summary("  Engine:    %s at %p",
        out.engineMethod, reinterpret_cast<void*>(out.Engine));
    Solitaer::Summary("  Version:   %s",
        out.godotVersion.empty() ? "unknown" : out.godotVersion.c_str());

    // Cache results
    s_cachedPointers = out;
    s_scanned = true;

    // Success if we found at least one useful pointer
    return (out.ObjectDB != 0 || out.ClassDB != 0 || out.SceneTree != 0 || out.Engine != 0);
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
    Solitaer::Info("OFFS", "ValidateAndFixOffsets: Probing Object layout...");

    // Step 1: Validate basic Object layout using ObjectDB
    if (s_cachedPointers.ObjectDB) {
        // Dereference object_slots pointer to get array base
        uint64_t arrayBase = Zoltraak::ReadSafe<uint64_t>(s_cachedPointers.ObjectDB, 0);
        if (!arrayBase || !Zoltraak::IsReadable(arrayBase)) {
            Solitaer::Warn("OFFS", "ObjectDB pointer at %p is null/unreadable, skipping layout probing",
                reinterpret_cast<void*>(s_cachedPointers.ObjectDB));
            arrayBase = 0;
        }

        // Find a valid Object to probe
        for (int i = 0; arrayBase && i < 100; ++i) {
            uintptr_t slotAddr = static_cast<uintptr_t>(arrayBase) +
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
                Solitaer::Info("OFFS", "Object at %p: vtable=%p, ObjectID=%llu - layout confirmed",
                    reinterpret_cast<void*>(objPtr),
                    reinterpret_cast<void*>(vtable),
                    objectId);
                break;
            }

            // If ObjectID not found at expected offset, try probing nearby
            for (int probe = 0x10; probe <= 0x30; probe += 8) {
                objectId = Zoltraak::ReadSafe<uint64_t>(objPtr + probe, 0);
                if (objectId != 0 && objectId < 0x1000000) {  // Reasonable ObjectID range
                    Solitaer::Warn("OFFS", "ObjectID found at +0x%X (expected +0x%X), adjusting DynOff",
                        probe, DynOff::OBJECT_INSTANCE_ID);
                    DynOff::OBJECT_INSTANCE_ID = probe;
                    break;
                }
            }
            break;
        }
    }

    // Step 2: Probe StringName layout via Flamme
    // Get a sample _Data* from ClassDB's head element to auto-detect SDATA_NAME offset
    if (s_cachedPointers.ClassDB && !DynOff::bStringNameProbed.load(std::memory_order_acquire)) {
        int allocOff = DynOff::CLASSDB_ALLOC_OFFSET;
        uint64_t headElem = Zoltraak::ReadSafe<uint64_t>(s_cachedPointers.ClassDB + allocOff + 0x10, 0);
        if (headElem && Zoltraak::IsReadable(headElem)) {
            // HashMapElement layout: next(8B) + prev(8B) + KeyValue at +0x10
            // Key is StringName (8B = _Data* pointer)
            uint64_t sampleDataPtr = Zoltraak::ReadSafe<uint64_t>(headElem + 0x10, 0);
            if (sampleDataPtr && Zoltraak::IsReadable(sampleDataPtr)) {
                Flamme::ProbeLayout(sampleDataPtr);
                Solitaer::Info("OFFS", "StringName probed via ClassDB: SDATA_NAME=0x%X",
                    DynOff::SDATA_NAME);
            } else {
                Solitaer::Warn("OFFS", "ClassDB head element has no valid StringName _Data*");
            }
        } else {
            Solitaer::Warn("OFFS", "ClassDB head element not readable, using default SDATA_NAME=0x%X",
                DynOff::SDATA_NAME);
        }
    }

    // Step 3: Node layout probing
    // This will be done in Phase 3 (Serie) when we have SceneTree access
    // For now, mark Node offsets as unvalidated
    if (DynOff::NODE_DATA_OFFSET == -1) {
        Solitaer::Info("OFFS", "Node offsets not yet validated (will be probed in Phase 3)");
    }

    // Mark basic offsets as validated
    DynOff::bOffsetsValidated.store(true, std::memory_order_release);
    Solitaer::Info("OFFS", "Basic offsets validated successfully");

    return true;
}

const EnginePointers& GetPointers() {
    return s_cachedPointers;
}

} // namespace Zolt::Frieren
