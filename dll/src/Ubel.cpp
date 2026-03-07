// Ubel — 優貝爾 (Assassin Specialist — Variant/Property Reader)
// Reads Godot Variant values and walks object instance properties.

#include "Ubel.h"
#include "Zoltraak.h"
#include "DiAgolze.h"
#include "Flamme.h"
#include "Solliter.h"
#include "Grimoire.h"

namespace Zolt::Ubel {

// TODO: Variant::Type enum mapping table
// NIL=0, BOOL=1, INT=2, FLOAT=3, STRING=4, VECTOR2=5, ...
static const char* VariantTypeName(int type) {
    // TODO: Full Variant::Type to string mapping
    switch (type) {
        case 0: return "Nil";
        case 1: return "bool";
        case 2: return "int";
        case 3: return "float";
        case 4: return "String";
        default: return "Unknown";
    }
}

VariantValue ReadVariant(uint64_t addr) {
    VariantValue result{};
    result.rawAddr = addr;

    if (!addr) return result;

    // TODO: Read Variant::Type at DynOff::VARIANT_TYPE
    // TODO: Based on type, read data at DynOff::VARIANT_DATA
    // TODO: Format value as string (bool, int, float, String, Vector2, etc.)
    Solliter::LogWalk("ReadVariant: TODO — not yet implemented");
    return result;
}

void WalkInstance(uint64_t /*objAddr*/,
                  const std::function<void(const FieldEntry& entry)>& callback) {
    // TODO: Determine object class via vtable or ClassDB lookup
    // TODO: Get property list from DiAgolze::GetPropertyList
    // TODO: For each property, compute address = objAddr + property.offset
    // TODO: ReadVariant at that address, build FieldEntry
    (void)callback;
    Solliter::LogWalk("WalkInstance: TODO — not yet implemented");
}

VariantValue GetFieldValue(uint64_t /*objAddr*/, const char* /*propertyName*/) {
    // TODO: Look up property offset from DiAgolze
    // TODO: Read Variant at objAddr + offset
    Solliter::LogWalk("GetFieldValue: TODO — not yet implemented");
    return {};
}

} // namespace Zolt::Ubel
