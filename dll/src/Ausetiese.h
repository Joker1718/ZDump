/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
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

// Find an object slot index by its address (exact match)
int FindByAddress(uint64_t addr);

// Result for range-based address lookup
struct AddressMatch {
    int      slotIndex    = -1;   // ObjectDB slot index (-1 = not found)
    uint64_t objectAddr   = 0;    // Start address of containing Object
    int      memberOffset = 0;    // Displacement: addr - objectAddr
};

// Find the Object whose address range contains 'addr'.
// Falls back to sorted binary search when exact match fails.
// Used when the user has a member variable address (e.g. from CE) and wants to find the owning Object.
AddressMatch FindByAddressRange(uint64_t addr);

// Directly validate whether an address looks like a valid Godot Object.
// Checks: readable, vtable in module range, first two vtable entries in .text.
// Does NOT require the object to be in ObjectDB — used as fallback when
// ObjectDB lookup fails (e.g. slot_max estimation too low).
bool ValidateObjectAddress(uint64_t addr);

// One-shot diagnostics dump
void LogDiagnostics();

} // namespace Zolt::Ausetiese
