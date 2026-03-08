#pragma once

// Ausetiese — 服從之秤 (Scales of Obedience — ObjectDB Enumeration)
// Enumerates the Godot ObjectDB sparse array.
// Provides iteration, lookup by index/name/address.

#include <cstdint>
#include <functional>
#include <string>

namespace Zolt::Ausetiese {

// Initialize with the ObjectDB base address (address of object_slots pointer in .bss/.data)
void Init(uint64_t objectDBAddr);

// Get the total object count in the ObjectDB (cached, refreshed on ForEach)
int GetObjectCount();

// Get the max slot index (array capacity)
int GetSlotMax();

// Get object address by slot index (returns 0 if slot is empty)
uint64_t GetObjectByIndex(int index);

// Iterate all valid objects, calling callback(index, objectAddr)
void ForEach(const std::function<void(int index, uint64_t objectAddr)>& callback);

// Iterate all valid objects with early-exit support (return false to stop)
bool ForEachEx(const std::function<bool(int index, uint64_t objectAddr)>& callback);

// Find an object by its class/node name (linear scan)
uint64_t FindByName(const char* name);

// Find an object slot index by its address
int FindByAddress(uint64_t addr);

// One-shot diagnostics dump
void LogDiagnostics();

} // namespace Zolt::Ausetiese
