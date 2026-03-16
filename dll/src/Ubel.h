#pragma once

// Ubel — 優貝爾 (Assassin Specialist — Variant/Property Reader)
// Reads Godot Variant values and walks object instance properties.
// Extracts field values from live object memory.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Zolt::Ubel {

// Represents a decoded Variant value
struct VariantValue {
    int         type = 0;       // Variant::Type enum
    std::string typeName;       // Human-readable type name
    std::string value;          // String representation of the value
    uint64_t    rawAddr = 0;    // Address of the variant data (where the field lives)
    uint64_t    ptrTarget = 0;  // For Object-type: address of the pointed-to Object (0 if not applicable)
};

// Property with its value
struct FieldEntry {
    std::string  name;
    std::string  className;  // owning class
    VariantValue value;
    int          offset = -1;    // byte offset within object (-1 if unknown)
    int          nativeSize = 0; // native access size (1, 4, 8)
    int          bitShift = 0;   // right-shift for bitfield extraction (0 = none)
    int          bitMask  = 0;   // bitmask after shift (0 = use full value)
    bool         fromPtrcall = false; // true if value obtained via runtime ptrcall
    int          hint = 0;       // PropertyHint enum value
    std::string  hintString;     // hint parameter string
    uint32_t     usage = 0;      // PROPERTY_USAGE_* flags
};

// Read and decode a Variant at the given address
VariantValue ReadVariant(uint64_t addr);

// Walk all properties of an object instance, reading their values
// Calls callback(fieldEntry) for each property
void WalkInstance(uint64_t objAddr,
                  const std::function<void(const FieldEntry& entry)>& callback);

// Get a single field value by property name
VariantValue GetFieldValue(uint64_t objAddr, const char* propertyName);

// Proactively scan ObjectDB for scripted nodes and probe ScriptInstance offset.
// Called during init to discover the offset early, before any walk_instance call.
// Ubel — 優貝爾 (Assassin Specialist) — Early script probe for GDScript member access
void ProbeScriptInstanceEarly();

// Result for ScriptInstance member address lookup
struct ScriptMemberMatch {
    uint64_t objectAddr   = 0;    // Owning Object address
    int      memberIndex  = -1;   // Index within members Vector
    int      variantOffset = 0;   // Byte offset within the Variant (addr - variantStart)
};

// Search ObjectDB for an Object whose ScriptInstance members Vector contains 'addr'.
// Used when a user has a GDScript variable address (e.g. Money from CE) and wants
// to find the owning Object. Returns empty match if not found.
ScriptMemberMatch FindScriptMemberByAddress(uint64_t addr);

} // namespace Zolt::Ubel
