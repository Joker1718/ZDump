// Ubel — 優貝爾 (Assassin Specialist — Variant/Property Reader)
// Reads Godot Variant values and walks object instance properties.
// Extracts field metadata from ClassDB and reads Variant values from memory.

#include "Ubel.h"
#include "Zoltraak.h"
#include "DiAgolze.h"
#include "Flamme.h"
#include "Solliter.h"
#include "Grimoire.h"

#include <cstdio>
#include <iterator>

namespace Zolt::Ubel {

// ============================================================
// Variant::Type enum to string mapping (full 39 types)
// ============================================================
static const char* VariantTypeName(int type) {
    static const char* names[] = {
        "Nil",                  // 0
        "bool",                 // 1
        "int",                  // 2
        "float",                // 3
        "String",               // 4
        "Vector2",              // 5
        "Vector2i",             // 6
        "Rect2",                // 7
        "Rect2i",               // 8
        "Vector3",              // 9
        "Vector3i",             // 10
        "Transform2D",          // 11
        "Vector4",              // 12
        "Vector4i",             // 13
        "Plane",                // 14
        "Quaternion",           // 15
        "AABB",                 // 16
        "Basis",                // 17
        "Transform3D",          // 18
        "Projection",           // 19
        "Color",                // 20
        "StringName",           // 21
        "NodePath",             // 22
        "RID",                  // 23
        "Object",               // 24
        "Callable",             // 25
        "Signal",               // 26
        "Dictionary",           // 27
        "Array",                // 28
        "PackedByteArray",      // 29
        "PackedInt32Array",     // 30
        "PackedInt64Array",     // 31
        "PackedFloat32Array",   // 32
        "PackedFloat64Array",   // 33
        "PackedStringArray",    // 34
        "PackedVector2Array",   // 35
        "PackedVector3Array",   // 36
        "PackedColorArray",     // 37
        "PackedVector4Array",   // 38
    };
    if (type >= 0 && type < static_cast<int>(std::size(names)))
        return names[type];
    return "Unknown";
}

// ============================================================
// Variant size table (in-memory size of the data portion)
// ============================================================
static int VariantDataSize(int type) {
    switch (type) {
        case 0:  return 0;    // Nil
        case 1:  return 4;    // bool (uint32_t)
        case 2:  return 8;    // int (int64_t)
        case 3:  return 8;    // float (double)
        case 4:  return 8;    // String (CowData ptr)
        case 5:  return 8;    // Vector2 (2 floats)
        case 6:  return 8;    // Vector2i (2 int32)
        case 7:  return 16;   // Rect2 (4 floats)
        case 8:  return 16;   // Rect2i (4 int32)
        case 9:  return 12;   // Vector3 (3 floats)
        case 10: return 12;   // Vector3i (3 int32)
        case 11: return 8;    // Transform2D (ptr)
        case 12: return 16;   // Vector4 (4 floats)
        case 13: return 16;   // Vector4i (4 int32)
        case 14: return 16;   // Plane (4 floats: normal + d)
        case 15: return 16;   // Quaternion (4 floats)
        case 16: return 24;   // AABB (6 floats: pos + size)
        case 17: return 8;    // Basis (ptr)
        case 18: return 8;    // Transform3D (ptr)
        case 19: return 8;    // Projection (ptr)
        case 20: return 16;   // Color (4 floats)
        case 21: return 8;    // StringName (_Data* ptr)
        case 22: return 8;    // NodePath (ptr)
        case 23: return 8;    // RID (uint64_t)
        case 24: return 16;   // Object (ObjectID + Object*)
        case 25: return 8;    // Callable (ptr)
        case 26: return 8;    // Signal (ptr)
        case 27: return 8;    // Dictionary (ptr)
        case 28: return 8;    // Array (ptr)
        default: return 8;    // Packed arrays (RefCounted ptr)
    }
}

// Format a float without unnecessary trailing zeros
static std::string FormatFloat(float v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.4g", static_cast<double>(v));
    return buf;
}

static std::string FormatDouble(double v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

// ============================================================
// ReadVariant — Read and decode a Variant at the given address
// ============================================================
VariantValue ReadVariant(uint64_t addr) {
    VariantValue result{};
    result.rawAddr = addr;

    if (!addr || !Zoltraak::IsReadable(addr)) return result;

    // Read Variant::Type (int32 at offset 0)
    int32_t type = Zoltraak::ReadSafe<int32_t>(addr + DynOff::VARIANT_TYPE, -1);
    if (type < 0 || type > 38) return result;

    result.type     = type;
    result.typeName = VariantTypeName(type);

    uint64_t data = addr + DynOff::VARIANT_DATA;

    switch (type) {
        case 0: // NIL
            result.value = "(null)";
            break;

        case 1: // BOOL
            result.value = Zoltraak::ReadSafe<uint32_t>(data, 0) ? "true" : "false";
            break;

        case 2: // INT (int64_t)
            result.value = std::to_string(Zoltraak::ReadSafe<int64_t>(data, 0));
            break;

        case 3: // FLOAT (double)
            result.value = FormatDouble(Zoltraak::ReadSafe<double>(data, 0.0));
            break;

        case 4: { // STRING (CowData<char32_t>)
            // String is stored inline as CowData which is essentially a pointer
            uint64_t cowPtr = Zoltraak::ReadSafe<uint64_t>(data, 0);
            if (cowPtr && Zoltraak::IsReadable(cowPtr)) {
                result.value = Zoltraak::ReadGodotString(cowPtr);
            } else {
                result.value = "\"\"";
            }
            break;
        }

        case 5: { // VECTOR2 (2 floats inline)
            float x = Zoltraak::ReadSafe<float>(data, 0.0f);
            float y = Zoltraak::ReadSafe<float>(data + 4, 0.0f);
            result.value = "(" + FormatFloat(x) + ", " + FormatFloat(y) + ")";
            break;
        }

        case 6: { // VECTOR2I (2 int32 inline)
            int32_t x = Zoltraak::ReadSafe<int32_t>(data, 0);
            int32_t y = Zoltraak::ReadSafe<int32_t>(data + 4, 0);
            result.value = "(" + std::to_string(x) + ", " + std::to_string(y) + ")";
            break;
        }

        case 7: { // RECT2 (4 floats: x, y, w, h)
            float x = Zoltraak::ReadSafe<float>(data, 0.0f);
            float y = Zoltraak::ReadSafe<float>(data + 4, 0.0f);
            float w = Zoltraak::ReadSafe<float>(data + 8, 0.0f);
            float h = Zoltraak::ReadSafe<float>(data + 12, 0.0f);
            result.value = "(" + FormatFloat(x) + ", " + FormatFloat(y) +
                          ", " + FormatFloat(w) + ", " + FormatFloat(h) + ")";
            break;
        }

        case 8: { // RECT2I (4 int32)
            int32_t x = Zoltraak::ReadSafe<int32_t>(data, 0);
            int32_t y = Zoltraak::ReadSafe<int32_t>(data + 4, 0);
            int32_t w = Zoltraak::ReadSafe<int32_t>(data + 8, 0);
            int32_t h = Zoltraak::ReadSafe<int32_t>(data + 12, 0);
            result.value = "(" + std::to_string(x) + ", " + std::to_string(y) +
                          ", " + std::to_string(w) + ", " + std::to_string(h) + ")";
            break;
        }

        case 9: { // VECTOR3 (3 floats inline)
            float x = Zoltraak::ReadSafe<float>(data, 0.0f);
            float y = Zoltraak::ReadSafe<float>(data + 4, 0.0f);
            float z = Zoltraak::ReadSafe<float>(data + 8, 0.0f);
            result.value = "(" + FormatFloat(x) + ", " + FormatFloat(y) + ", " + FormatFloat(z) + ")";
            break;
        }

        case 10: { // VECTOR3I (3 int32)
            int32_t x = Zoltraak::ReadSafe<int32_t>(data, 0);
            int32_t y = Zoltraak::ReadSafe<int32_t>(data + 4, 0);
            int32_t z = Zoltraak::ReadSafe<int32_t>(data + 8, 0);
            result.value = "(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")";
            break;
        }

        case 12: { // VECTOR4 (4 floats inline)
            float x = Zoltraak::ReadSafe<float>(data, 0.0f);
            float y = Zoltraak::ReadSafe<float>(data + 4, 0.0f);
            float z = Zoltraak::ReadSafe<float>(data + 8, 0.0f);
            float w = Zoltraak::ReadSafe<float>(data + 12, 0.0f);
            result.value = "(" + FormatFloat(x) + ", " + FormatFloat(y) +
                          ", " + FormatFloat(z) + ", " + FormatFloat(w) + ")";
            break;
        }

        case 13: { // VECTOR4I (4 int32)
            int32_t x = Zoltraak::ReadSafe<int32_t>(data, 0);
            int32_t y = Zoltraak::ReadSafe<int32_t>(data + 4, 0);
            int32_t z = Zoltraak::ReadSafe<int32_t>(data + 8, 0);
            int32_t w = Zoltraak::ReadSafe<int32_t>(data + 12, 0);
            result.value = "(" + std::to_string(x) + ", " + std::to_string(y) +
                          ", " + std::to_string(z) + ", " + std::to_string(w) + ")";
            break;
        }

        case 14: { // PLANE (normal + d = 4 floats)
            float nx = Zoltraak::ReadSafe<float>(data, 0.0f);
            float ny = Zoltraak::ReadSafe<float>(data + 4, 0.0f);
            float nz = Zoltraak::ReadSafe<float>(data + 8, 0.0f);
            float d  = Zoltraak::ReadSafe<float>(data + 12, 0.0f);
            result.value = "Plane(" + FormatFloat(nx) + ", " + FormatFloat(ny) +
                          ", " + FormatFloat(nz) + ", " + FormatFloat(d) + ")";
            break;
        }

        case 15: { // QUATERNION (4 floats)
            float x = Zoltraak::ReadSafe<float>(data, 0.0f);
            float y = Zoltraak::ReadSafe<float>(data + 4, 0.0f);
            float z = Zoltraak::ReadSafe<float>(data + 8, 0.0f);
            float w = Zoltraak::ReadSafe<float>(data + 12, 0.0f);
            result.value = "Quat(" + FormatFloat(x) + ", " + FormatFloat(y) +
                          ", " + FormatFloat(z) + ", " + FormatFloat(w) + ")";
            break;
        }

        case 20: { // COLOR (4 floats: RGBA)
            float r = Zoltraak::ReadSafe<float>(data, 0.0f);
            float g = Zoltraak::ReadSafe<float>(data + 4, 0.0f);
            float b = Zoltraak::ReadSafe<float>(data + 8, 0.0f);
            float a = Zoltraak::ReadSafe<float>(data + 12, 0.0f);
            result.value = "Color(" + FormatFloat(r) + ", " + FormatFloat(g) +
                          ", " + FormatFloat(b) + ", " + FormatFloat(a) + ")";
            break;
        }

        case 21: { // STRINGNAME
            // StringName = _Data* pointer
            result.value = Flamme::Resolve(data);
            if (result.value.empty()) result.value = "(empty StringName)";
            break;
        }

        case 23: { // RID (uint64_t)
            uint64_t rid = Zoltraak::ReadSafe<uint64_t>(data, 0);
            char buf[32];
            snprintf(buf, sizeof(buf), "RID(%llu)", static_cast<unsigned long long>(rid));
            result.value = buf;
            break;
        }

        case 24: { // OBJECT — ObjData { ObjectID id(8B); Object* obj(8B) }
            uint64_t objId  = Zoltraak::ReadSafe<uint64_t>(data, 0);
            uint64_t objPtr = Zoltraak::ReadSafe<uint64_t>(data + 8, 0);
            char buf[64];
            snprintf(buf, sizeof(buf), "Object@%llX (id=%llu)",
                static_cast<unsigned long long>(objPtr),
                static_cast<unsigned long long>(objId));
            result.value = buf;
            break;
        }

        default: {
            // For unhandled types (Transform2D, Basis, Transform3D, Projection,
            // AABB, NodePath, Callable, Signal, Dictionary, Array, PackedArrays):
            // Show type name + raw hex of data portion
            int dataSize = VariantDataSize(type);
            if (dataSize <= 0) dataSize = 8;
            if (dataSize > 24) dataSize = 24;

            uint8_t raw[24] = {};
            Zoltraak::ReadSafe(data, raw, dataSize);

            std::string hex;
            hex.reserve(dataSize * 2);
            for (int i = 0; i < dataSize; i++) {
                char h[3];
                snprintf(h, sizeof(h), "%02X", raw[i]);
                hex += h;
            }
            result.value = result.typeName + std::string("{") + hex + "}";
            break;
        }
    }

    return result;
}

// ============================================================
// WalkInstance — Walk all properties of an object instance
// ============================================================
void WalkInstance(uint64_t objAddr,
                  const std::function<void(const FieldEntry& entry)>& callback) {
    if (!objAddr || !callback) return;

    // Step 1: Determine object class via RTTI
    std::string className = DiAgolze::GetObjectClass(objAddr);
    if (className.empty()) {
        Solliter::Debug("WALK", "WalkInstance: could not determine class for object at %p",
            reinterpret_cast<void*>(objAddr));
        return;
    }

    // Step 2: Get property list from ClassDB (includes inherited properties)
    auto properties = DiAgolze::GetPropertyList(className.c_str());
    if (properties.empty()) {
        Solliter::Debug("WALK", "WalkInstance: no properties for class '%s'", className.c_str());
        return;
    }

    Solliter::Debug("WALK", "WalkInstance: %s @ %p — %d properties",
        className.c_str(), reinterpret_cast<void*>(objAddr),
        static_cast<int>(properties.size()));

    // Step 3: Build FieldEntry for each property
    // Note: Godot ClassDB does not expose byte offsets for properties.
    // We provide property metadata (name, type) without live values for now.
    // Live value reading requires calling MethodBind getters (future work).
    for (auto& prop : properties) {
        FieldEntry entry;
        entry.name      = prop.name;
        entry.className = prop.className;

        // Set up VariantValue with type metadata but no live value
        entry.value.type     = prop.type;
        entry.value.typeName = VariantTypeName(prop.type);
        entry.value.value    = "";      // No live value (would need getter call)
        entry.value.rawAddr  = 0;       // No address (offset unknown)

        callback(entry);
    }
}

// ============================================================
// GetFieldValue — Get a single field value by property name
// ============================================================
VariantValue GetFieldValue(uint64_t objAddr, const char* propertyName) {
    if (!objAddr || !propertyName) return {};

    // Look up the class and property
    std::string className = DiAgolze::GetObjectClass(objAddr);
    if (className.empty()) {
        Solliter::Debug("WALK", "GetFieldValue: unknown class for %p", reinterpret_cast<void*>(objAddr));
        return {};
    }

    auto properties = DiAgolze::GetPropertyList(className.c_str());
    for (auto& prop : properties) {
        if (prop.name == propertyName) {
            // Found the property — return type metadata
            // Live value reading requires MethodBind getter invocation (future)
            VariantValue result;
            result.type     = prop.type;
            result.typeName = VariantTypeName(prop.type);
            result.rawAddr  = 0;
            return result;
        }
    }

    return {};
}

} // namespace Zolt::Ubel
