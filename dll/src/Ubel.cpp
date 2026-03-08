// Ubel — 優貝爾 (Assassin Specialist — Variant/Property Reader)
// Reads Godot Variant values and walks object instance properties.
// Extracts field metadata from ClassDB and reads Variant values from memory.
//
// Version-aware Variant::Type handling:
//   Godot 3.x: 27 types (0-26), different enum numbering from type 6+
//   Godot 4.0-4.4: 38 types (0-37)
//   Godot 4.5+: 39 types (0-38, added PackedVector4Array)

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
// Godot 4.x Variant::Type enum to string mapping (39 types max)
// ============================================================
static const char* VariantTypeName4x(int type) {
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
// Godot 3.x Variant::Type enum to string mapping (27 types)
// ============================================================
static const char* VariantTypeName3x(int type) {
    static const char* names[] = {
        "Nil",                  // 0
        "bool",                 // 1
        "int",                  // 2
        "real",                 // 3  (= float/double)
        "String",               // 4
        "Vector2",              // 5
        "Rect2",                // 6
        "Vector3",              // 7
        "Transform2D",          // 8
        "Plane",                // 9
        "Quat",                 // 10
        "AABB",                 // 11
        "Basis",                // 12
        "Transform",            // 13
        "Color",                // 14
        "NodePath",             // 15
        "RID",                  // 16
        "Object",               // 17
        "Dictionary",           // 18
        "Array",                // 19
        "PoolByteArray",        // 20
        "PoolIntArray",         // 21
        "PoolRealArray",        // 22
        "PoolStringArray",      // 23
        "PoolVector2Array",     // 24
        "PoolVector3Array",     // 25
        "PoolColorArray",       // 26
    };
    if (type >= 0 && type < static_cast<int>(std::size(names)))
        return names[type];
    return "Unknown";
}

// Version-aware type name lookup
static const char* VariantTypeName(int type) {
    if (GodotVer::IsGodot3())
        return VariantTypeName3x(type);
    return VariantTypeName4x(type);
}

// ============================================================
// Map Godot 3.x Variant::Type → 4.x equivalent for unified ReadVariant
// Returns the 4.x type number so ReadVariant only needs one switch.
// Returns -1 for invalid types.
// ============================================================
static int MapType3xTo4x(int type3) {
    static const int mapping[] = {
         0,  // 0:  NIL            → 0:  NIL
         1,  // 1:  BOOL           → 1:  BOOL
         2,  // 2:  INT            → 2:  INT
         3,  // 3:  REAL           → 3:  FLOAT
         4,  // 4:  STRING         → 4:  STRING
         5,  // 5:  VECTOR2        → 5:  VECTOR2
         7,  // 6:  RECT2          → 7:  RECT2
         9,  // 7:  VECTOR3        → 9:  VECTOR3
        11,  // 8:  TRANSFORM2D    → 11: TRANSFORM2D
        14,  // 9:  PLANE          → 14: PLANE
        15,  // 10: QUAT           → 15: QUATERNION
        16,  // 11: AABB           → 16: AABB
        17,  // 12: BASIS          → 17: BASIS
        18,  // 13: TRANSFORM      → 18: TRANSFORM3D
        20,  // 14: COLOR          → 20: COLOR
        22,  // 15: NODE_PATH      → 22: NODE_PATH
        23,  // 16: _RID           → 23: RID
        24,  // 17: OBJECT         → 24: OBJECT
        27,  // 18: DICTIONARY     → 27: DICTIONARY
        28,  // 19: ARRAY          → 28: ARRAY
        29,  // 20: POOL_BYTE_ARR  → 29: PACKED_BYTE_ARRAY
        30,  // 21: POOL_INT_ARR   → 30: PACKED_INT32_ARRAY
        32,  // 22: POOL_REAL_ARR  → 32: PACKED_FLOAT32_ARRAY
        34,  // 23: POOL_STR_ARR   → 34: PACKED_STRING_ARRAY
        35,  // 24: POOL_VEC2_ARR  → 35: PACKED_VECTOR2_ARRAY
        36,  // 25: POOL_VEC3_ARR  → 36: PACKED_VECTOR3_ARRAY
        37,  // 26: POOL_COL_ARR   → 37: PACKED_COLOR_ARRAY
    };
    if (type3 >= 0 && type3 < static_cast<int>(std::size(mapping)))
        return mapping[type3];
    return -1;
}

// ============================================================
// Variant data size table (based on 4.x type numbers, works for both)
// ============================================================
static int VariantDataSize(int type4x) {
    switch (type4x) {
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
// Uses 4.x type numbers internally via MapType3xTo4x for 3.x.
// ============================================================
VariantValue ReadVariant(uint64_t addr) {
    VariantValue result{};
    result.rawAddr = addr;

    if (!addr || !Zoltraak::IsReadable(addr)) return result;

    // Read Variant::Type (int32 at offset 0)
    int32_t rawType = Zoltraak::ReadSafe<int32_t>(addr + DynOff::VARIANT_TYPE, -1);
    int maxType = GodotVer::VariantTypeMax();
    if (rawType < 0 || rawType >= maxType) return result;

    // For display: use version-appropriate type name from the RAW type number
    result.typeName = VariantTypeName(rawType);

    // For decoding: map to 4.x type number so we use a single switch
    int type4x = rawType;
    if (GodotVer::IsGodot3()) {
        type4x = MapType3xTo4x(rawType);
        if (type4x < 0) return result;
    }

    result.type = rawType;  // Store the raw (version-specific) type number

    uint64_t data = addr + DynOff::VARIANT_DATA;

    switch (type4x) {
        case 0: // NIL
            result.value = "(null)";
            break;

        case 1: // BOOL
            result.value = Zoltraak::ReadSafe<uint32_t>(data, 0) ? "true" : "false";
            break;

        case 2: // INT (int64_t)
            result.value = std::to_string(Zoltraak::ReadSafe<int64_t>(data, 0));
            break;

        case 3: // FLOAT/REAL (double)
            result.value = FormatDouble(Zoltraak::ReadSafe<double>(data, 0.0));
            break;

        case 4: { // STRING (CowData<char32_t> in 4.x, CowData<wchar_t> in 3.x)
            uint64_t cowPtr = Zoltraak::ReadSafe<uint64_t>(data, 0);
            if (cowPtr && Zoltraak::IsReadable(cowPtr)) {
                // ReadGodotString handles CowData<char32_t> (4.x)
                result.value = Zoltraak::ReadGodotString(cowPtr);
                // If that fails and we're on 3.x, try wchar_t path
                if (result.value.empty() && GodotVer::IsGodot3()) {
                    result.value = Zoltraak::ReadChar32String(cowPtr, 256);
                }
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

        case 6: { // VECTOR2I (2 int32 inline) — 4.x only
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

        case 8: { // RECT2I (4 int32) — 4.x only
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

        case 10: { // VECTOR3I (3 int32) — 4.x only
            int32_t x = Zoltraak::ReadSafe<int32_t>(data, 0);
            int32_t y = Zoltraak::ReadSafe<int32_t>(data + 4, 0);
            int32_t z = Zoltraak::ReadSafe<int32_t>(data + 8, 0);
            result.value = "(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")";
            break;
        }

        case 12: { // VECTOR4 (4 floats inline) — 4.x only
            float x = Zoltraak::ReadSafe<float>(data, 0.0f);
            float y = Zoltraak::ReadSafe<float>(data + 4, 0.0f);
            float z = Zoltraak::ReadSafe<float>(data + 8, 0.0f);
            float w = Zoltraak::ReadSafe<float>(data + 12, 0.0f);
            result.value = "(" + FormatFloat(x) + ", " + FormatFloat(y) +
                          ", " + FormatFloat(z) + ", " + FormatFloat(w) + ")";
            break;
        }

        case 13: { // VECTOR4I (4 int32) — 4.x only
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

        case 15: { // QUATERNION/QUAT (4 floats)
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

        case 21: { // STRINGNAME (4.x only — 3.x doesn't have this as a Variant type)
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
            // For unhandled types: show type name + raw hex
            int dataSize = VariantDataSize(type4x);
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

    std::string className = DiAgolze::GetObjectClass(objAddr);
    if (className.empty()) {
        Solliter::Debug("WALK", "GetFieldValue: unknown class for %p", reinterpret_cast<void*>(objAddr));
        return {};
    }

    auto properties = DiAgolze::GetPropertyList(className.c_str());
    for (auto& prop : properties) {
        if (prop.name == propertyName) {
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
