#pragma once

// Flamme — 芙拉梅 (Great Mage Master — StringName Resolver)
// Resolves Godot StringName pointers to readable UTF-8 strings.
// StringName is Godot's interned string type used everywhere.

#include <cstdint>
#include <string>

namespace Zolt::Flamme {

// Resolve a StringName at the given address to a UTF-8 string.
// Reads the _Data pointer, then extracts the String name field.
// Returns empty string on failure.
std::string Resolve(uint64_t stringNameAddr);

} // namespace Zolt::Flamme
