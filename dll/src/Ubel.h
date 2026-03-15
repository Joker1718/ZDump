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
    int         type;       // Variant::Type enum
    std::string typeName;   // Human-readable type name
    std::string value;      // String representation of the value
    uint64_t    rawAddr;    // Address of the variant data
};

// Property with its value
struct FieldEntry {
    std::string  name;
    std::string  className;  // owning class
    VariantValue value;
    int          offset = -1;    // byte offset within object (-1 if unknown)
    int          nativeSize = 0; // native access size (1, 4, 8)
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

} // namespace Zolt::Ubel
