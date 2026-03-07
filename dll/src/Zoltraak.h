#pragma once

// Zoltraak — 栔爾朵列 (攻撃魔法 — Attack Magic)
// Core memory scanning: AOB pattern matching, SEH-protected reads,
// RIP-relative resolution, module enumeration.
// Named after Frieren's signature spell — the magic that deconstructs.

#include <cstdint>
#include <string>
#include <vector>

namespace Zolt::Zoltraak {

// Get base address and size of a module by name (nullptr = main module)
uint64_t GetModuleBase(const char* moduleName = nullptr);
uint64_t GetModuleSize(const char* moduleName = nullptr);

// AOB pattern scan on the .text section of the main module
// pattern: space-separated hex bytes, ?? for wildcard
// Returns: address of first match, or 0 if not found
uint64_t AOBScan(const char* pattern);

// AOB pattern scan within a specific address range
uint64_t AOBScanRange(const char* pattern, uint64_t start, uint64_t size);

// Resolve a RIP-relative instruction target
// matchAddr: address where pattern matched
// instrOffset: offset within match to the instruction
// opcodeLen: bytes of opcode before displacement
// totalLen: total instruction length
uint64_t ResolveRIP(uint64_t matchAddr, int instrOffset, int opcodeLen, int totalLen);

// SEH-protected memory read — returns false if access violation
bool ReadSafe(uint64_t addr, void* buffer, size_t size);

// SEH-protected single-value reads
template<typename T>
T ReadSafe(uint64_t addr, T defaultValue = T{}) {
    T value = defaultValue;
    ReadSafe(addr, &value, sizeof(T));
    return value;
}

// Write memory at address (with page protection adjustment)
bool WriteMem(uint64_t addr, const void* data, size_t size);

// Check if an address is in a heap region (not .text section)
bool IsHeapAddress(uint64_t addr);

// Check if an address is readable
bool IsReadable(uint64_t addr);

// Read a null-terminated string from memory
std::string ReadString(uint64_t addr, size_t maxLen = 256);

// Read a UTF-32 Godot String from its CowData pointer
std::string ReadGodotString(uint64_t cowDataPtr);

} // namespace Zolt::Zoltraak
