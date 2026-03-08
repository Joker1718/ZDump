// Zoltraak — 栔爾朵列 (攻撃魔法 — Attack Magic)
// Core memory scanning: AOB pattern matching, SEH-protected reads,
// RIP-relative resolution, module enumeration.

#include "Zoltraak.h"
#include <Windows.h>
#include <Psapi.h>
#include <cstring>
#include <sstream>

namespace Zolt::Zoltraak {

uint64_t GetModuleBase(const char* moduleName) {
    HMODULE hModule = nullptr;
    if (moduleName) {
        hModule = GetModuleHandleA(moduleName);
    } else {
        hModule = GetModuleHandleA(nullptr);
    }
    return reinterpret_cast<uint64_t>(hModule);
}

uint64_t GetModuleSize(const char* moduleName) {
    HMODULE hModule = nullptr;
    if (moduleName) {
        hModule = GetModuleHandleA(moduleName);
    } else {
        hModule = GetModuleHandleA(nullptr);
    }
    if (!hModule) return 0;

    MODULEINFO modInfo{};
    if (GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(modInfo))) {
        return modInfo.SizeOfImage;
    }
    return 0;
}

// Parse AOB pattern string into bytes and mask
static bool ParsePattern(const char* pattern, std::vector<uint8_t>& bytes, std::vector<bool>& mask) {
    bytes.clear();
    mask.clear();

    std::istringstream stream(pattern);
    std::string token;
    while (stream >> token) {
        if (token == "??" || token == "?") {
            bytes.push_back(0);
            mask.push_back(false);  // wildcard
        } else {
            bytes.push_back(static_cast<uint8_t>(std::stoul(token, nullptr, 16)));
            mask.push_back(true);   // must match
        }
    }
    return !bytes.empty();
}

// ============================================================
// PE section parsing helpers
// ============================================================

struct SectionCacheEntry {
    SectionInfo text, data, bss, rdata;
    bool initialized = false;
};

static SectionCacheEntry s_sectionCache;

static void InitSectionCache() {
    if (s_sectionCache.initialized) return;

    uint64_t base = GetModuleBase();
    if (!base) return;

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;

    auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        char name[9] = {};
        memcpy(name, section[i].Name, 8);

        SectionInfo info;
        info.baseAddr = base + section[i].VirtualAddress;
        info.size = section[i].Misc.VirtualSize;

        if (strcmp(name, ".text") == 0) s_sectionCache.text = info;
        else if (strcmp(name, ".data") == 0) s_sectionCache.data = info;
        else if (strcmp(name, ".bss") == 0) s_sectionCache.bss = info;
        else if (strcmp(name, ".rdata") == 0) s_sectionCache.rdata = info;
    }
    s_sectionCache.initialized = true;
}

SectionInfo GetTextSection() { InitSectionCache(); return s_sectionCache.text; }
SectionInfo GetDataSection() { InitSectionCache(); return s_sectionCache.data; }
SectionInfo GetBssSection() { InitSectionCache(); return s_sectionCache.bss; }
SectionInfo GetRdataSection() { InitSectionCache(); return s_sectionCache.rdata; }

bool IsInWritableSection(uint64_t addr) {
    InitSectionCache();
    auto& d = s_sectionCache.data;
    auto& b = s_sectionCache.bss;
    return (d.size > 0 && addr >= d.baseAddr && addr < d.baseAddr + d.size) ||
           (b.size > 0 && addr >= b.baseAddr && addr < b.baseAddr + b.size);
}

// ============================================================
// AOB scanning
// ============================================================

uint64_t AOBScan(const char* pattern) {
    uint64_t base = GetModuleBase();
    uint64_t size = GetModuleSize();
    if (!base || !size) return 0;

    return AOBScanRange(pattern, base, size);
}

uint64_t AOBScanRange(const char* pattern, uint64_t start, uint64_t size) {
    std::vector<uint8_t> patBytes;
    std::vector<bool> patMask;
    if (!ParsePattern(pattern, patBytes, patMask)) return 0;

    const uint8_t* mem = reinterpret_cast<const uint8_t*>(start);
    const size_t patLen = patBytes.size();

    for (size_t i = 0; i <= size - patLen; ++i) {
        bool found = true;
        for (size_t j = 0; j < patLen; ++j) {
            if (patMask[j] && mem[i + j] != patBytes[j]) {
                found = false;
                break;
            }
        }
        if (found) {
            return start + i;
        }
    }
    return 0;
}

std::vector<uint64_t> AOBScanAll(const char* pattern, size_t maxResults) {
    std::vector<uint64_t> results;

    std::vector<uint8_t> patBytes;
    std::vector<bool> patMask;
    if (!ParsePattern(pattern, patBytes, patMask)) return results;

    uint64_t base = GetModuleBase();
    uint64_t size = GetModuleSize();
    if (!base || !size) return results;

    const uint8_t* mem = reinterpret_cast<const uint8_t*>(base);
    const size_t patLen = patBytes.size();

    for (size_t i = 0; i <= size - patLen && results.size() < maxResults; ++i) {
        bool found = true;
        for (size_t j = 0; j < patLen; ++j) {
            if (patMask[j] && mem[i + j] != patBytes[j]) {
                found = false;
                break;
            }
        }
        if (found) {
            results.push_back(base + i);
        }
    }
    return results;
}

