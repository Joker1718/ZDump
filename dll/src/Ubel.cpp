// Ubel — 優貝爾 (Assassin Specialist — Variant/Property Reader)
// Reads Godot Variant values and walks object instance properties.
// Extracts field metadata from ClassDB and reads Variant values from memory.
//
// Version-aware Variant::Type handling:
//   Godot 3.x: 27 types (0-26), different enum numbering from type 6+
//   Godot 4.0-4.4: 38 types (0-37)
//   Godot 4.5+: 39 types (0-38, added PackedVector4Array)

#include "Ubel.h"
#include "Ausetiese.h"
#include "Zoltraak.h"
#include "Diagoldze.h"
#include "Flamme.h"
#include "Heiter.h"
#include "Solitaer.h"
#include "Grimoire.h"

#include <cstdio>
#include <iterator>
#include <unordered_map>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>

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
    result.ptrTarget = 0;

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
                // ReadGodotString auto-detects 3.x wchar_t vs 4.x char32_t
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
            if (objPtr && Zoltraak::IsReadable(objPtr)) {
                result.ptrTarget = objPtr;
            }
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
// ReadNativeValue — Read a native member value from object memory
// ============================================================
// Reads directly from object memory at the given offset, using the
// Variant type and native access size to determine how to interpret.
// This bypasses Variant boxing — reads the raw native member value.
static VariantValue ReadNativeValue(uint64_t memberAddr, int variantType, int nativeSize,
                                    int bitShift = 0, int bitMask = 0) {
    VariantValue result{};
    result.rawAddr = memberAddr;
    result.ptrTarget = 0;
    result.type    = variantType;
    result.typeName = VariantTypeName(variantType);

    if (!memberAddr || !Zoltraak::IsReadable(memberAddr)) return result;

    // Map 3.x type to 4.x for unified switch
    int type4x = variantType;
    if (GodotVer::IsGodot3()) {
        type4x = MapType3xTo4x(variantType);
        if (type4x < 0) return result;
    }

    switch (type4x) {
        case 0: // NIL
            result.value = "(null)";
            break;

        case 1: // BOOL
            if (bitMask) {
                uint8_t raw = Zoltraak::ReadSafe<uint8_t>(memberAddr, 0);
                result.value = ((raw >> bitShift) & bitMask) ? "true" : "false";
            } else {
                result.value = Zoltraak::ReadSafe<uint8_t>(memberAddr, 0) ? "true" : "false";
            }
            break;

        case 2: // INT
            if (bitMask) {
                // Bitfield: read byte, shift, mask
                uint8_t raw = Zoltraak::ReadSafe<uint8_t>(memberAddr, 0);
                result.value = std::to_string((raw >> bitShift) & bitMask);
            } else if (nativeSize <= 4) {
                result.value = std::to_string(Zoltraak::ReadSafe<int32_t>(memberAddr, 0));
            } else {
                result.value = std::to_string(Zoltraak::ReadSafe<int64_t>(memberAddr, 0));
            }
            break;

        case 3: // FLOAT
            if (nativeSize <= 4) {
                result.value = FormatFloat(Zoltraak::ReadSafe<float>(memberAddr, 0.0f));
            } else {
                result.value = FormatDouble(Zoltraak::ReadSafe<double>(memberAddr, 0.0));
            }
            break;

        case 4: { // STRING (CowData pointer — auto-detects 3.x wchar_t vs 4.x char32_t)
            uint64_t cowPtr = Zoltraak::ReadSafe<uint64_t>(memberAddr, 0);
            if (cowPtr && Zoltraak::IsReadable(cowPtr)) {
                result.value = Zoltraak::ReadGodotString(cowPtr);
            }
            if (result.value.empty()) result.value = "\"\"";
            break;
        }

        case 5: { // VECTOR2
            float x = Zoltraak::ReadSafe<float>(memberAddr, 0.0f);
            float y = Zoltraak::ReadSafe<float>(memberAddr + 4, 0.0f);
            result.value = "(" + FormatFloat(x) + ", " + FormatFloat(y) + ")";
            break;
        }

        case 6: { // VECTOR2I
            int32_t x = Zoltraak::ReadSafe<int32_t>(memberAddr, 0);
            int32_t y = Zoltraak::ReadSafe<int32_t>(memberAddr + 4, 0);
            result.value = "(" + std::to_string(x) + ", " + std::to_string(y) + ")";
            break;
        }

        case 9: { // VECTOR3
            float x = Zoltraak::ReadSafe<float>(memberAddr, 0.0f);
            float y = Zoltraak::ReadSafe<float>(memberAddr + 4, 0.0f);
            float z = Zoltraak::ReadSafe<float>(memberAddr + 8, 0.0f);
            result.value = "(" + FormatFloat(x) + ", " + FormatFloat(y) + ", " + FormatFloat(z) + ")";
            break;
        }

        case 10: { // VECTOR3I
            int32_t x = Zoltraak::ReadSafe<int32_t>(memberAddr, 0);
            int32_t y = Zoltraak::ReadSafe<int32_t>(memberAddr + 4, 0);
            int32_t z = Zoltraak::ReadSafe<int32_t>(memberAddr + 8, 0);
            result.value = "(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")";
            break;
        }

        case 20: { // COLOR (4 floats RGBA)
            float r = Zoltraak::ReadSafe<float>(memberAddr, 0.0f);
            float g = Zoltraak::ReadSafe<float>(memberAddr + 4, 0.0f);
            float b = Zoltraak::ReadSafe<float>(memberAddr + 8, 0.0f);
            float a = Zoltraak::ReadSafe<float>(memberAddr + 12, 0.0f);
            result.value = "Color(" + FormatFloat(r) + ", " + FormatFloat(g) +
                          ", " + FormatFloat(b) + ", " + FormatFloat(a) + ")";
            break;
        }

        case 21: { // STRINGNAME
            result.value = Flamme::Resolve(memberAddr);
            if (result.value.empty()) result.value = "(empty)";
            break;
        }

        case 22: { // NODEPATH (pointer to NodePath::Data)
            uint64_t pathPtr = Zoltraak::ReadSafe<uint64_t>(memberAddr, 0);
            char buf[32];
            snprintf(buf, sizeof(buf), "NodePath@%llX", static_cast<unsigned long long>(pathPtr));
            result.value = buf;
            break;
        }

        case 24: { // OBJECT (pointer)
            uint64_t objPtr = Zoltraak::ReadSafe<uint64_t>(memberAddr, 0);
            if (objPtr && Zoltraak::IsReadable(objPtr)) {
                result.ptrTarget = objPtr;
                char buf[32];
                snprintf(buf, sizeof(buf), "Object@%llX", static_cast<unsigned long long>(objPtr));
                result.value = buf;
            } else {
                result.value = "(null)";
            }
            break;
        }

        default: {
            // For unhandled types: show raw hex of native member
            int readSize = (nativeSize > 0) ? nativeSize : 8;
            if (readSize > 24) readSize = 24;

            uint8_t raw[24] = {};
            Zoltraak::ReadSafe(memberAddr, raw, readSize);

            std::string hex;
            hex.reserve(readSize * 2);
            for (int i = 0; i < readSize; i++) {
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
// Runtime ptrcall — invoke MethodBind::ptrcall on the game thread
// ============================================================
// Used as fallback when ExtractMemberAccess fails to disassemble the getter.
// ptrcall signature: void (MethodBind* self, Object* obj, const void** args, void* ret)

// Returns native return buffer size for ptrcall, or 0 if type is unsafe
static int PtrcallReturnSize(int type4x) {
    switch (type4x) {
        case 0:  return 0;     // Nil — skip
        case 1:  return 8;     // bool (1B but padded)
        case 2:  return 8;     // int64_t
        case 3:  return 8;     // double
        case 4:  return 8;     // String (CowData ptr)
        case 5:  return 8;     // Vector2 (2x float)
        case 6:  return 8;     // Vector2i (2x int32)
        case 7:  return 16;    // Rect2 (4x float)
        case 8:  return 16;    // Rect2i (4x int32)
        case 9:  return 16;    // Vector3 (3x float, padded)
        case 10: return 16;    // Vector3i (3x int32, padded)
        case 11: return 24;    // Transform2D (6 floats)
        case 12: return 16;    // Vector4 (4x float)
        case 13: return 16;    // Vector4i (4x int32)
        case 14: return 16;    // Plane (4x float)
        case 15: return 16;    // Quaternion (4x float)
        case 16: return 24;    // AABB (6x float)
        case 17: return 48;    // Basis (9x float, padded)
        case 18: return 64;    // Transform3D (Basis + Origin)
        case 19: return 64;    // Projection (4x Vector4)
        case 20: return 16;    // Color (4x float)
        case 21: return 8;     // StringName (ptr)
        case 22: return 8;     // NodePath (ptr)
        case 23: return 8;     // RID (uint64_t)
        case 24: return 8;     // Object (ptr)
        default: return 0;     // Callable, Signal, Dict, Array, Packed* — unsafe
    }
}

// Decode a ptrcall return buffer into a VariantValue
static VariantValue DecodeReturnBuffer(const uint8_t* buf, int variantType, int type4x) {
    VariantValue result{};
    result.type     = variantType;
    result.typeName = VariantTypeName(variantType);
    result.rawAddr  = 0;
    result.ptrTarget = 0;

    switch (type4x) {
        case 1: // bool
            result.value = (*buf) ? "true" : "false";
            break;
        case 2: { // int64
            int64_t v;
            memcpy(&v, buf, 8);
            result.value = std::to_string(v);
            break;
        }
        case 3: { // double
            double v;
            memcpy(&v, buf, 8);
            result.value = FormatDouble(v);
            break;
        }
        case 4: { // String — ptrcall writes a String object inline
            // String is { CowData<char32_t>* _cowdata; } = 8 bytes
            uint64_t cowPtr;
            memcpy(&cowPtr, buf, 8);
            if (cowPtr && Zoltraak::IsReadable(cowPtr)) {
                result.value = Zoltraak::ReadGodotString(cowPtr);
            }
            if (result.value.empty()) result.value = "\"\"";
            break;
        }
        case 5: { // Vector2
            float f[2];
            memcpy(f, buf, 8);
            result.value = "(" + FormatFloat(f[0]) + ", " + FormatFloat(f[1]) + ")";
            break;
        }
        case 6: { // Vector2i
            int32_t i[2];
            memcpy(i, buf, 8);
            result.value = "(" + std::to_string(i[0]) + ", " + std::to_string(i[1]) + ")";
            break;
        }
        case 7: { // Rect2
            float f[4];
            memcpy(f, buf, 16);
            result.value = "(" + FormatFloat(f[0]) + ", " + FormatFloat(f[1]) +
                          ", " + FormatFloat(f[2]) + ", " + FormatFloat(f[3]) + ")";
            break;
        }
        case 8: { // Rect2i
            int32_t i[4];
            memcpy(i, buf, 16);
            result.value = "(" + std::to_string(i[0]) + ", " + std::to_string(i[1]) +
                          ", " + std::to_string(i[2]) + ", " + std::to_string(i[3]) + ")";
            break;
        }
        case 9: { // Vector3
            float f[3];
            memcpy(f, buf, 12);
            result.value = "(" + FormatFloat(f[0]) + ", " + FormatFloat(f[1]) + ", " + FormatFloat(f[2]) + ")";
            break;
        }
        case 10: { // Vector3i
            int32_t i[3];
            memcpy(i, buf, 12);
            result.value = "(" + std::to_string(i[0]) + ", " + std::to_string(i[1]) + ", " + std::to_string(i[2]) + ")";
            break;
        }
        case 12: { // Vector4
            float f[4];
            memcpy(f, buf, 16);
            result.value = "(" + FormatFloat(f[0]) + ", " + FormatFloat(f[1]) +
                          ", " + FormatFloat(f[2]) + ", " + FormatFloat(f[3]) + ")";
            break;
        }
        case 13: { // Vector4i
            int32_t i[4];
            memcpy(i, buf, 16);
            result.value = "(" + std::to_string(i[0]) + ", " + std::to_string(i[1]) +
                          ", " + std::to_string(i[2]) + ", " + std::to_string(i[3]) + ")";
            break;
        }
        case 14: { // Plane
            float f[4];
            memcpy(f, buf, 16);
            result.value = "Plane(" + FormatFloat(f[0]) + ", " + FormatFloat(f[1]) +
                          ", " + FormatFloat(f[2]) + ", " + FormatFloat(f[3]) + ")";
            break;
        }
        case 15: { // Quaternion
            float f[4];
            memcpy(f, buf, 16);
            result.value = "Quat(" + FormatFloat(f[0]) + ", " + FormatFloat(f[1]) +
                          ", " + FormatFloat(f[2]) + ", " + FormatFloat(f[3]) + ")";
            break;
        }
        case 20: { // Color
            float f[4];
            memcpy(f, buf, 16);
            result.value = "Color(" + FormatFloat(f[0]) + ", " + FormatFloat(f[1]) +
                          ", " + FormatFloat(f[2]) + ", " + FormatFloat(f[3]) + ")";
            break;
        }
        case 21: { // StringName
            uint64_t snPtr;
            memcpy(&snPtr, buf, 8);
            if (snPtr) {
                // ptrcall writes a StringName inline; resolve its _Data*
                result.value = Flamme::ResolveAt(snPtr);
            }
            if (result.value.empty()) result.value = "(empty)";
            break;
        }
        case 23: { // RID
            uint64_t rid;
            memcpy(&rid, buf, 8);
            char b[32];
            snprintf(b, sizeof(b), "RID(%llu)", static_cast<unsigned long long>(rid));
            result.value = b;
            break;
        }
        case 24: { // Object pointer
            uint64_t objPtr;
            memcpy(&objPtr, buf, 8);
            if (objPtr && Zoltraak::IsReadable(objPtr)) {
                result.ptrTarget = objPtr;
                char b[32];
                snprintf(b, sizeof(b), "Object@%llX", static_cast<unsigned long long>(objPtr));
                result.value = b;
            } else {
                result.value = "(null)";
            }
            break;
        }
        default: {
            // Hex dump for unhandled value types
            int retSize = PtrcallReturnSize(type4x);
            if (retSize <= 0) retSize = 8;
            if (retSize > 24) retSize = 24;
            std::string hex;
            hex.reserve(retSize * 2);
            for (int i = 0; i < retSize; i++) {
                char h[3];
                snprintf(h, sizeof(h), "%02X", buf[i]);
                hex += h;
            }
            result.value = result.typeName + std::string("{") + hex + "}";
            break;
        }
    }
    return result;
}

// Invoke MethodBind::ptrcall on game thread to read a property value
// methodBindAddr: MethodBind* (from PropertySetGet._getptr)
// objAddr: Object* to read from
// variantType: raw Variant::Type for result interpretation
static VariantValue CallGetterPtrcall(uint64_t methodBindAddr, uint64_t objAddr, int variantType) {
    VariantValue result{};
    if (!methodBindAddr || !objAddr) return result;

    // Map to 4.x type for unified handling
    int type4x = variantType;
    if (GodotVer::IsGodot3()) {
        type4x = MapType3xTo4x(variantType);
        if (type4x < 0) return result;
    }

    int retSize = PtrcallReturnSize(type4x);
    if (retSize <= 0) return result;  // Unsafe type, skip

    // Resolve ptrcall function pointer from MethodBind vtable
    uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(methodBindAddr, 0);
    if (!vtable || !Zoltraak::IsReadable(vtable)) return result;

    int ptrcallIdx = DynOff::METHODBIND_PTRCALL_VTIDX;
    uint64_t ptrcallFn = Zoltraak::ReadSafe<uint64_t>(vtable + ptrcallIdx * 8, 0);
    if (!ptrcallFn || !Zoltraak::IsReadable(ptrcallFn)) return result;

    // ptrcall signature: void (__fastcall*)(MethodBind* self, Object* obj, const void** args, void* ret)
    using PtrcallFn = void(__fastcall*)(void*, void*, const void**, void*);
    auto fnPtr = reinterpret_cast<PtrcallFn>(ptrcallFn);

    // Prepare return buffer (aligned, zero-initialized)
    alignas(16) uint8_t retBuf[128] = {};
    bool success = false;

    // For String/StringName types, decode inside the game thread lambda
    // to avoid lifetime issues with heap-allocated return values
    std::string stringResult;
    bool isStringType = (type4x == 4 || type4x == 21 || type4x == 22);

    auto work = [&]() {
        fnPtr(reinterpret_cast<void*>(methodBindAddr),
              reinterpret_cast<void*>(objAddr),
              nullptr,   // args = null for getter
              retBuf);
        success = true;

        // Decode string types immediately on game thread (before String is freed)
        if (isStringType && success) {
            VariantValue tmp = DecodeReturnBuffer(retBuf, variantType, type4x);
            stringResult = std::move(tmp.value);
        }
    };

    bool dispatched = Heiter::QueueWorkSync(work, Zolt::Heiter::SYNC_TIMEOUT_MS);

    if (!dispatched || !success) {
        return result;
    }

    // For string types, use the value decoded on game thread
    if (isStringType) {
        result.type     = variantType;
        result.typeName = VariantTypeName(variantType);
        result.value    = std::move(stringResult);
        result.rawAddr  = 0;
        return result;
    }

    // For value types, decode from the buffer (safe — data is on our stack)
    return DecodeReturnBuffer(retBuf, variantType, type4x);
}

// ============================================================
// GDScript member reading — reads script-defined variables
// ============================================================
// GDScript-defined variables (var money = 100, var hp = 50) are stored
// in GDScriptInstance::members (Vector<Variant>), NOT in ClassDB.
// We probe the ScriptInstance offset from Object*, then read the Variants.

// Module base/size for vtable validation (cached from init)
static uint64_t s_moduleBase = 0;
static uint64_t s_moduleSize = 0;

static void EnsureModuleInfo() {
    if (s_moduleBase) return;
    HMODULE hModule = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&EnsureModuleInfo), &hModule);
    if (!hModule) return;

    MODULEINFO mi{};
    if (GetModuleInformation(GetCurrentProcess(), hModule, &mi, sizeof(mi))) {
        s_moduleBase = reinterpret_cast<uint64_t>(mi.lpBaseOfDll);
        s_moduleSize = mi.SizeOfImage;
    }
}

// Probe the offset of ScriptInstance* within Object.
// Strategy: scan Object memory for a pointer whose target has
// a backlink (owner) pointing back to the Object.
// Allows retry on up to MAX_PROBE_ATTEMPTS different objects, since not all objects
// have GDScript attached (e.g. Engine, InputMap are engine singletons without scripts).
static std::atomic<int> s_probeAttempts{0};
static constexpr int MAX_PROBE_ATTEMPTS = 60;

static void ProbeScriptInstanceOffset(uint64_t objAddr) {
    if (DynOff::bScriptInstanceProbed.load()) return; // Already found

    int attempt = s_probeAttempts.fetch_add(1);
    if (attempt >= MAX_PROBE_ATTEMPTS) return; // Give up after N attempts

    EnsureModuleInfo();
    Solitaer::Info("WALK", "=== Probing ScriptInstance offset (attempt %d/%d) ===",
        attempt + 1, MAX_PROBE_ATTEMPTS);

    // Scan from +0x20 to +0x500 (ScriptInstance* is after the fixed Object header)
    // Godot 4.5+ may have moved script_instance further due to Object refactoring
    for (int off = 0x20; off <= 0x500; off += 8) {
        uint64_t candidate = Zoltraak::ReadSafe<uint64_t>(objAddr + off, 0);
        if (!candidate || !Zoltraak::IsReadable(candidate)) continue;

        // Validate: candidate should have a vtable in .text/.rdata section
        uint64_t candidateVtable = Zoltraak::ReadSafe<uint64_t>(candidate, 0);
        if (!candidateVtable || !Zoltraak::IsReadable(candidateVtable)) continue;

        // Check if vtable is in main module (game executable)
        // Skip if it's a common value or null-ish
        if (candidateVtable < 0x10000) continue;

        // Key validation: GDScriptInstance has Object* owner at +0x10
        // This should point back to our Object
        uint64_t owner = Zoltraak::ReadSafe<uint64_t>(candidate + DynOff::GDSCRIPT_OWNER_OFF, 0);
        if (owner != objAddr) continue;

        // Additional check: owner_id at +0x08 should be non-zero
        uint64_t ownerId = Zoltraak::ReadSafe<uint64_t>(candidate + 0x08, 0);
        if (ownerId == 0) continue;

        // Check that script pointer at +0x18 is also valid
        uint64_t scriptPtr = Zoltraak::ReadSafe<uint64_t>(candidate + DynOff::GDSCRIPT_SCRIPT_OFF, 0);
        if (!scriptPtr || !Zoltraak::IsReadable(scriptPtr)) continue;

        // Found it!
        DynOff::OBJECT_SCRIPT_INSTANCE = off;
        DynOff::bScriptInstanceProbed.store(true); // Mark as found — no more retries
        Solitaer::Info("WALK", "  ScriptInstance offset = +0x%X (inst=%p, script=%p)",
            off, reinterpret_cast<void*>(candidate), reinterpret_cast<void*>(scriptPtr));

        // Hexdump first 0x50 bytes of GDScriptInstance for layout diagnosis
        Solitaer::Info("WALK", "  GDScriptInstance hexdump (first 0x50 bytes at %p):",
            reinterpret_cast<void*>(candidate));
        for (int doff = 0; doff < 0x50; doff += 8) {
            uint64_t val = Zoltraak::ReadSafe<uint64_t>(candidate + doff, 0);
            Solitaer::Info("WALK", "    +0x%02X: %016llX%s",
                doff, val,
                (doff == 0x00) ? "  <- vtable" :
                (doff == 0x08) ? "  <- owner_id" :
                (doff == 0x10) ? "  <- owner" :
                (doff == 0x18) ? "  <- script (Ref)" : "");
        }

        // Probe members Vector offset dynamically:
        // Scan from +0x20 to +0x48 for a valid CowData pointer (aligned, readable, sensible count)
        int probedMembersOff = -1;
        for (int moff = 0x20; moff <= 0x48; moff += 8) {
            uint64_t mptr = Zoltraak::ReadSafe<uint64_t>(candidate + moff, 0);
            if (!mptr || !Zoltraak::IsReadable(mptr)) continue;
            // CowData._ptr must be reasonably aligned (at least 8-byte for Variant data)
            if (mptr % 8 != 0) {
                Solitaer::Debug("WALK", "  +0x%02X: ptr=%p SKIP (not 8-byte aligned)", moff,
                    reinterpret_cast<void*>(mptr));
                continue;
            }
            // Read size from CowData header
            int sizeOff = GodotVer::IsGodot3() ? 4 : 8;
            uint32_t cnt = Zoltraak::ReadSafe<uint32_t>(mptr - sizeOff, 0);
            if (cnt == 0 || cnt > 1000) continue;
            // Validate: first Variant::Type should be in range [0, VariantTypeMax)
            int32_t firstType = Zoltraak::ReadSafe<int32_t>(mptr, -1);
            if (firstType < 0 || firstType >= GodotVer::VariantTypeMax()) continue;

            probedMembersOff = moff;
            Solitaer::Info("WALK", "  Members Vector probed at +0x%02X: ptr=%p count=%u firstType=%d",
                moff, reinterpret_cast<void*>(mptr), cnt, firstType);
            break;
        }

        if (probedMembersOff >= 0 && probedMembersOff != DynOff::GDSCRIPT_MEMBERS_OFF) {
            Solitaer::Info("WALK", "  Updating GDSCRIPT_MEMBERS_OFF: 0x%X -> 0x%X",
                DynOff::GDSCRIPT_MEMBERS_OFF, probedMembersOff);
            DynOff::GDSCRIPT_MEMBERS_OFF = probedMembersOff;
        } else if (probedMembersOff < 0) {
            // Fallback: log all readable pointers for manual analysis
            Solitaer::Warn("WALK", "  Could not probe members Vector offset (all candidates failed)");
            uint64_t defaultPtr = Zoltraak::ReadSafe<uint64_t>(
                candidate + DynOff::GDSCRIPT_MEMBERS_OFF, 0);
            Solitaer::Warn("WALK", "  Default +0x%X value: %p (aligned=%s)",
                DynOff::GDSCRIPT_MEMBERS_OFF,
                reinterpret_cast<void*>(defaultPtr),
                (defaultPtr % 8 == 0) ? "yes" : "NO");
        }

        Solitaer::Info("WALK", "=== ScriptInstance probe complete ===");
        return;
    }

    // Not found on this object — DON'T set bScriptInstanceProbed, allow retry on next object
    Solitaer::Info("WALK", "  No ScriptInstance found on this object (attempt %d/%d)",
        attempt + 1, MAX_PROBE_ATTEMPTS);
    Solitaer::Info("WALK", "=== ScriptInstance probe complete ===");
}

// Read member names from GDScript::member_indices HashMap
// Returns name→index map. Fails gracefully with empty map.
static std::unordered_map<int, std::string> ReadGDScriptMemberNames(uint64_t gdscriptAddr) {
    std::unordered_map<int, std::string> indexToName;
    if (!gdscriptAddr || !Zoltraak::IsReadable(gdscriptAddr)) return indexToName;

    // GDScript inherits from Script → Resource → RefCounted → Object
    // member_indices is a HashMap<StringName, MemberInfo> somewhere in GDScript
    // MemberInfo starts with int index (4B + 4B pad)
    //
    // Strategy: scan GDScript memory for a robin-hood HashMap (4.x) or bucket-chain (3.x)
    // where keys resolve to StringNames and values start with small ints (indices 0-N)

    if (GodotVer::IsGodot3()) {
        // 3.x: bucket-chain HashMap (16B inline)
        for (int off = 0x40; off <= 0x600; off += 8) {
            uint64_t ht    = Zoltraak::ReadSafe<uint64_t>(gdscriptAddr + off, 0);
            uint8_t  pw    = Zoltraak::ReadSafe<uint8_t>(gdscriptAddr + off + 0x08, 0);
            uint32_t count = Zoltraak::ReadSafe<uint32_t>(gdscriptAddr + off + 0x0C, 0);
            if (!ht || !Zoltraak::IsReadable(ht)) continue;
            if (pw < 1 || pw > 12 || count < 1 || count > 200) continue;

            // Validate: first non-null bucket element key resolves to a StringName,
            // and value starts with a small int (index)
            int buckets = 1 << pw;
            bool valid = false;
            for (int b = 0; b < buckets && !valid; ++b) {
                uint64_t elem = Zoltraak::ReadSafe<uint64_t>(ht + b * 8, 0);
                if (!elem || !Zoltraak::IsReadable(elem)) continue;

                std::string name = Flamme::Resolve(elem + DynOff::ELEM_3X_KEY_OFF);
                if (name.empty() || name.size() > 64) continue;

                int32_t idx = Zoltraak::ReadSafe<int32_t>(elem + DynOff::ELEM_3X_VALUE_OFF, -1);
                if (idx < 0 || idx >= static_cast<int>(count)) continue;

                valid = true;
            }
            if (!valid) continue;

            // Read all entries
            for (int b = 0; b < buckets; ++b) {
                uint64_t elem = Zoltraak::ReadSafe<uint64_t>(ht + b * 8, 0);
                while (elem && Zoltraak::IsReadable(elem)) {
                    std::string name = Flamme::Resolve(elem + DynOff::ELEM_3X_KEY_OFF);
                    int32_t idx = Zoltraak::ReadSafe<int32_t>(elem + DynOff::ELEM_3X_VALUE_OFF, -1);
                    if (!name.empty() && idx >= 0 && idx < 200) {
                        indexToName[idx] = name;
                    }
                    uint64_t next = Zoltraak::ReadSafe<uint64_t>(elem + DynOff::ELEM_3X_NEXT_OFF, 0);
                    if (next == elem) break;
                    elem = next;
                }
            }
            if (!indexToName.empty()) {
                Solitaer::Info("WALK", "  GDScript member_indices: %d names at +0x%X",
                    static_cast<int>(indexToName.size()), off);
                break;
            }
        }
    } else {
        // 4.x: robin-hood HashMap with DefaultTypedAllocator (48B inline)
        // HashMap layout (MSVC x64, DefaultTypedAllocator = empty class, 1B + 7B pad):
        //   +0x00: element_alloc  (8B with padding)
        //   +0x08: elements**     (8B)
        //   +0x10: hashes*        (8B)
        //   +0x18: head_element*  (8B)  — linked list head
        //   +0x20: tail_element*  (8B)
        //   +0x28: capacity_index (4B)
        //   +0x2C: num_elements   (4B)
        // Total: 0x30 = 48 bytes
        //
        // Element layout:
        //   +0x00: next*          (8B)
        //   +0x08: prev*          (8B)
        //   +0x10: KeyValue { key: StringName(8B), value: MemberInfo { int index(4B), ... } }
        constexpr int HEAD_OFF = 0x18;  // head_element offset within HashMap
        constexpr int NUM_OFF  = 0x2C;  // num_elements offset within HashMap
        constexpr int ELEM_KEY_OFF   = 0x10;  // StringName key in Element
        constexpr int ELEM_VALUE_OFF = 0x18;  // MemberInfo.index in Element

        for (int off = 0x40; off <= 0x800; off += 8) {
            uint64_t head = Zoltraak::ReadSafe<uint64_t>(gdscriptAddr + off + HEAD_OFF, 0);
            uint32_t num  = Zoltraak::ReadSafe<uint32_t>(gdscriptAddr + off + NUM_OFF, 0);
            if (!head || num < 1 || num > 500) continue;
            if (!Zoltraak::IsReadable(head)) continue;

            // Validate: key resolves to a StringName, value.index is valid
            std::string firstName = Flamme::Resolve(head + ELEM_KEY_OFF);
            if (firstName.empty() || firstName.size() > 64) continue;

            int32_t firstIdx = Zoltraak::ReadSafe<int32_t>(head + ELEM_VALUE_OFF, -1);
            if (firstIdx < 0 || firstIdx >= static_cast<int>(num)) continue;

            // Read all entries by walking the linked list
            uint64_t cur = head;
            int walked = 0;
            while (cur && walked < static_cast<int>(num) + 5) {
                if (!Zoltraak::IsReadable(cur)) break;
                std::string name = Flamme::Resolve(cur + ELEM_KEY_OFF);
                int32_t idx = Zoltraak::ReadSafe<int32_t>(cur + ELEM_VALUE_OFF, -1);
                if (!name.empty() && idx >= 0 && idx < 500) {
                    indexToName[idx] = name;
                }
                uint64_t next = Zoltraak::ReadSafe<uint64_t>(cur, 0);
                if (next == cur) break;
                cur = next;
                walked++;
            }
            if (!indexToName.empty()) {
                Solitaer::Info("WALK", "  GDScript member_indices: %d names at GDScript+0x%X (head=%p)",
                    static_cast<int>(indexToName.size()), off,
                    reinterpret_cast<void*>(head));
                break;
            }
        }
    }

    return indexToName;
}

// Auto-detect Variant stride by probing the first few members of a Vector<Variant>.
// Checks candidate strides (0x18 for float, 0x28 for double) and picks the one
// where the most type fields read as valid Godot Variant types.
static void ProbeVariantStride(uint64_t membersPtr, uint32_t count) {
    static bool sProbed = false;
    if (sProbed) return;
    sProbed = true;

    const int candidates[] = { 0x18, 0x28 };  // float=24, double=40
    int maxType = GodotVer::VariantTypeMax();
    int probeCount = static_cast<int>((count < 20) ? count : 20);
    int bestStride = DynOff::VARIANT_STRIDE;
    int bestValid  = -1;

    for (int stride : candidates) {
        int validCount = 0;
        for (int i = 0; i < probeCount; i++) {
            int32_t t = Zoltraak::ReadSafe<int32_t>(membersPtr + i * stride, -1);
            if (t >= 0 && t < maxType) validCount++;
        }
        Solitaer::Debug("WALK", "ProbeVariantStride: stride=0x%X → %d/%d valid types",
            stride, validCount, probeCount);
        if (validCount > bestValid) {
            bestValid  = validCount;
            bestStride = stride;
        }
    }

    if (bestStride != DynOff::VARIANT_STRIDE) {
        Solitaer::Info("WALK", "ProbeVariantStride: adjusting VARIANT_STRIDE from 0x%X to 0x%X (%d/%d valid)",
            DynOff::VARIANT_STRIDE, bestStride, bestValid, probeCount);
        DynOff::VARIANT_STRIDE = bestStride;
    } else {
        Solitaer::Info("WALK", "ProbeVariantStride: confirmed stride 0x%X (%d/%d valid)",
            bestStride, bestValid, probeCount);
    }
}

// Read GDScript members from an Object's ScriptInstance
// Appends FieldEntry items to the callback for each script member.
static void ReadGDScriptMembers(uint64_t objAddr,
                                 const std::function<void(const FieldEntry& entry)>& callback) {
    if (DynOff::OBJECT_SCRIPT_INSTANCE < 0) {
        // Offset not yet probed — no log needed (normal before probe succeeds)
        return;
    }

    uint64_t scriptInst = Zoltraak::ReadSafe<uint64_t>(
        objAddr + DynOff::OBJECT_SCRIPT_INSTANCE, 0);
    if (!scriptInst) {
        // Object has no ScriptInstance (no GDScript attached) — expected for engine nodes
        return;
    }
    if (!Zoltraak::IsReadable(scriptInst)) {
        Solitaer::Debug("WALK", "ReadGDScriptMembers: ScriptInstance ptr %p not readable for obj %p",
            reinterpret_cast<void*>(scriptInst), reinterpret_cast<void*>(objAddr));
        return;
    }

    // Verify owner backlink
    uint64_t owner = Zoltraak::ReadSafe<uint64_t>(scriptInst + DynOff::GDSCRIPT_OWNER_OFF, 0);
    if (owner != objAddr) {
        Solitaer::Debug("WALK", "ReadGDScriptMembers: owner mismatch at %p (owner=%p, expected=%p) — not a GDScriptInstance",
            reinterpret_cast<void*>(scriptInst), reinterpret_cast<void*>(owner), reinterpret_cast<void*>(objAddr));
        return;
    }

    // Read GDScript pointer for name lookup
    uint64_t gdscriptPtr = Zoltraak::ReadSafe<uint64_t>(
        scriptInst + DynOff::GDSCRIPT_SCRIPT_OFF, 0);

    // Read members Vector<Variant> (CowData pointer)
    uint64_t membersPtr = Zoltraak::ReadSafe<uint64_t>(
        scriptInst + DynOff::GDSCRIPT_MEMBERS_OFF, 0);
    if (!membersPtr) {
        Solitaer::Info("WALK", "ReadGDScriptMembers: obj %p has GDScriptInstance at %p but members Vector is empty (ptr=NULL)",
            reinterpret_cast<void*>(objAddr), reinterpret_cast<void*>(scriptInst));
        return;
    }
    if (!Zoltraak::IsReadable(membersPtr)) {
        Solitaer::Warn("WALK", "ReadGDScriptMembers: members ptr %p not readable (scriptInst=%p)",
            reinterpret_cast<void*>(membersPtr), reinterpret_cast<void*>(scriptInst));
        return;
    }

    // Read count from CowData header (before data pointer)
    // Godot 4.x: USize (uint64_t) at ptr-8; Godot 3.x: uint32_t at ptr-4
    int sizeOff = GodotVer::IsGodot3() ? 4 : 8;
    uint32_t count = Zoltraak::ReadSafe<uint32_t>(membersPtr - sizeOff, 0);
    if (count == 0) {
        Solitaer::Debug("WALK", "ReadGDScriptMembers: members count=0 for obj %p (CowData at %p, size at %p)",
            reinterpret_cast<void*>(objAddr), reinterpret_cast<void*>(membersPtr),
            reinterpret_cast<void*>(membersPtr - sizeOff));
        return;
    }
    if (count > 500) {
        Solitaer::Warn("WALK", "ReadGDScriptMembers: members count=%u exceeds sanity limit (CowData at %p)",
            count, reinterpret_cast<void*>(membersPtr));
        return;
    }

    // Auto-detect Variant stride on first encounter
    ProbeVariantStride(membersPtr, count);

    // Try to get member names from GDScript object
    auto nameMap = ReadGDScriptMemberNames(gdscriptPtr);

    Solitaer::Info("WALK", "GDScript members: %u variables, %d names resolved (membersPtr=%p, stride=0x%X)",
        count, static_cast<int>(nameMap.size()),
        reinterpret_cast<void*>(membersPtr), DynOff::VARIANT_STRIDE);
    for (auto& kv : nameMap) {
        Solitaer::Debug("WALK", "  member[%d] = '%s'", kv.first, kv.second.c_str());
    }

    int variantStride = DynOff::VARIANT_STRIDE;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t varAddr = membersPtr + i * variantStride;

        // Read raw type BEFORE ReadVariant for diagnostics
        int32_t rawType = Zoltraak::ReadSafe<int32_t>(varAddr + DynOff::VARIANT_TYPE, -1);

        VariantValue val = ReadVariant(varAddr);

        // Diagnostic: log entries ReadVariant rejected (rawType out of range)
        if (val.value.empty() && val.typeName.empty()) {
            uint64_t w0 = Zoltraak::ReadSafe<uint64_t>(varAddr + 0x00, 0);
            uint64_t w1 = Zoltraak::ReadSafe<uint64_t>(varAddr + 0x08, 0);
            uint64_t w2 = Zoltraak::ReadSafe<uint64_t>(varAddr + 0x10, 0);
            Solitaer::Info("WALK", "  member[%u] rawType=%d (rejected): "
                "+0x00=%016llX +0x08=%016llX +0x10=%016llX (at %p, stride=0x%X)",
                i, rawType, w0, w1, w2, reinterpret_cast<void*>(varAddr), variantStride);
        } else if (val.type == 0 && val.value == "(null)") {
            // NIL type — check if data area has non-zero residual
            uint64_t d0 = Zoltraak::ReadSafe<uint64_t>(varAddr + 0x08, 0);
            uint64_t d1 = Zoltraak::ReadSafe<uint64_t>(varAddr + 0x10, 0);
            if (d0 || d1) {
                Solitaer::Info("WALK", "  member[%u] type=NIL but data non-zero: "
                    "+0x08=%016llX +0x10=%016llX (at %p)",
                    i, d0, d1, reinterpret_cast<void*>(varAddr));
            }
        }

        FieldEntry entry;
        auto nameIt = nameMap.find(static_cast<int>(i));
        entry.name = (nameIt != nameMap.end()) ? nameIt->second
                                                : "script_var_" + std::to_string(i);
        entry.className = "(GDScript)";
        entry.value     = val;
        // Show data address (Variant + VARIANT_DATA) — this matches what CE finds
        entry.value.rawAddr = varAddr + DynOff::VARIANT_DATA;
        entry.offset    = -1;  // Not a fixed offset from Object base (heap allocated)

        callback(entry);
    }
}

