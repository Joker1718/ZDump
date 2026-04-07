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

// Flamme — 弗蘭梅 (Great Mage Master — StringName Resolver)
// Resolves Godot StringName pointers to readable UTF-8 strings.
// StringName is Godot's interned string type used everywhere.

#include <cstdint>
#include <string>

namespace Zolt::Flamme {

// Resolve a StringName at the given address to a UTF-8 string.
// Reads the _Data pointer, then extracts the String name field.
// Returns empty string on failure or null StringName.
std::string Resolve(uint64_t stringNameAddr);

// Resolve directly from a _Data* pointer (skips StringName indirection).
// Useful when you already have the _Data pointer from another source.
std::string ResolveAt(uint64_t dataPtr);

// Auto-detect the SDATA_NAME offset within _Data struct.
// Tests candidate offsets against a known StringName (from ClassDB).
// Called by Frieren::ValidateAndFixOffsets() during init.
void ProbeLayout(uint64_t sampleDataPtr = 0);

// One-shot diagnostic dump of StringName resolution internals.
// Hex dumps a sample _Data struct and shows results for each candidate offset.
void LogDiagnostics(uint64_t sampleAddr = 0);

} // namespace Zolt::Flamme