uint64_t ResolveRIP(uint64_t matchAddr, int instrOffset, int opcodeLen, int totalLen) {
    uint64_t instrAddr = matchAddr + instrOffset;
    int32_t disp = 0;
    if (!ReadSafe(instrAddr + opcodeLen, &disp, sizeof(disp))) return 0;
    return instrAddr + totalLen + disp;
}

bool ReadSafe(uint64_t addr, void* buffer, size_t size) {
    __try {
        memcpy(buffer, reinterpret_cast<const void*>(addr), size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WriteMem(uint64_t addr, const void* data, size_t size) {
    DWORD oldProtect;
    if (!VirtualProtect(reinterpret_cast<void*>(addr), size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }
    memcpy(reinterpret_cast<void*>(addr), data, size);
    VirtualProtect(reinterpret_cast<void*>(addr), size, oldProtect, &oldProtect);
    return true;
}

bool IsHeapAddress(uint64_t addr) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<const void*>(addr), &mbi, sizeof(mbi)) == 0) {
        return false;
    }
    // Heap memory is typically MEM_PRIVATE with PAGE_READWRITE
    return (mbi.State == MEM_COMMIT) &&
           (mbi.Type == MEM_PRIVATE) &&
           !(mbi.Protect & PAGE_EXECUTE);
}

bool IsReadable(uint64_t addr) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<const void*>(addr), &mbi, sizeof(mbi)) == 0) {
        return false;
    }
    return (mbi.State == MEM_COMMIT) &&
           (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE));
}

std::string ReadString(uint64_t addr, size_t maxLen) {
    std::string result;
    result.reserve(maxLen);

    for (size_t i = 0; i < maxLen; ++i) {
        char c = ReadSafe<char>(addr + i, '\0');
        if (c == '\0') break;
        result.push_back(c);
    }
    return result;
}

// Helper: convert char32_t[] to UTF-8, reading from memory
static std::string ConvertChar32ToUtf8(uint64_t dataAddr, uint32_t maxChars) {
    std::string result;
    result.reserve(maxChars);

    for (uint32_t i = 0; i < maxChars; ++i) {
        uint32_t ch = ReadSafe<uint32_t>(dataAddr + i * 4, 0);
        if (ch == 0) break;

        // Simple UTF-32 to UTF-8 conversion
        if (ch < 0x80) {
            result.push_back(static_cast<char>(ch));
        } else if (ch < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (ch >> 6)));
            result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else if (ch < 0x10000) {
            result.push_back(static_cast<char>(0xE0 | (ch >> 12)));
            result.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xF0 | (ch >> 18)));
            result.push_back(static_cast<char>(0x80 | ((ch >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        }
    }
    return result;
}

std::string ReadGodotString(uint64_t cowDataPtr) {
    if (!cowDataPtr || !IsReadable(cowDataPtr)) return "";

    // Godot CowData<char32_t> layout:
    // _ptr points to char32_t[] data. Size is stored before the data.
    // The layout changed across Godot versions:
    //
    // Old layout (Godot 4.0-4.2): [uint32_t refcount][uint32_t size][data...]
    //   → size at _ptr - 4 (uint32_t)
    //
    // New layout (Godot 4.3+): [SafeNumeric<uint64_t> refcount][uint64_t capacity][uint64_t size][padding][data...]
    //   → size at _ptr - 0x10 (uint64_t), DATA_OFFSET = 0x20
    //
    // Since the layout varies, we try multiple strategies and validate each one.
    // A size is valid if: (a) it's in range 1..4096, and (b) char32_t[size] == 0 (null terminator).

    // Strategy 1: Old CowData layout — uint32_t size at _ptr - 4
    uint32_t size = ReadSafe<uint32_t>(cowDataPtr - 4, 0);
    if (size > 0 && size <= 4096) {
        // Validate: null terminator at expected position
        uint32_t term = ReadSafe<uint32_t>(cowDataPtr + size * 4, 0);
        if (term == 0) {
            return ConvertChar32ToUtf8(cowDataPtr, size);
        }
    }

    // Strategy 2: New CowData layout — uint64_t size at _ptr - 0x10
    uint64_t size64 = ReadSafe<uint64_t>(cowDataPtr - 0x10, 0);
    if (size64 > 0 && size64 <= 4096) {
        uint32_t s = static_cast<uint32_t>(size64);
        uint32_t term = ReadSafe<uint32_t>(cowDataPtr + s * 4, 0);
        if (term == 0) {
            return ConvertChar32ToUtf8(cowDataPtr, s);
        }
    }

    // Strategy 3: Try uint32_t at _ptr - 8 (some intermediate layouts)
    size = ReadSafe<uint32_t>(cowDataPtr - 8, 0);
    if (size > 0 && size <= 4096) {
        uint32_t term = ReadSafe<uint32_t>(cowDataPtr + size * 4, 0);
        if (term == 0) {
            return ConvertChar32ToUtf8(cowDataPtr, size);
        }
    }

    // Strategy 4: Null-terminated read (most robust fallback)
    // Works regardless of header layout — just read char32_t until null
    return ConvertChar32ToUtf8(cowDataPtr, 256);
}

std::string ReadChar32String(uint64_t addr, size_t maxChars) {
    if (!addr || !IsReadable(addr)) return "";
    return ConvertChar32ToUtf8(addr, static_cast<uint32_t>(maxChars));
}

} // namespace Zolt::Zoltraak