// SEH-safe ptrcall invocation (separate function to avoid C2712 with C++ objects)
using PtrcallFn = void(__fastcall*)(void*, void*, const void**, void*);
static bool SafePtrcallInvoke(PtrcallFn fn, uint64_t methodBind, uint64_t objAddr, uint8_t* retBuf) {
    __try {
        fn(reinterpret_cast<void*>(methodBind),
           reinterpret_cast<void*>(objAddr),
           nullptr, retBuf);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ============================================================
// WalkInstance — Walk all properties of an object instance
// ============================================================
void WalkInstance(uint64_t objAddr,
                  const std::function<void(const FieldEntry& entry)>& callback) {
    if (!objAddr || !callback) return;

    // Step 1: Determine object class via RTTI
    std::string className = Diagoldze::GetObjectClass(objAddr);
    if (className.empty()) {
        Solitaer::Debug("WALK", "WalkInstance: could not determine class for object at %p",
            reinterpret_cast<void*>(objAddr));
        return;
    }

    // Step 2: Get property list from ClassDB (includes inherited properties)
    auto properties = Diagoldze::GetPropertyList(className.c_str());
    if (properties.empty()) {
        Solitaer::Debug("WALK", "WalkInstance: no ClassDB properties for class '%s', trying GDScript members",
            className.c_str());
        // Don't return — still try GDScript members below
        ProbeScriptInstanceOffset(objAddr);
        ReadGDScriptMembers(objAddr, callback);
        return;
    }

    Solitaer::Debug("WALK", "WalkInstance: %s @ %p — %d properties",
        className.c_str(), reinterpret_cast<void*>(objAddr),
        static_cast<int>(properties.size()));

    // Log property summary for diagnostics
    int withOffset = 0, withGetter = 0, metadataOnly = 0;
    for (auto& p : properties) {
        if (p.offset >= 0) withOffset++;
        else if (p.getterMethodBind != 0) withGetter++;
        else metadataOnly++;
    }
    Solitaer::Debug("WALK", "  Property breakdown: %d with offset, %d with getter, %d metadata-only",
        withOffset, withGetter, metadataOnly);

    // Step 3: Build FieldEntry for each property, reading values where offsets are known
    int valuesRead = 0;
    int directReads = 0;
    int ptrcallReads = 0;

    // Batch ptrcall: collect properties needing ptrcall, execute all in one game-thread dispatch
    struct PtrcallJob {
        size_t propIndex;
        uint64_t methodBind;
        int variantType;
    };
    std::vector<PtrcallJob> ptrcallJobs;
    std::vector<FieldEntry> entries;
    entries.reserve(properties.size());

    for (size_t i = 0; i < properties.size(); i++) {
        auto& prop = properties[i];
        FieldEntry entry;
        entry.name       = prop.name;
        entry.className  = prop.className;
        entry.offset     = prop.offset;
        entry.nativeSize = prop.nativeSize;
        entry.bitShift   = prop.bitShift;
        entry.bitMask    = prop.bitMask;
        entry.hint       = prop.hint;
        entry.hintString = prop.hintString;
        entry.usage      = prop.usage;

        if (prop.offset >= 0) {
            // Fast path: direct memory read at known offset
            uint64_t memberAddr = objAddr + prop.offset;
            entry.value = ReadNativeValue(memberAddr, prop.type, prop.nativeSize, prop.bitShift, prop.bitMask);
            valuesRead++;
            directReads++;
        } else if (prop.getterMethodBind != 0) {
            // Queue for batched ptrcall
            ptrcallJobs.push_back({ i, prop.getterMethodBind, prop.type });
            entry.value.type     = prop.type;
            entry.value.typeName = VariantTypeName(prop.type);
            entry.value.value    = "";
            entry.value.rawAddr  = 0;
        } else {
            // No offset, no getter — metadata only
            entry.value.type     = prop.type;
            entry.value.typeName = VariantTypeName(prop.type);
            entry.value.value    = "";
            entry.value.rawAddr  = 0;
        }

        entries.push_back(std::move(entry));
    }

    Solitaer::Debug("WALK", "WalkInstance: for-loop done. %d direct, %d ptrcall queued, %d metadata",
        directReads, static_cast<int>(ptrcallJobs.size()),
        static_cast<int>(entries.size()) - directReads - static_cast<int>(ptrcallJobs.size()));

    // Execute batched ptrcall on game thread (single QueueWorkSync for all)
    // Skip entirely if ptrcall is auto-disabled (all timeouts → direct reads only)
    if (!ptrcallJobs.empty() && !Heiter::IsPtrcallDisabled()) {
        struct PtrcallResult {
            VariantValue value;
            bool ok = false;
        };
        std::vector<PtrcallResult> ptrcallResults(ptrcallJobs.size());

        auto batchWork = [&]() {
            for (size_t j = 0; j < ptrcallJobs.size(); j++) {
                auto& job = ptrcallJobs[j];

                int type4x = job.variantType;
                if (GodotVer::IsGodot3()) {
                    type4x = MapType3xTo4x(job.variantType);
                    if (type4x < 0) continue;
                }
                int retSize = PtrcallReturnSize(type4x);
                if (retSize <= 0) continue;

                uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(job.methodBind, 0);
                if (!vtable || !Zoltraak::IsReadable(vtable)) continue;
                uint64_t ptrcallFn = Zoltraak::ReadSafe<uint64_t>(
                    vtable + DynOff::METHODBIND_PTRCALL_VTIDX * 8, 0);
                if (!ptrcallFn || !Zoltraak::IsReadable(ptrcallFn)) continue;

                using PtrcallFn = void(__fastcall*)(void*, void*, const void**, void*);
                auto fnPtr = reinterpret_cast<PtrcallFn>(ptrcallFn);

                alignas(16) uint8_t retBuf[128] = {};
                bool callOk = SafePtrcallInvoke(fnPtr, job.methodBind, objAddr, retBuf);
                if (!callOk) {
                    Solitaer::Warn("WALK", "  SEH in ptrcall for prop[%zu] MB=%p fn=%p type=%d",
                        job.propIndex, reinterpret_cast<void*>(job.methodBind),
                        reinterpret_cast<void*>(ptrcallFn), job.variantType);
                    continue;
                }

                ptrcallResults[j].value = DecodeReturnBuffer(retBuf, job.variantType, type4x);
                ptrcallResults[j].ok = true;
            }
        };

        bool dispatched = Heiter::QueueWorkSync(batchWork, Zolt::Heiter::SYNC_TIMEOUT_MS);
        if (dispatched) {
            for (size_t j = 0; j < ptrcallJobs.size(); j++) {
                if (ptrcallResults[j].ok && !ptrcallResults[j].value.value.empty()) {
                    entries[ptrcallJobs[j].propIndex].value = std::move(ptrcallResults[j].value);
                    entries[ptrcallJobs[j].propIndex].fromPtrcall = true;
                    valuesRead++;
                    ptrcallReads++;
                }
            }
        }
    }

    Solitaer::Debug("WALK", "WalkInstance: ptrcall phase done. Emitting %d entries...",
        static_cast<int>(entries.size()));

    // Emit all entries
    for (auto& entry : entries) {
        callback(entry);
    }

    if (valuesRead > 0) {
        Solitaer::Debug("WALK", "WalkInstance: read %d/%d values (%d direct, %d ptrcall) for '%s'",
            valuesRead, static_cast<int>(properties.size()),
            directReads, ptrcallReads, className.c_str());
    }

    // Step 4: Probe ScriptInstance offset (retries on different objects until found or max attempts)
    ProbeScriptInstanceOffset(objAddr);

    // Step 5: Read GDScript member variables (if object has a script attached)
    ReadGDScriptMembers(objAddr, callback);
}

// ============================================================
// GetFieldValue — Get a single field value by property name
// ============================================================
VariantValue GetFieldValue(uint64_t objAddr, const char* propertyName) {
    if (!objAddr || !propertyName) return {};

    std::string className = Diagoldze::GetObjectClass(objAddr);
    if (className.empty()) {
        Solitaer::Debug("WALK", "GetFieldValue: unknown class for %p", reinterpret_cast<void*>(objAddr));
        return {};
    }

    auto properties = Diagoldze::GetPropertyList(className.c_str());
    for (auto& prop : properties) {
        if (prop.name == propertyName) {
            if (prop.offset >= 0) {
                return ReadNativeValue(objAddr + prop.offset, prop.type, prop.nativeSize, prop.bitShift, prop.bitMask);
            }
            if (prop.getterMethodBind != 0) {
                return CallGetterPtrcall(prop.getterMethodBind, objAddr, prop.type);
            }
            // Fallback: metadata only
            VariantValue result;
            result.type     = prop.type;
            result.typeName = VariantTypeName(prop.type);
            result.rawAddr  = 0;
            return result;
        }
    }

    return {};
}

// ============================================================
// ProbeScriptInstanceEarly — Scan ObjectDB at init time to find
// the ScriptInstance offset before any walk_instance call.
// Ubel — 優貝爾 — Early script probe for GDScript member access
// ============================================================
void ProbeScriptInstanceEarly() {
    if (DynOff::bScriptInstanceProbed.load()) return; // Already found

    Solitaer::Info("WALK", "=== Early ScriptInstance probe: scanning ObjectDB ===");

    // Collect candidate objects — prefer game-specific nodes (likely have scripts)
    struct Candidate { uint64_t addr; std::string className; };
    std::vector<Candidate> candidates;
    candidates.reserve(100);

    // Set of engine-only classes unlikely to have scripts attached
    static const char* engineSkip[] = {
        "Window", "Viewport", "World2D", "World3D", "SubViewport",
        "InputMap", "IP", "TranslationServer", "NavigationServer2D",
        "NavigationServer3D", "PhysicsServer2D", "PhysicsServer3D",
        "RenderingServer", "AudioServer", "DisplayServer", "EditorNode",
        "ThemeDB", "ResourceLoader", "ResourceSaver", "ClassDB",
        nullptr
    };

    Ausetiese::ForEachEx([&](int /*index*/, uint64_t objAddr) {
        if (candidates.size() >= 100) return false; // enough candidates

        std::string cls = Diagoldze::GetObjectClass(objAddr);
        if (cls.empty()) return true;

        // Skip common engine singletons
        bool skip = false;
        for (int i = 0; engineSkip[i]; i++) {
            if (cls == engineSkip[i]) { skip = true; break; }
        }
        if (skip) return true;

        // Prioritize Node-derived classes (game objects with scripts)
        bool isNode = Diagoldze::IsNodeDerived(cls);
        if (isNode) {
            candidates.push_back({ objAddr, std::move(cls) });
        }

        return true;
    });

    Solitaer::Info("WALK", "  Found %d Node candidates to probe", static_cast<int>(candidates.size()));

    int probed = 0;
    for (auto& c : candidates) {
        if (DynOff::bScriptInstanceProbed.load()) break; // Found!

        // Reset attempt counter so ProbeScriptInstanceOffset doesn't refuse
        // (it checks s_probeAttempts against MAX_PROBE_ATTEMPTS)
        ProbeScriptInstanceOffset(c.addr);
        probed++;

        if (probed >= 50) break; // Don't spend too long
    }

    if (DynOff::bScriptInstanceProbed.load()) {
        Solitaer::Info("WALK", "  Early probe SUCCESS: ScriptInstance at Object+0x%X (after %d objects)",
            DynOff::OBJECT_SCRIPT_INSTANCE, probed);
    } else {
        Solitaer::Info("WALK", "  Early probe: no ScriptInstance found (probed %d objects). "
            "Will retry on walk_instance calls.", probed);
    }
    Solitaer::Info("WALK", "=== Early ScriptInstance probe complete ===");
}

// ============================================================
// FindScriptMemberByAddress — Reverse-lookup a GDScript variable address
// to its owning Object. Scans all Objects in ObjectDB for ScriptInstance
// whose members Vector contains the target address.
// ============================================================
ScriptMemberMatch FindScriptMemberByAddress(uint64_t addr) {
    ScriptMemberMatch result;
    if (!addr || DynOff::OBJECT_SCRIPT_INSTANCE < 0) return result;

    int variantStride = DynOff::VARIANT_STRIDE;
    int sizeOff = GodotVer::IsGodot3() ? 4 : 8;

    Ausetiese::ForEachEx([&](int /*index*/, uint64_t objAddr) {
        // Read ScriptInstance pointer from Object
        uint64_t scriptInst = Zoltraak::ReadSafe<uint64_t>(
            objAddr + DynOff::OBJECT_SCRIPT_INSTANCE, 0);
        if (!scriptInst || !Zoltraak::IsReadable(scriptInst)) return true;

        // Verify owner backlink
        uint64_t owner = Zoltraak::ReadSafe<uint64_t>(
            scriptInst + DynOff::GDSCRIPT_OWNER_OFF, 0);
        if (owner != objAddr) return true;

        // Read members Vector (CowData pointer)
        uint64_t membersPtr = Zoltraak::ReadSafe<uint64_t>(
            scriptInst + DynOff::GDSCRIPT_MEMBERS_OFF, 0);
        if (!membersPtr || !Zoltraak::IsReadable(membersPtr)) return true;

        // Read count
        uint32_t count = Zoltraak::ReadSafe<uint32_t>(membersPtr - sizeOff, 0);
        if (count == 0 || count > 500) return true;

        // Check if addr falls within [membersPtr, membersPtr + count * variantStride)
        uint64_t rangeStart = membersPtr;
        uint64_t rangeEnd   = membersPtr + static_cast<uint64_t>(count) * variantStride;

        if (addr >= rangeStart && addr < rangeEnd) {
            uint64_t displacement = addr - rangeStart;
            result.objectAddr    = objAddr;
            result.memberIndex   = static_cast<int>(displacement / variantStride);
            result.variantOffset = static_cast<int>(displacement % variantStride);

            Solitaer::Debug("WALK", "FindScriptMemberByAddress: %p → Object %p, "
                "member[%d] + 0x%X (members at %p, count=%u)",
                reinterpret_cast<void*>(addr),
                reinterpret_cast<void*>(objAddr),
                result.memberIndex, result.variantOffset,
                reinterpret_cast<void*>(membersPtr), count);
            return false; // found — stop
        }

        return true; // continue
    });

    return result;
}

// Public wrapper for ReadGDScriptMemberNames (used by Qual mailbox)
std::unordered_map<int, std::string> GetMemberNames(uint64_t gdscriptPtr) {
    return ReadGDScriptMemberNames(gdscriptPtr);
}

} // namespace Zolt::Ubel
