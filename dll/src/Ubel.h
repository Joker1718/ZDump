#pragma once

// Ubel — 尤蓓爾 (Assassin Specialist — Variant/Property Reader)
// Reads Godot Variant values and walks object instance properties.
// Extracts field values from live object memory.

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
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

// Version-aware Variant::Type → display name (3.x returns "real", 4.x returns "float", etc.)
const char* VariantTypeName(int type);

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
// Ubel — 尤蓓爾 (Assassin Specialist) — Early script probe for GDScript member access
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

// Get GDScript member names from a GDScript object (index → name mapping).
// Public wrapper around internal ReadGDScriptMemberNames for mailbox use.
std::unordered_map<int, std::string> GetMemberNames(uint64_t gdscriptPtr);

// Result for container owner address lookup
struct ContainerOwnerMatch {
    uint64_t objectAddr   = 0;    // Owning Object address
    int      memberIndex  = -1;   // Script member index containing the container
    std::string containerType;    // "Array" or "Dictionary"
};

// Search ObjectDB for an Object whose script member containers (Array/Dictionary)
// hold the target address in their internal data. Handles addresses inside
// container elements that FindScriptMemberByAddress misses.
ContainerOwnerMatch FindContainerOwnerByAddress(uint64_t addr);

// Search ALL ObjectDB objects' raw memory for native Dictionary/Array properties
// containing the target address. Scans Object body bytes for potential container
// private pointers. Slower but catches native C++ properties that scripts don't expose.
ContainerOwnerMatch FindNativeContainerByAddress(uint64_t addr);

// ============================================================
// Container reading (Array / Dictionary / PackedArray)
// ============================================================

// Element within a container
struct ContainerElement {
    int          index = 0;   // Element index (0-based for Array, sequential for Dictionary)
    std::string  key;         // For Dictionary: key display string (empty for Array)
    VariantValue value;       // Decoded element value
};

// Information about a container field
struct ContainerInfo {
    std::string containerType;    // "Array", "Dictionary", "PackedByteArray", etc.
    int         elementCount = 0; // Total number of elements
    std::string elementType;      // Typed array element type name (empty if untyped)
    std::string keyType;          // Dictionary key type name (empty for Array)
};

// Read container metadata (count, types) without reading elements.
// dataAddr = address of the Variant data (the pointer to the container internals).
// variantType = raw Variant::Type value (version-specific).
ContainerInfo ReadContainerInfo(uint64_t dataAddr, int variantType);

// Read container elements with pagination.
// dataAddr = address of the Variant data (the pointer to the container internals).
// variantType = raw Variant::Type value (version-specific).
// offset/limit = pagination (0-based).
std::vector<ContainerElement> ReadContainerElements(uint64_t dataAddr, int variantType,
                                                     int offset = 0, int limit = 100);

// Find a dictionary element by key string and return its value Variant's data address.
// Walks the dictionary linked list, decoding each key and comparing to `key`.
// Returns the value Variant data address (Variant + VARIANT_DATA), or 0 if not found.
// dataAddr = address of the Variant data field (the pointer to DictPrivate).
uint64_t FindDictValueByKey(uint64_t dataAddr, const std::string& key);

// Find an array element by index and return its value Variant's data address.
// Returns the element Variant data address (Variant + VARIANT_DATA), or 0 if not found.
// dataAddr = address of the Variant data field (the pointer to array internals).
uint64_t FindArrayValueByIndex(uint64_t dataAddr, int index);

} // namespace Zolt::Ubel
