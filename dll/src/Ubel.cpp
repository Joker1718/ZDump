// Ubel — 尤蓓爾 (Assassin Specialist — Variant/Property Reader)
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
#include "Metode.h"
#include "Himmel.h"

#include <cstdio>
#include <cmath>
#include <chrono>
#include <iterator>
#include <mutex>
#include <unordered_map>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>

namespace Zolt::Ubel {

// Suppress verbose debug logging during bulk scans (Phase 5/6/7).
// Set to true before bulk ForEachEx loops, false after.
static thread_local bool s_bulkScanQuiet = false;

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

// Version-aware type name lookup (public — used by Fern.cpp too)
const char* VariantTypeName(int type) {
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

// Forward declarations for container reading (used by ReadVariant, defined below)
static std::pair<int, uint64_t> ReadArrayInternals(uint64_t dataAddr);

// Dictionary internals return: {elementCount, headElementPtr}
// headElementPtr differs by version:
//   4.x: HashMapElement* (walk via ->next at +0x00)
//   3.x: List::Element*  (walk via ->next at element+keyPtrSize+VARIANT_STRIDE)
// Result for ReadDictionaryInternals
struct DictInternals {
    int      count = 0;
    uint64_t head  = 0;
    int      elemPrefix = 0; // 0 = standard, 0x10 = extended (custom builds with per-element metadata)
};
static DictInternals ReadDictionaryInternals(uint64_t dataAddr);

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

        case 27: { // DICTIONARY — show element count via ReadDictionaryInternals
            auto di = ReadDictionaryInternals(data);
            char buf[64];
            if (di.head) {
                snprintf(buf, sizeof(buf), "Dictionary[%d]", di.count);
            } else {
                snprintf(buf, sizeof(buf), "Dictionary(empty)");
            }
            result.value = buf;
            break;
        }

        case 28: { // ARRAY — show element count
            auto [count, dataPtr] = ReadArrayInternals(data);
            char buf[64];
            if (dataPtr) {
                snprintf(buf, sizeof(buf), "Array[%d]", count);
            } else {
                snprintf(buf, sizeof(buf), "Array(empty)");
            }
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
// Container reading — Array / Dictionary / PackedArray
// Ubel — 尤蓓爾 (Assassin Specialist) — Drills into container internals
// ============================================================

// Godot Array memory layout:
//   Variant data = pointer to ArrayPrivate*
//   ArrayPrivate:
//     +0x00: SafeRefCount refcount (4 bytes + 4 padding)
//     +0x08: Vector<Variant> array → CowData<Variant>._ptr (8 bytes)
//     +0x10: Variant* read_only (4.x only)
//   CowData<T>._ptr points to element data:
//     _ptr - 4: uint32_t size (element count)
//     _ptr + 0: T[0], T[1], ...  (each T is VARIANT_STRIDE bytes for Variant)

// Read Array element count and data pointer from Variant data address.
// Returns {elementCount, dataPtr} or {0, 0} on failure.
static std::pair<int, uint64_t> ReadArrayInternals(uint64_t dataAddr) {
    // dataAddr points to the Variant data for an Array-type Variant.
    // The data is a pointer to ArrayPrivate._p
    uint64_t arrayPrivate = Zoltraak::ReadSafe<uint64_t>(dataAddr, 0);
    if (!arrayPrivate || !Zoltraak::IsReadable(arrayPrivate)) {
        // NULL _p = genuinely empty/uninitialized Array (common in Godot)
        return {0, 0};
    }

    // Vector<Variant>.cowdata._ptr at ArrayPrivate + 0x08
    // Godot 3.x & 4.x: ArrayPrivate { SafeRefCount(4+pad=8), Vector<Variant>(8) }
    uint64_t cowPtr = Zoltraak::ReadSafe<uint64_t>(arrayPrivate + 0x08, 0);
    if (!cowPtr || !Zoltraak::IsReadable(cowPtr)) {
        // NULL cowdata._ptr = allocated ArrayPrivate but empty Vector (0 elements)
        if (!s_bulkScanQuiet)
            Solitaer::Debug("WALK", "ReadArrayInternals: empty Vector in ArrayPrivate %p (cowPtr=%p)",
                reinterpret_cast<void*>(arrayPrivate), reinterpret_cast<void*>(cowPtr));
        return {0, 0};
    }

    // CowData size is at _ptr - 4 (uint32_t)
    uint32_t count = Zoltraak::ReadSafe<uint32_t>(cowPtr - 4, 0);
    if (count > 100000) {
        // Sanity check — try alternative: maybe CowData size is at _ptr - 8 (uint64_t USize)
        // Some Godot versions use 64-bit USize
        uint32_t count64 = static_cast<uint32_t>(Zoltraak::ReadSafe<uint64_t>(cowPtr - 8, 0));
        if (count64 > 0 && count64 <= 100000) {
            if (!s_bulkScanQuiet)
                Solitaer::Info("WALK", "ReadArrayInternals: 64-bit USize detected, count=%u at %p",
                    count64, reinterpret_cast<void*>(cowPtr - 8));
            return {static_cast<int>(count64), cowPtr};
        }
        if (!s_bulkScanQuiet)
            Solitaer::Info("WALK", "ReadArrayInternals: suspicious count %u (and 64-bit %u) at ArrayPrivate %p",
                count, count64, reinterpret_cast<void*>(arrayPrivate));
        return {0, 0};
    }

    if (!s_bulkScanQuiet)
        Solitaer::Debug("WALK", "ReadArrayInternals: count=%u cowPtr=%p (ArrayPrivate=%p)",
            count, reinterpret_cast<void*>(cowPtr), reinterpret_cast<void*>(arrayPrivate));
    return {static_cast<int>(count), cowPtr};
}

// ============================================================
// ReadDictionaryInternals — Read Dictionary element count and head element
// ============================================================
// Godot 4.x Dictionary layout:
//   DictionaryPrivate:
//     +0x00: SafeRefCount (8 bytes)
//     +0x08: Variant *read_only (8 bytes)
//     +0x10: HashMap<Variant,Variant> variant_map
//       HashMap fields (compiler-dependent offset for empty allocator):
//         GCC/Clang: elements**(+0), hashes*(+8), head*(+16), tail*(+24), cap(+32), num(+36)
//         MSVC:      alloc(+0,8pad), elements**(+8), hashes*(+16), head*(+24), tail*(+32), cap(+40), num(+44)
//       We probe both patterns.
//
// Godot 3.x Dictionary layout:
//   DictionaryPrivate:
//     +0x00: SafeRefCount (8 bytes)
//     +0x08: OrderedHashMap<Variant,Variant>
//       +0x00: List<Pair<K*,V>>  (just _Data* pointer, 8 bytes)
//       +0x08: HashMap<K, ListElement*>
//     List._Data: { Element *first, Element *last, int size_cache }
//
// Returns {count, head, elemPrefix} or defaults on failure.
// elemPrefix: 0 for standard element layout (next@+0x00, prev@+0x08, key@+0x10),
//             0x10 for extended layout (classPtr@+0x00, hash@+0x08, next@+0x10, prev@+0x18, key@+0x20).
static DictInternals ReadDictionaryInternals(uint64_t dataAddr) {
    uint64_t dictPrivate = Zoltraak::ReadSafe<uint64_t>(dataAddr, 0);
    if (!dictPrivate || !Zoltraak::IsReadable(dictPrivate)) {
        return {0, 0}; // NULL _p = empty/uninitialized Dictionary
    }

    if (GodotVer::IsGodot3()) {
        // 3.x: DictionaryPrivate+0x08 = OrderedHashMap.list (_Data* pointer)
        uint64_t listData = Zoltraak::ReadSafe<uint64_t>(dictPrivate + 0x08, 0);
        if (!listData || !Zoltraak::IsReadable(listData)) {
            if (!s_bulkScanQuiet)
                Solitaer::Debug("WALK", "ReadDictInternals(3.x): NULL list._data at %p",
                    reinterpret_cast<void*>(dictPrivate + 0x08));
            return {0, 0};
        }
        // _Data: first(8B) + last(8B) + size_cache(4B)
        uint64_t firstElem = Zoltraak::ReadSafe<uint64_t>(listData + 0x00, 0);
        int count = static_cast<int>(Zoltraak::ReadSafe<uint32_t>(listData + 0x10, 0));
        if (count < 0 || count > 100000) count = 0;
        if (!s_bulkScanQuiet)
            Solitaer::Info("WALK", "ReadDictInternals(3.x): count=%d first=%p (_Data=%p)",
                count, reinterpret_cast<void*>(firstElem), reinterpret_cast<void*>(listData));
        return {count, firstElem, 0};
    }

    // 4.x: HashMap starts at DictionaryPrivate+0x10
    uint64_t hmBase = dictPrivate + 0x10;

    // Probe HashMap layout for head_element and num_elements.
    // Godot 4.x HashMap has DefaultTypedAllocator (empty class, padded to 8 bytes)
    // before the element array pointer. Both GCC and MSVC pad to 8 bytes in C++17,
    // giving the same layout: head at +0x18, num at +0x2C.
    // We also try the legacy "no padding" pattern as fallback for older/custom builds.
    struct ProbePattern {
        int headOff;
        int numOff;
        const char* label;
    };
    static const ProbePattern patterns[] = {
        {0x18, 0x2C, "std"},      // Standard: 8-byte padded empty allocator (GCC+MSVC)
        {0x10, 0x24, "noPad"},    // Fallback: zero-padded allocator (unlikely)
        {0x20, 0x34, "ext8"},     // Custom: extra 8B in DictPrivate or HashMap (+8 shift)
        {0x28, 0x3C, "ext16"},    // Custom: extra 16B in DictPrivate or HashMap (+16 shift)
    };

    // Element layout variants:
    //   Standard: next(8) + prev(8) + key(stride) + value(stride), prefix=0
    //   Extended: classPtr(8) + hash(8) + next(8) + prev(8) + key + value, prefix=0x10
    //     (custom Godot builds with extra metadata per HashMap element)
    static const int elemPrefixes[] = {0, 0x10};

    for (auto& pat : patterns) {
        uint64_t head = Zoltraak::ReadSafe<uint64_t>(hmBase + pat.headOff, 0);
        uint32_t num  = Zoltraak::ReadSafe<uint32_t>(hmBase + pat.numOff, 0);
        if (num > 0 && num <= 100000 && head != 0 && Zoltraak::IsReadable(head)) {
            // Try both standard and extended element layouts for head validation
            for (int prefix : elemPrefixes) {
                int nextOff = prefix + 0x00;
                int prevOff = prefix + 0x08;

                // Validate: head->next should be readable pointer or NULL
                uint64_t headNext = Zoltraak::ReadSafe<uint64_t>(head + nextOff, 0);
                if (headNext != 0 && !Zoltraak::IsReadable(headNext)) continue;
                // Validate: head->prev should be NULL (it's the first element)
                uint64_t headPrev = Zoltraak::ReadSafe<uint64_t>(head + prevOff, 0);
                if (headPrev != 0) continue;

                // For extended layout, extra validation: classPtr at +0x00 must be
                // a readable pointer, hash at +0x08 must NOT be a readable pointer
                if (prefix == 0x10) {
                    uint64_t classPtr = Zoltraak::ReadSafe<uint64_t>(head, 0);
                    uint64_t hashVal  = Zoltraak::ReadSafe<uint64_t>(head + 8, 0);
                    if (!classPtr || !Zoltraak::IsReadable(classPtr)) continue;
                    if (hashVal != 0 && Zoltraak::IsReadable(hashVal)) continue;
                }

                // Validate: walk a few elements via next pointer and count matches num
                int walkCount = 1;
                uint64_t cur = headNext;
                while (cur && walkCount < 5 && Zoltraak::IsReadable(cur)) {
                    walkCount++;
                    cur = Zoltraak::ReadSafe<uint64_t>(cur + nextOff, 0);
                }
                // For small dicts, walkCount should match num
                if (num <= 5 && walkCount != static_cast<int>(num)) continue;

                if (!s_bulkScanQuiet)
                    Solitaer::Info("WALK", "ReadDictInternals(4.x %s prefix=%d): count=%u head=%p (hmBase=%p)",
                        pat.label, prefix, num, reinterpret_cast<void*>(head), reinterpret_cast<void*>(hmBase));
                return {static_cast<int>(num), head, prefix};
            }
        }
    }

    // All patterns failed — might be empty Dictionary
    if (!s_bulkScanQuiet)
        Solitaer::Debug("WALK", "ReadDictInternals(4.x): no valid pattern at hmBase=%p",
            reinterpret_cast<void*>(hmBase));
    return {};
}

// Read PackedArray element count and data pointer.
// PackedArray variants store a RefCounted pointer to PackedArrayRef<T>.
// PackedArrayRef<T> contains Vector<T> → CowData<T>._ptr.
// Layout:
//   Variant data = pointer to PackedArrayRef*
//   PackedArrayRef (extends RefCounted):
//     +0x00: RefCounted base (varies, typically 16-24 bytes)
//     After RefCounted: Vector<T>.cowdata._ptr
//   For simplicity, we probe multiple candidate offsets for the CowData pointer.
static std::pair<int, uint64_t> ReadPackedArrayInternals(uint64_t dataAddr, int elementSize) {
    uint64_t refPtr = Zoltraak::ReadSafe<uint64_t>(dataAddr, 0);
    if (!refPtr || !Zoltraak::IsReadable(refPtr)) return {0, 0};

    // Try common offsets for the CowData pointer within PackedArrayRef
    // RefCounted base is typically 8-16 bytes (refcount + ptr), then Vector<T>
    const int candidateOffsets[] = {0x08, 0x10, 0x18, 0x20};
    for (int off : candidateOffsets) {
        uint64_t cowPtr = Zoltraak::ReadSafe<uint64_t>(refPtr + off, 0);
        if (!cowPtr || !Zoltraak::IsReadable(cowPtr)) continue;

        uint32_t count = Zoltraak::ReadSafe<uint32_t>(cowPtr - 4, 0);
        if (count == 0 || count > 100000) continue;

        // Validate: check that data at cowPtr is readable for at least one element
        if (elementSize > 0 && Zoltraak::IsReadable(cowPtr)) {
            Solitaer::Debug("WALK", "ReadPackedArrayInternals: found count=%u at refPtr+0x%X",
                count, off);
            return {static_cast<int>(count), cowPtr};
        }
    }
    return {0, 0};
}

// Map 4.x packed array type to element size in bytes
static int PackedArrayElementSize(int type4x) {
    switch (type4x) {
        case 29: return 1;  // PackedByteArray
        case 30: return 4;  // PackedInt32Array
        case 31: return 8;  // PackedInt64Array
        case 32: return 4;  // PackedFloat32Array
        case 33: return 8;  // PackedFloat64Array
        case 34: return 8;  // PackedStringArray (CowData<String> ptr)
        case 35: return 8;  // PackedVector2Array (2 floats)
        case 36: return 12; // PackedVector3Array (3 floats)
        case 37: return 16; // PackedColorArray (4 floats)
        case 38: return 16; // PackedVector4Array (4 floats, 4.x only)
        default: return 0;
    }
}

// Read a single PackedArray element as a VariantValue
static VariantValue ReadPackedElement(uint64_t elemAddr, int type4x) {
    VariantValue result{};
    result.rawAddr = elemAddr;
    result.ptrTarget = 0;

    switch (type4x) {
        case 29: { // PackedByteArray → uint8_t
            uint8_t v = Zoltraak::ReadSafe<uint8_t>(elemAddr, 0);
            result.typeName = "Byte";
            result.type = type4x;
            result.value = std::to_string(v);
            break;
        }
        case 30: { // PackedInt32Array → int32_t
            int32_t v = Zoltraak::ReadSafe<int32_t>(elemAddr, 0);
            result.typeName = "Int32";
            result.type = type4x;
            result.value = std::to_string(v);
            break;
        }
        case 31: { // PackedInt64Array → int64_t
            int64_t v = Zoltraak::ReadSafe<int64_t>(elemAddr, 0);
            result.typeName = "Int64";
            result.type = type4x;
            result.value = std::to_string(v);
            break;
        }
        case 32: { // PackedFloat32Array → float
            float v = Zoltraak::ReadSafe<float>(elemAddr, 0.0f);
            result.typeName = "Float32";
            result.type = type4x;
            result.value = FormatFloat(v);
            break;
        }
        case 33: { // PackedFloat64Array → double
            double v = Zoltraak::ReadSafe<double>(elemAddr, 0.0);
            result.typeName = "Float64";
            result.type = type4x;
            result.value = FormatDouble(v);
            break;
        }
        case 34: { // PackedStringArray → String (CowData<char32_t>)
            uint64_t cowPtr = Zoltraak::ReadSafe<uint64_t>(elemAddr, 0);
            result.typeName = "String";
            result.type = 4; // String variant type
            result.value = (cowPtr && Zoltraak::IsReadable(cowPtr))
                           ? Zoltraak::ReadGodotString(cowPtr)
                           : "\"\"";
            break;
        }
        case 35: { // PackedVector2Array → 2 floats
            float x = Zoltraak::ReadSafe<float>(elemAddr, 0.0f);
            float y = Zoltraak::ReadSafe<float>(elemAddr + 4, 0.0f);
            result.typeName = "Vector2";
            result.type = 5;
            result.value = "(" + FormatFloat(x) + ", " + FormatFloat(y) + ")";
            break;
        }
        case 36: { // PackedVector3Array → 3 floats
            float x = Zoltraak::ReadSafe<float>(elemAddr, 0.0f);
            float y = Zoltraak::ReadSafe<float>(elemAddr + 4, 0.0f);
            float z = Zoltraak::ReadSafe<float>(elemAddr + 8, 0.0f);
            result.typeName = "Vector3";
            result.type = 9;
            result.value = "(" + FormatFloat(x) + ", " + FormatFloat(y) + ", " + FormatFloat(z) + ")";
            break;
        }
        case 37: { // PackedColorArray → 4 floats (RGBA)
            float r = Zoltraak::ReadSafe<float>(elemAddr, 0.0f);
            float g = Zoltraak::ReadSafe<float>(elemAddr + 4, 0.0f);
            float b = Zoltraak::ReadSafe<float>(elemAddr + 8, 0.0f);
            float a = Zoltraak::ReadSafe<float>(elemAddr + 12, 0.0f);
            result.typeName = "Color";
            result.type = 20;
            result.value = "Color(" + FormatFloat(r) + ", " + FormatFloat(g) +
                          ", " + FormatFloat(b) + ", " + FormatFloat(a) + ")";
            break;
        }
        case 38: { // PackedVector4Array → 4 floats
            float x = Zoltraak::ReadSafe<float>(elemAddr, 0.0f);
            float y = Zoltraak::ReadSafe<float>(elemAddr + 4, 0.0f);
            float z = Zoltraak::ReadSafe<float>(elemAddr + 8, 0.0f);
            float w = Zoltraak::ReadSafe<float>(elemAddr + 12, 0.0f);
            result.typeName = "Vector4";
            result.type = 12;
            result.value = "(" + FormatFloat(x) + ", " + FormatFloat(y) +
                          ", " + FormatFloat(z) + ", " + FormatFloat(w) + ")";
            break;
        }
        default:
            result.typeName = "Unknown";
            result.value = "?";
            break;
    }
    return result;
}

ContainerInfo ReadContainerInfo(uint64_t dataAddr, int variantType) {
    ContainerInfo info{};

    int type4x = variantType;
    if (GodotVer::IsGodot3()) {
        type4x = MapType3xTo4x(variantType);
        if (type4x < 0) return info;
    }

    info.containerType = VariantTypeName(variantType);

    if (type4x == 28) { // Array
        auto [count, dataPtr] = ReadArrayInternals(dataAddr);
        info.elementCount = count;
        Solitaer::Debug("WALK", "ReadContainerInfo(Array): count=%d dataPtr=%p",
            count, reinterpret_cast<void*>(dataPtr));
    }
    else if (type4x == 27) { // Dictionary
        auto di = ReadDictionaryInternals(dataAddr);
        info.elementCount = di.count;
        Solitaer::Debug("WALK", "ReadContainerInfo(Dictionary): count=%d head=%p prefix=%d",
            di.count, reinterpret_cast<void*>(di.head), di.elemPrefix);
    }
    else if (type4x >= 29 && type4x <= 38) { // Packed arrays
        int elemSize = PackedArrayElementSize(type4x);
        auto [count, dataPtr] = ReadPackedArrayInternals(dataAddr, elemSize);
        info.elementCount = count;
        Solitaer::Debug("WALK", "ReadContainerInfo(Packed %s): count=%d elemSize=%d",
            info.containerType.c_str(), count, elemSize);
    }

    return info;
}

std::vector<ContainerElement> ReadContainerElements(uint64_t dataAddr, int variantType,
                                                     int offset, int limit) {
    std::vector<ContainerElement> elements;

    int type4x = variantType;
    if (GodotVer::IsGodot3()) {
        type4x = MapType3xTo4x(variantType);
        if (type4x < 0) return elements;
    }

    if (type4x == 28) { // Array — elements are Variant[]
        auto [count, dataPtr] = ReadArrayInternals(dataAddr);
        if (count <= 0 || !dataPtr) return elements;

        int variantStride = DynOff::VARIANT_STRIDE;
        int start = (offset < count) ? offset : count;
        int end   = ((start + limit) < count) ? (start + limit) : count;

        Solitaer::Info("WALK", "ReadContainerElements(Array): reading [%d..%d) of %d (stride=0x%X)",
            start, end, count, variantStride);

        for (int i = start; i < end; i++) {
            uint64_t varAddr = dataPtr + static_cast<uint64_t>(i) * variantStride;
            VariantValue val = ReadVariant(varAddr);

            ContainerElement elem;
            elem.index = i;
            elem.value = val;
            // Fix address to point to Variant data (consistent with WalkInstance)
            elem.value.rawAddr = varAddr + DynOff::VARIANT_DATA;
            elements.push_back(std::move(elem));
        }
    }
    else if (type4x >= 29 && type4x <= 38) { // Packed arrays
        int elemSize = PackedArrayElementSize(type4x);
        if (elemSize <= 0) return elements;

        auto [count, dataPtr] = ReadPackedArrayInternals(dataAddr, elemSize);
        if (count <= 0 || !dataPtr) return elements;

        int start = (offset < count) ? offset : count;
        int end   = ((start + limit) < count) ? (start + limit) : count;

        Solitaer::Info("WALK", "ReadContainerElements(Packed): reading [%d..%d) of %d (elemSize=%d)",
            start, end, count, elemSize);

        for (int i = start; i < end; i++) {
            uint64_t elemAddr = dataPtr + static_cast<uint64_t>(i) * elemSize;
            ContainerElement elem;
            elem.index = i;
            elem.value = ReadPackedElement(elemAddr, type4x);
            elements.push_back(std::move(elem));
        }
    }
    else if (type4x == 27) { // Dictionary — key-value pairs via linked list
        auto di = ReadDictionaryInternals(dataAddr);
        int count = di.count;
        uint64_t headElem = di.head;
        int dictElemPrefix = di.elemPrefix;
        if (count <= 0 || !headElem) return elements;

        int variantStride = DynOff::VARIANT_STRIDE;
        int variantData   = DynOff::VARIANT_DATA;

        // Clamp offset/limit
        int start = (offset < count) ? offset : count;
        int end   = ((start + limit) < count) ? (start + limit) : count;

        Solitaer::Info("WALK", "ReadContainerElements(Dict): reading [%d..%d) of %d (stride=0x%X)",
            start, end, count, variantStride);

        if (GodotVer::IsGodot3()) {
            // 3.x: List<Pair<Variant*, Variant>>::Element
            // Element layout: value.first(Variant* 8B) + value.second(Variant, VARIANT_STRIDE)
            //                 + next_ptr(8B) + prev_ptr(8B) + data_ptr(8B)
            // next is at offset: 8 + VARIANT_STRIDE = 0x20 (stride=0x18)
            int nextOff = 8 + variantStride;
            uint64_t cur = headElem;
            for (int i = 0; i < end && cur && Zoltraak::IsReadable(cur); i++) {
                if (i >= start) {
                    ContainerElement elem;
                    elem.index = i;
                    // Key: dereference the Variant* at +0x00
                    uint64_t keyPtr = Zoltraak::ReadSafe<uint64_t>(cur + 0x00, 0);
                    if (keyPtr && Zoltraak::IsReadable(keyPtr)) {
                        VariantValue kv = ReadVariant(keyPtr);
                        // Prefix key type for non-string keys to aid debugging
                        if (kv.typeName == "String" || kv.typeName == "NodePath") {
                            elem.key = kv.value;
                        } else {
                            elem.key = "(" + kv.typeName + ")" + kv.value;
                        }
                        Solitaer::Debug("WALK", "Dict[%d] key: type=%s val='%s' keyPtr=%p",
                            i, kv.typeName.c_str(), kv.value.c_str(),
                            reinterpret_cast<void*>(keyPtr));
                    }
                    // Value: inline Variant at +0x08
                    elem.value = ReadVariant(cur + 0x08);
                    elem.value.rawAddr = cur + 0x08 + variantData;
                    Solitaer::Debug("WALK", "Dict[%d] val: type=%s val='%s' elem=%p next@%p",
                        i, elem.value.typeName.c_str(), elem.value.value.c_str(),
                        reinterpret_cast<void*>(cur),
                        reinterpret_cast<void*>(cur + nextOff));
                    elements.push_back(std::move(elem));
                }
                cur = Zoltraak::ReadSafe<uint64_t>(cur + nextOff, 0);
            }
        } else {
            // 4.x: HashMapElement<Variant, Variant> linked list
            // Standard layout: next(8B) + prev(8B) + KeyValue{key(stride) + value(stride)}
            // Extended layout: classPtr(8B) + hash(8B) + next(8B) + prev(8B) + key + value
            //   (custom builds with per-element metadata, prefix=0x10)
            int p = dictElemPrefix;
            int keyOff   = p + 0x10;
            int valueOff = p + 0x10 + variantStride;
            int nextOff  = p + 0x00;
            uint64_t cur = headElem;
            for (int i = 0; i < end && cur && Zoltraak::IsReadable(cur); i++) {
                if (i >= start) {
                    ContainerElement elem;
                    elem.index = i;
                    // Key: inline Variant at prefix+0x10
                    VariantValue kv = ReadVariant(cur + keyOff);
                    // Prefix key type for non-string keys
                    if (kv.typeName == "String" || kv.typeName == "StringName" || kv.typeName == "NodePath") {
                        elem.key = kv.value;
                    } else {
                        elem.key = "(" + kv.typeName + ")" + kv.value;
                    }
                    // Value: inline Variant at prefix+0x10+stride
                    elem.value = ReadVariant(cur + valueOff);
                    elem.value.rawAddr = cur + valueOff + variantData;
                    elements.push_back(std::move(elem));
                }
                // next pointer at prefix+0x00
                cur = Zoltraak::ReadSafe<uint64_t>(cur + nextOff, 0);
            }
        }
    }

    return elements;
}

// ============================================================
// FindDictValueByKey — Look up a dictionary element by key string
// ============================================================
// Walks the dictionary linked list, reading and decoding each key.
// Returns the value Variant's data address if a matching key is found.
uint64_t FindDictValueByKey(uint64_t dataAddr, const std::string& key) {
    auto di = ReadDictionaryInternals(dataAddr);
    if (di.count <= 0 || !di.head) return 0;

    int variantStride = DynOff::VARIANT_STRIDE;
    int variantData   = DynOff::VARIANT_DATA;

    if (GodotVer::IsGodot3()) {
        // 3.x: key is Variant* at +0x00, value is inline Variant at +0x08
        int nextOff = 8 + variantStride;
        uint64_t cur = di.head;
        for (int i = 0; i < di.count && cur && Zoltraak::IsReadable(cur); i++) {
            uint64_t keyPtr = Zoltraak::ReadSafe<uint64_t>(cur + 0x00, 0);
            if (keyPtr && Zoltraak::IsReadable(keyPtr)) {
                VariantValue kv = ReadVariant(keyPtr);
                if (kv.value == key) {
                    return cur + 0x08 + variantData;
                }
            }
            cur = Zoltraak::ReadSafe<uint64_t>(cur + nextOff, 0);
        }
    } else {
        // 4.x: HashMapElement linked list
        int p = di.elemPrefix;
        int keyOff   = p + 0x10;
        int valueOff = p + 0x10 + variantStride;
        int nextOff  = p + 0x00;
        uint64_t cur = di.head;
        for (int i = 0; i < di.count && cur && Zoltraak::IsReadable(cur); i++) {
            VariantValue kv = ReadVariant(cur + keyOff);
            if (kv.value == key) {
                return cur + valueOff + variantData;
            }
            cur = Zoltraak::ReadSafe<uint64_t>(cur + nextOff, 0);
        }
    }

    Solitaer::Debug("WALK", "FindDictValueByKey: key '%s' not found in dict (count=%d)",
                    key.c_str(), di.count);
    return 0;
}

// ============================================================
// FindArrayValueByIndex — Look up an array element by index
// ============================================================
uint64_t FindArrayValueByIndex(uint64_t dataAddr, int index) {
    auto [count, dataPtr] = ReadArrayInternals(dataAddr);
    if (count <= 0 || !dataPtr || index < 0 || index >= count) return 0;

    int variantStride = DynOff::VARIANT_STRIDE;
    uint64_t varAddr = dataPtr + static_cast<uint64_t>(index) * variantStride;
    return varAddr + DynOff::VARIANT_DATA;
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

        case 27: { // DICTIONARY (native — pointer to internals)
            uint64_t dictPtr = Zoltraak::ReadSafe<uint64_t>(memberAddr, 0);
            char buf[64];
            snprintf(buf, sizeof(buf), "Dictionary@%llX",
                static_cast<unsigned long long>(dictPtr));
            result.value = buf;
            break;
        }

        case 28: { // ARRAY (native — pointer to ArrayPrivate)
            auto [count, dataPtr] = ReadArrayInternals(memberAddr);
            char buf[64];
            if (dataPtr) {
                snprintf(buf, sizeof(buf), "Array[%d]", count);
            } else {
                snprintf(buf, sizeof(buf), "Array(empty)");
            }
            result.value = buf;
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

    // GDScriptInstance layout differs between versions:
    //   4.x: vtable(8) + owner_id(8) + owner(8) + script(8) + members(8...)
    //        → owner at +0x10, script at +0x18, members at +0x20
    //   3.x: vtable(8) + owner(8) + script(8) + members(8...)
    //        → owner at +0x08, script at +0x10, members at +0x18  (no owner_id)
    // Try both layouts when probing (preferred version first)
    struct InstanceLayout {
        int ownerOff;   // Offset of Object* owner within GDScriptInstance
        int scriptOff;  // Offset of Ref<GDScript> script
        int membersOff; // Offset of Vector<Variant> members
        const char* tag;
    };
    InstanceLayout layouts[] = {
        { 0x08, 0x10, 0x18, "3.x" },  // Godot 3.x: no owner_id
        { 0x10, 0x18, 0x20, "4.x" },  // Godot 4.x: has owner_id at +0x08
    };
    // Prefer the matching version first
    int layoutOrder[2] = { 0, 1 };
    if (!GodotVer::IsGodot3()) {
        layoutOrder[0] = 1; layoutOrder[1] = 0;
    }

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

        // Try each layout variant (3.x and 4.x have different GDScriptInstance layouts)
        for (int li = 0; li < 2; li++) {
            auto& lay = layouts[layoutOrder[li]];

            // Key validation: owner pointer should point back to our Object
            uint64_t owner = Zoltraak::ReadSafe<uint64_t>(candidate + lay.ownerOff, 0);
            if (owner != objAddr) continue;

            // For 4.x layout, check owner_id at +0x08 is non-zero
            if (lay.ownerOff == 0x10) {
                uint64_t ownerId = Zoltraak::ReadSafe<uint64_t>(candidate + 0x08, 0);
                if (ownerId == 0) continue;
            }

            // Check that script pointer is also valid
            uint64_t scriptPtr = Zoltraak::ReadSafe<uint64_t>(candidate + lay.scriptOff, 0);
            if (!scriptPtr || !Zoltraak::IsReadable(scriptPtr)) continue;

            // Found it! Apply the detected layout offsets
            DynOff::OBJECT_SCRIPT_INSTANCE = off;
            DynOff::GDSCRIPT_OWNER_OFF     = lay.ownerOff;
            DynOff::GDSCRIPT_SCRIPT_OFF    = lay.scriptOff;
            DynOff::GDSCRIPT_MEMBERS_OFF   = lay.membersOff;
            DynOff::bScriptInstanceProbed.store(true);
            Solitaer::Info("WALK", "  ScriptInstance offset = +0x%X (inst=%p, script=%p) [%s layout]",
                off, reinterpret_cast<void*>(candidate), reinterpret_cast<void*>(scriptPtr), lay.tag);
            Solitaer::Info("WALK", "  GDScriptInstance offsets: owner=+0x%X script=+0x%X members=+0x%X",
                lay.ownerOff, lay.scriptOff, lay.membersOff);

            // Hexdump first 0x50 bytes of GDScriptInstance for layout diagnosis
            Solitaer::Info("WALK", "  GDScriptInstance hexdump (first 0x50 bytes at %p):",
                reinterpret_cast<void*>(candidate));
            for (int doff = 0; doff < 0x50; doff += 8) {
                uint64_t val = Zoltraak::ReadSafe<uint64_t>(candidate + doff, 0);
                const char* label = "";
                if (doff == 0x00) label = "  <- vtable";
                else if (doff == lay.ownerOff) label = "  <- owner";
                else if (doff == lay.scriptOff) label = "  <- script (Ref)";
                else if (doff == lay.membersOff) label = "  <- members";
                Solitaer::Info("WALK", "    +0x%02X: %016llX%s", doff, val, label);
            }

            // Probe members Vector offset dynamically.
            // We DON'T update GDSCRIPT_MEMBERS_OFF here — the offset will be
            // confirmed by scanning multiple GDScriptInstance objects below
            // (in ProbeScriptInstanceEarly's multi-object vote after this function returns).
            // For now, just log what we see on this single object.
            {
                int searchStart = lay.membersOff;
                int searchEnd   = lay.membersOff + 0x28;
                int sizeOff = GodotVer::IsGodot3() ? 4 : 8;
                for (int moff = searchStart; moff <= searchEnd; moff += 8) {
                    uint64_t mptr = Zoltraak::ReadSafe<uint64_t>(candidate + moff, 0);
                    if (!mptr || !Zoltraak::IsReadable(mptr)) continue;
                    if (mptr % 8 != 0) continue;
                    uint32_t cnt = Zoltraak::ReadSafe<uint32_t>(mptr - sizeOff, 0);
                    if (cnt == 0 || cnt > 1000) continue;
                    int32_t firstType = Zoltraak::ReadSafe<int32_t>(mptr, -1);
                    if (firstType < 0 || firstType >= GodotVer::VariantTypeMax()) continue;
                    Solitaer::Info("WALK", "  Members candidate +0x%02X: ptr=%p count=%u firstType=%d",
                        moff, reinterpret_cast<void*>(mptr), cnt, firstType);
                }
            }

            Solitaer::Info("WALK", "=== ScriptInstance probe complete ===");
            return;
        }  // end layout loop
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
        // 3.x: GDScript::member_indices is Map<StringName, MemberInfo> (red-black tree),
        // NOT a HashMap. Map uses _Data { _root*(8B) + _nil*(8B) + size_cache(4B) }.
        //
        // Map::Element layout varies by build. We probe multiple candidates:
        //   Layout A (color-first): color(4+4pad) + right(8) + left(8) + parent(8) + _next(8) + _prev(8) + key(8) + val
        //   Layout B (ptrs-first):  left(8) + right(8) + parent(8) + _next(8) + _prev(8) + color(4+4pad) + key(8) + val
        //   Layout C (compact):     left(8) + right(8) + parent(8) + _next(8) + _prev(8) + key(8) + val
        struct MapElemLayout {
            int leftOff, nextOff, keyOff, valOff;
            const char* tag;
        };
        constexpr MapElemLayout mapLayouts[] = {
            { 0x10, 0x20, 0x30, 0x38, "color-first" },  // color(4+4pad), right, left, parent, next, prev, key, val
            { 0x00, 0x18, 0x28, 0x30, "ptrs-first" },   // left, right, parent, next, prev, key, val (no leading color)
            { 0x00, 0x18, 0x30, 0x38, "ptrs+color" },   // left, right, parent, next, prev, color(4+4pad), key, val
            { 0x08, 0x20, 0x30, 0x38, "alt-color" },    // color(4+4pad), left, right, parent, next, prev, key, val
        };

        // Hex-dump first 0x400 bytes of GDScript object for diagnostics
        Solitaer::Debug("WALK", "  GDScript object hexdump (first 0x400 bytes at %p):",
            reinterpret_cast<void*>(gdscriptAddr));
        for (int d = 0; d < 0x400; d += 8) {
            uint64_t val = Zoltraak::ReadSafe<uint64_t>(gdscriptAddr + d, 0);
            if (val != 0) {
                Solitaer::Debug("WALK", "    +0x%03X: %016llX%s",
                    d, val, Zoltraak::IsReadable(val) ? " (ptr)" : "");
            }
        }

        // Broad candidate scan: log ALL offsets where (root=ptr, nil=ptr|0, count=1-200)
        // Focus on count matching our expected member count for deep analysis
        Solitaer::Debug("WALK", "  Map _Data scan (looking for root+nil+count patterns):");
        for (int off = 0x40; off <= 0x600; off += 8) {
            uint64_t root  = Zoltraak::ReadSafe<uint64_t>(gdscriptAddr + off, 0);
            uint64_t nil   = Zoltraak::ReadSafe<uint64_t>(gdscriptAddr + off + 0x08, 0);
            uint32_t count = Zoltraak::ReadSafe<uint32_t>(gdscriptAddr + off + 0x10, 0);
            if (root && Zoltraak::IsReadable(root) && count >= 1 && count <= 200) {
                Solitaer::Debug("WALK", "    +0x%03X: root=%p nil=%p count=%u (nil %s)",
                    off, reinterpret_cast<void*>(root), reinterpret_cast<void*>(nil),
                    count, (nil && Zoltraak::IsReadable(nil)) ? "readable" :
                           (nil == 0 ? "NULL" : "unreadable"));
                // For candidates with count >= 10, do deep element analysis
                if (root != nil && count >= 10) {
                    // Dump root element fully (0x80 bytes)
                    Solitaer::Debug("WALK", "      ROOT elem at %p (0x80 bytes):", reinterpret_cast<void*>(root));
                    for (int d = 0; d < 0x80; d += 8) {
                        uint64_t ev = Zoltraak::ReadSafe<uint64_t>(root + d, 0);
                        Solitaer::Debug("WALK", "        +0x%02X: %016llX%s",
                            d, ev, Zoltraak::IsReadable(ev) ? " (ptr)" : "");
                    }
                    // Try Flamme::Resolve (StringName indirection) at every offset
                    for (int ko = 0; ko < 0x70; ko += 8) {
                        std::string name = Flamme::Resolve(root + ko);
                        if (!name.empty()) {
                            Solitaer::Debug("WALK", "      Flamme::Resolve(elem+0x%02X) = '%s'", ko, name.c_str());
                        }
                    }
                    // Try Flamme::ResolveAt directly on every pointer value (maybe key is _Data* not StringName)
                    for (int ko = 0; ko < 0x70; ko += 8) {
                        uint64_t ev = Zoltraak::ReadSafe<uint64_t>(root + ko, 0);
                        if (ev && Zoltraak::IsReadable(ev)) {
                            std::string name = Flamme::ResolveAt(ev);
                            if (!name.empty()) {
                                Solitaer::Debug("WALK", "      Flamme::ResolveAt(val@+0x%02X=%p) = '%s'",
                                    ko, reinterpret_cast<void*>(ev), name.c_str());
                            }
                        }
                    }
                    // Also dump a CHILD element (follow left/right pointers)
                    for (int co = 0x08; co <= 0x18; co += 8) {
                        uint64_t child = Zoltraak::ReadSafe<uint64_t>(root + co, 0);
                        if (child && child != nil && Zoltraak::IsReadable(child) && child != root) {
                            Solitaer::Debug("WALK", "      CHILD elem at %p (via +0x%02X, 0x80 bytes):",
                                reinterpret_cast<void*>(child), co);
                            for (int d = 0; d < 0x80; d += 8) {
                                uint64_t ev = Zoltraak::ReadSafe<uint64_t>(child + d, 0);
                                Solitaer::Debug("WALK", "        +0x%02X: %016llX%s",
                                    d, ev, Zoltraak::IsReadable(ev) ? " (ptr)" : "");
                            }
                            // Try Flamme on child element too
                            for (int ko = 0; ko < 0x70; ko += 8) {
                                std::string name = Flamme::Resolve(child + ko);
                                if (!name.empty()) {
                                    Solitaer::Debug("WALK", "      child Flamme::Resolve(+0x%02X) = '%s'", ko, name.c_str());
                                }
                            }
                            for (int ko = 0; ko < 0x70; ko += 8) {
                                uint64_t ev = Zoltraak::ReadSafe<uint64_t>(child + ko, 0);
                                if (ev && Zoltraak::IsReadable(ev)) {
                                    std::string name = Flamme::ResolveAt(ev);
                                    if (!name.empty()) {
                                        Solitaer::Debug("WALK", "      child Flamme::ResolveAt(val@+0x%02X=%p) = '%s'",
                                            ko, reinterpret_cast<void*>(ev), name.c_str());
                                    }
                                }
                            }
                            break; // Only dump first child
                        }
                    }
                }
            }
        }

        for (int off = 0x40; off <= 0x600; off += 8) {
            uint64_t root  = Zoltraak::ReadSafe<uint64_t>(gdscriptAddr + off, 0);
            uint64_t nil   = Zoltraak::ReadSafe<uint64_t>(gdscriptAddr + off + 0x08, 0);
            uint32_t count = Zoltraak::ReadSafe<uint32_t>(gdscriptAddr + off + 0x10, 0);

            if (count < 1 || count > 200) continue;
            if (!root || !nil) continue;
            if (root == nil) continue;  // empty tree
            if (!Zoltraak::IsReadable(root)) continue;

            // Try each Element layout variant
            for (const auto& lay : mapLayouts) {
                // Validate: try root element key first; if empty, try child elements
                // (Godot 3.x root can have empty key after red-black tree rotations)
                uint64_t validationElem = root;
                std::string validName;
                int32_t validIdx = -1;

                // Try root first
                validName = Flamme::Resolve(root + lay.keyOff);
                if (!validName.empty() && validName.size() <= 64) {
                    validIdx = Zoltraak::ReadSafe<int32_t>(root + lay.valOff, -1);
                }

                // If root key empty, try left child, then right child
                if (validName.empty() || validIdx < 0 || validIdx >= static_cast<int>(count)) {
                    // Try left child (at leftOff)
                    uint64_t leftChild = Zoltraak::ReadSafe<uint64_t>(root + lay.leftOff, 0);
                    if (leftChild && leftChild != nil && Zoltraak::IsReadable(leftChild)) {
                        validName = Flamme::Resolve(leftChild + lay.keyOff);
                        if (!validName.empty() && validName.size() <= 64) {
                            validIdx = Zoltraak::ReadSafe<int32_t>(leftChild + lay.valOff, -1);
                            validationElem = leftChild;
                        }
                    }
                }
                // Also try right child (at offset that could be right: for color-first, right=+0x08)
                if (validName.empty() || validIdx < 0 || validIdx >= static_cast<int>(count)) {
                    // For color-first layout, right is at +0x08; for ptrs-first, right is at +0x08
                    int rightOff = (lay.leftOff == 0x10) ? 0x08 : 0x08;
                    uint64_t rightChild = Zoltraak::ReadSafe<uint64_t>(root + rightOff, 0);
                    if (rightChild && rightChild != nil && Zoltraak::IsReadable(rightChild)) {
                        validName = Flamme::Resolve(rightChild + lay.keyOff);
                        if (!validName.empty() && validName.size() <= 64) {
                            validIdx = Zoltraak::ReadSafe<int32_t>(rightChild + lay.valOff, -1);
                            validationElem = rightChild;
                        }
                    }
                }

                if (validName.empty() || validName.size() > 64) continue;
                if (validIdx < 0 || validIdx >= static_cast<int>(count)) continue;

                Solitaer::Debug("WALK", "  Map candidate at GDScript+0x%X [%s]: root=%p nil=%p count=%u validKey='%s' validIdx=%d",
                    off, lay.tag, reinterpret_cast<void*>(root), reinterpret_cast<void*>(nil), count,
                    validName.c_str(), validIdx);

                // Find leftmost element by following left pointers
                uint64_t cur = root;
                for (int i = 0; i < 30; i++) {
                    uint64_t left = Zoltraak::ReadSafe<uint64_t>(cur + lay.leftOff, 0);
                    if (!left || left == nil || !Zoltraak::IsReadable(left)) break;
                    cur = left;
                }

                // Walk _next chain to traverse all elements in order
                int walked = 0;
                while (cur && cur != nil && walked < static_cast<int>(count) + 5) {
                    if (!Zoltraak::IsReadable(cur)) break;
                    std::string name = Flamme::Resolve(cur + lay.keyOff);
                    int32_t idx = Zoltraak::ReadSafe<int32_t>(cur + lay.valOff, -1);
                    if (!name.empty() && idx >= 0 && idx < 200) {
                        indexToName[idx] = name;
                    }
                    uint64_t next = Zoltraak::ReadSafe<uint64_t>(cur + lay.nextOff, 0);
                    if (next == cur || next == 0) break;
                    cur = next;
                    walked++;
                }

                if (!indexToName.empty()) {
                    Solitaer::Info("WALK", "  GDScript member_indices (Map/%s): %d/%u names at GDScript+0x%X",
                        lay.tag, static_cast<int>(indexToName.size()), count, off);
                    break;
                }
                // Clear for next layout attempt
                indexToName.clear();
            }
            if (!indexToName.empty()) break;
        }

        // Fallback: try HashMap layout (some 3.x forks may use HashMap instead of Map)
        if (indexToName.empty()) {
            Solitaer::Debug("WALK", "  Map scan failed, trying HashMap fallback for 3.x");
            for (int off = 0x40; off <= 0x600; off += 8) {
                uint64_t ht    = Zoltraak::ReadSafe<uint64_t>(gdscriptAddr + off, 0);
                uint8_t  pw    = Zoltraak::ReadSafe<uint8_t>(gdscriptAddr + off + 0x08, 0);
                uint32_t count = Zoltraak::ReadSafe<uint32_t>(gdscriptAddr + off + 0x0C, 0);
                if (!ht || !Zoltraak::IsReadable(ht)) continue;
                if (pw < 1 || pw > 12 || count < 1 || count > 200) continue;

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
                    Solitaer::Info("WALK", "  GDScript member_indices (HashMap): %d names at +0x%X",
                        static_cast<int>(indexToName.size()), off);
                    break;
                }
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
static void ProbeVariantStride(uint64_t membersPtr, uint32_t count, bool force = false) {
    static bool sProbed = false;
    if (sProbed && !force) return;
    sProbed = true;

    const int candidates[] = { 0x18, 0x20, 0x28 };  // float=24, padded=32, double=40
    int maxType = GodotVer::VariantTypeMax();
    int probeCount = static_cast<int>((count < 20) ? count : 20);
    int bestStride = DynOff::VARIANT_STRIDE;
    int bestValid  = -1;
    int defaultValid = -1;  // track how the current default scores

    for (int stride : candidates) {
        int validCount = 0;
        for (int i = 0; i < probeCount; i++) {
            int32_t t = Zoltraak::ReadSafe<int32_t>(membersPtr + i * stride, -1);
            if (t >= 0 && t < maxType) validCount++;
        }
        Solitaer::Info("WALK", "ProbeVariantStride: stride=0x%X → %d/%d valid types",
            stride, validCount, probeCount);
        if (stride == DynOff::VARIANT_STRIDE) defaultValid = validCount;
        if (validCount > bestValid) {
            bestValid  = validCount;
            bestStride = stride;
        }
    }

    // Only override the default stride if the evidence is STRONG:
    // 1. probeCount >= 4 for reliability
    // 2. Default stride must be clearly broken (< 40% valid)
    //    OR the new stride must win by a significant margin (>= 3 AND > 25% better)
    // A margin of 1-2 with 20 probes is noise from reading past array bounds.
    if (bestStride != DynOff::VARIANT_STRIDE) {
        int margin = bestValid - defaultValid;
        int minMargin = (std::max)(3, probeCount / 5);  // at least 3 or 20% of probes
        bool defaultBroken = (defaultValid < probeCount * 2 / 5);  // < 40% valid
        bool strongWin = (margin >= minMargin);

        if (probeCount >= 4 && (defaultBroken || strongWin)) {
            Solitaer::Info("WALK", "ProbeVariantStride: adjusting VARIANT_STRIDE from 0x%X to 0x%X "
                "(%d/%d valid, margin=%d, defaultBroken=%d)",
                DynOff::VARIANT_STRIDE, bestStride, bestValid, probeCount,
                margin, defaultBroken ? 1 : 0);
            DynOff::VARIANT_STRIDE = bestStride;
        } else {
            Solitaer::Info("WALK", "ProbeVariantStride: keeping default stride 0x%X (margin=%d < %d, "
                "default=%d/%d not broken; best=0x%X %d/%d)",
                DynOff::VARIANT_STRIDE, margin, minMargin,
                defaultValid, probeCount, bestStride, bestValid, probeCount);
        }
    } else {
        Solitaer::Info("WALK", "ProbeVariantStride: confirmed stride 0x%X (%d/%d valid)",
            bestStride, bestValid, probeCount);
    }
}

// ============================================================
// ReadCSharpManagedFields — Read .NET managed object fields
// ============================================================
// For .NET/C# Godot games, the chain is:
//   GodotObject[+ScriptInstance] → CSharpInstance → GCHandle → .NET managed object
// This function resolves the chain and emits fields from the managed object.
// When Metode (CLR bridge) is active, uses reflection for real field names/types.
// Otherwise falls back to heuristic offset-based scanning.

// MethodTable → TypeName resolution via Himmel cache
static std::string ResolveManagedTypeName(uint64_t methodTable) {
    Zolt::Himmel::EnsureMTCache();
    return Zolt::Himmel::LookupMTTypeName(methodTable);
}

// Map C# type name to Variant::Type for display
static int CSharpTypeToVariantType(const std::string& typeName) {
    if (typeName == "System.Int32" || typeName == "System.Int64" ||
        typeName == "System.Int16" || typeName == "System.Byte" ||
        typeName == "System.SByte" || typeName == "System.UInt32" ||
        typeName == "System.UInt64" || typeName == "System.UInt16")
        return 2; // INT
    if (typeName == "System.Single" || typeName == "System.Double")
        return 3; // FLOAT
    if (typeName == "System.Boolean")
        return 1; // BOOL
    if (typeName == "System.String" || typeName == "Godot.StringName")
        return 4; // STRING
    return 0; // NIL (unknown — will show raw hex)
}

static void ReadCSharpManagedFields(uint64_t objAddr,
                                     const std::function<void(const FieldEntry& entry)>& callback) {
    if (!DynOff::bIsDotNetGame.load(std::memory_order_acquire)) return;
    if (DynOff::OBJECT_SCRIPT_INSTANCE < 0) return;

    uint64_t scriptInst = Zoltraak::ReadSafe<uint64_t>(
        objAddr + DynOff::OBJECT_SCRIPT_INSTANCE, 0);
    if (!scriptInst || !Zoltraak::IsReadable(scriptInst)) return;

    // Verify this is NOT a GDScriptInstance (owner at GDScript offset would match)
    uint64_t ownerAtGdsi = Zoltraak::ReadSafe<uint64_t>(
        scriptInst + DynOff::GDSCRIPT_OWNER_OFF, 0);
    if (ownerAtGdsi == objAddr) return; // GDScriptInstance — handled by ReadGDScriptMembers

    // Verify CSharpInstance owner at +0x08 matches our object
    uint64_t csOwner = Zoltraak::ReadSafe<uint64_t>(
        scriptInst + DynOff::CSHARP_OWNER_OFF, 0);
    if (csOwner != objAddr) {
        Solitaer::Debug("WALK", "ReadCSharpManagedFields: SI at %p owner mismatch (%p != %p)",
            reinterpret_cast<void*>(scriptInst), reinterpret_cast<void*>(csOwner),
            reinterpret_cast<void*>(objAddr));
        return;
    }

    // Read GCHandle — pointer into CLR handle table
    uint64_t gcHandle = Zoltraak::ReadSafe<uint64_t>(
        scriptInst + DynOff::CSHARP_GCHANDLE_OFF, 0);
    if (!gcHandle || !Zoltraak::IsReadable(gcHandle)) {
        Solitaer::Debug("WALK", "ReadCSharpManagedFields: no GCHandle at SI+0x%X for obj %p",
            DynOff::CSHARP_GCHANDLE_OFF, reinterpret_cast<void*>(objAddr));
        return;
    }

    // Dereference GCHandle → .NET managed object address
    uint64_t managedObj = Zoltraak::ReadSafe<uint64_t>(gcHandle, 0);
    if (!managedObj || (managedObj & 0x7) != 0 || !Zoltraak::IsReadable(managedObj)) {
        Solitaer::Debug("WALK", "ReadCSharpManagedFields: GCHandle deref failed (gcHandle=%p → %p)",
            reinterpret_cast<void*>(gcHandle), reinterpret_cast<void*>(managedObj));
        return;
    }

    // Validate first qword = MethodTable pointer (should be in module range)
    uint64_t methodTable = Zoltraak::ReadSafe<uint64_t>(managedObj, 0);
    if (!methodTable || (methodTable & 0x7) != 0 || (methodTable >> 40) < 0x7F) {
        Solitaer::Debug("WALK", "ReadCSharpManagedFields: invalid MethodTable at managed %p (mt=%p)",
            reinterpret_cast<void*>(managedObj), reinterpret_cast<void*>(methodTable));
        return;
    }

    Solitaer::Info("WALK", "ReadCSharpManagedFields: obj=%p SI=%p GCHandle=%p managed=%p MT=%p",
        reinterpret_cast<void*>(objAddr), reinterpret_cast<void*>(scriptInst),
        reinterpret_cast<void*>(gcHandle), reinterpret_cast<void*>(managedObj),
        reinterpret_cast<void*>(methodTable));

    // ============================================================
    // Strategy 1: Metode reflection (real field names + types)
    // ============================================================
    const std::string& typeName = ResolveManagedTypeName(methodTable);
    if (!typeName.empty()) {
        auto layout = Zolt::Himmel::GetTypeLayoutCached(typeName);
        if (!layout.fields.empty()) {
            Solitaer::Info("WALK", "ReadCSharpManagedFields: Metode resolved '%s' — %zu fields",
                typeName.c_str(), layout.fields.size());

            // Read managed object body for value extraction
            constexpr int MAX_READ = 0x400;
            int readSize = (layout.instanceSize > 0 && layout.instanceSize <= MAX_READ)
                           ? layout.instanceSize : MAX_READ;
            std::vector<uint8_t> buf(readSize);
            if (!Zoltraak::ReadSafe(managedObj, buf.data(), readSize)) {
                Solitaer::Debug("WALK", "ReadCSharpManagedFields: failed to read managed body at %p",
                    reinterpret_cast<void*>(managedObj));
                return;
            }

            int fieldCount = 0;
            for (auto& fi : layout.fields) {
                if (fi.isStatic) continue;  // Skip static fields
                if (fi.offset < 0) continue; // No offset resolved

                FieldEntry entry;
                entry.className = typeName;
                entry.offset = fi.offset;
                entry.nativeSize = fi.size > 0 ? fi.size : 4;
                entry.name = fi.name;
                entry.value.rawAddr = managedObj + fi.offset;

                // Read value based on size and type
                if (fi.offset + fi.size > readSize) {
                    entry.value.type = 0;
                    entry.value.typeName = fi.typeName;
                    entry.value.value = "(out of range)";
                } else if (!fi.isValueType && fi.size == 8) {
                    // Reference type field — pointer to another .NET object
                    uint64_t refVal;
                    memcpy(&refVal, buf.data() + fi.offset, sizeof(refVal));
                    entry.value.type = 24; // Object
                    entry.nativeSize = 8;
                    entry.value.typeName = fi.typeName;
                    if (refVal != 0 && Zoltraak::IsReadable(refVal)) {
                        char valBuf[256];
                        snprintf(valBuf, sizeof(valBuf), "%s@%llX",
                            fi.typeName.c_str(), static_cast<unsigned long long>(refVal));
                        entry.value.value = valBuf;
                        entry.value.ptrTarget = refVal;
                    } else {
                        entry.value.value = refVal == 0 ? "null" : "(invalid)";
                    }
                } else if (fi.size == 1) {
                    uint8_t val1 = buf[fi.offset];
                    if (fi.typeName == "System.Boolean") {
                        entry.value.type = 1; // BOOL
                        entry.value.typeName = "bool";
                        entry.value.value = val1 ? "true" : "false";
                    } else {
                        entry.value.type = 2;
                        entry.value.typeName = fi.typeName;
                        entry.value.value = std::to_string(val1);
                    }
                } else if (fi.size == 2) {
                    uint16_t val2;
                    memcpy(&val2, buf.data() + fi.offset, sizeof(val2));
                    entry.value.type = 2;
                    entry.value.typeName = fi.typeName;
                    if (fi.typeName == "System.UInt16" || fi.typeName == "System.Char")
                        entry.value.value = std::to_string(val2);
                    else
                        entry.value.value = std::to_string(static_cast<int16_t>(val2));
                } else if (fi.size == 4) {
                    uint32_t val4;
                    memcpy(&val4, buf.data() + fi.offset, sizeof(val4));

                    if (fi.typeName == "System.Single") {
                        float fval;
                        memcpy(&fval, &val4, sizeof(fval));
                        entry.value.type = 3;
                        entry.value.typeName = "float";
                        char fmtBuf[32];
                        snprintf(fmtBuf, sizeof(fmtBuf), "%.4f", fval);
                        entry.value.value = fmtBuf;
                    } else if (fi.typeName == "System.Boolean") {
                        entry.value.type = 1;
                        entry.value.typeName = "bool";
                        entry.value.value = val4 ? "true" : "false";
                    } else {
                        int vt = CSharpTypeToVariantType(fi.typeName);
                        entry.value.type = vt ? vt : 2;
                        entry.value.typeName = fi.typeName;
                        entry.value.value = std::to_string(static_cast<int32_t>(val4));
                    }
                } else if (fi.size == 8 && fi.isValueType) {
                    uint64_t val8;
                    memcpy(&val8, buf.data() + fi.offset, sizeof(val8));

                    if (fi.typeName == "System.Double") {
                        double dval;
                        memcpy(&dval, &val8, sizeof(dval));
                        entry.value.type = 3;
                        entry.value.typeName = "double";
                        char fmtBuf[32];
                        snprintf(fmtBuf, sizeof(fmtBuf), "%.4f", dval);
                        entry.value.value = fmtBuf;
                    } else if (fi.typeName == "System.Int64") {
                        entry.value.type = 2;
                        entry.value.typeName = "long";
                        entry.value.value = std::to_string(static_cast<int64_t>(val8));
                    } else {
                        entry.value.type = 2;
                        entry.value.typeName = fi.typeName;
                        entry.value.value = std::to_string(val8);
                    }
                } else {
                    // Struct or unknown size — show as hex
                    entry.value.type = 0;
                    entry.value.typeName = fi.typeName;
                    char hexBuf[128];
                    hexBuf[0] = '\0'; // ensure null-terminated even when fi.size == 0
                    int hexLen = 0;
                    int showBytes = (fi.size < 32) ? fi.size : 32;
                    for (int b = 0; b < showBytes && fi.offset + b < readSize; b++)
                        hexLen += snprintf(hexBuf + hexLen, sizeof(hexBuf) - hexLen,
                            "%02X", buf[fi.offset + b]);
                    if (fi.size > 32) hexLen += snprintf(hexBuf + hexLen,
                        sizeof(hexBuf) - hexLen, "...");
                    entry.value.value = hexBuf;
                }

                callback(entry);
                fieldCount++;
            }

            Solitaer::Info("WALK",
                "ReadCSharpManagedFields: Metode emitted %d fields for '%s' (obj %p)",
                fieldCount, typeName.c_str(), reinterpret_cast<void*>(objAddr));
            return; // Done — Metode provided field info
        }
    }

    // ============================================================
    // Strategy 2: Heuristic scan (fallback when Metode unavailable)
    // ============================================================
    constexpr int MANAGED_SCAN_SIZE = 0x200; // Scan first 512 bytes
    constexpr int FIELD_START = 0x08;        // Fields start after MethodTable pointer

    uint8_t buf[MANAGED_SCAN_SIZE];
    if (!Zoltraak::ReadSafe(managedObj, buf, MANAGED_SCAN_SIZE)) {
        Solitaer::Debug("WALK", "ReadCSharpManagedFields: failed to read managed body at %p",
            reinterpret_cast<void*>(managedObj));
        return;
    }

    // Try to read BaseSize from MethodTable for precise scan range.
    // CoreCLR layout: MethodTable+0x04 = m_BaseSize (uint32_t)
    uint32_t baseSize = Zoltraak::ReadSafe<uint32_t>(methodTable + 0x04, 0);
    int scanEnd = MANAGED_SCAN_SIZE;
    if (baseSize >= 0x10 && baseSize <= 0x2000) {
        scanEnd = static_cast<int>(baseSize);
        if (scanEnd > MANAGED_SCAN_SIZE) scanEnd = MANAGED_SCAN_SIZE;
        Solitaer::Debug("WALK", "ReadCSharpManagedFields: MT BaseSize=0x%X, scanning %d bytes",
            baseSize, scanEnd);
    }

    // Heuristic field emission: scan with 4-byte steps from +0x08 onward.
    int fieldCount = 0;
    for (int off = FIELD_START; off + 4 <= scanEnd; ) {
        FieldEntry entry;
        entry.className = "(managed)";
        entry.offset = off;
        entry.value.rawAddr = managedObj + off;

        char nameBuf[64];
        snprintf(nameBuf, sizeof(nameBuf), "+0x%02X", off);
        entry.name = nameBuf;

        // At 8-byte-aligned offsets, check if this is a pointer (8 bytes)
        bool emittedPtr = false;
        if ((off & 0x7) == 0 && off + 8 <= scanEnd) {
            uint64_t val8;
            memcpy(&val8, buf + off, sizeof(val8));

            bool isPointer = (val8 != 0) && ((val8 & 0x7) == 0) && ((val8 >> 40) != 0)
                             && Zoltraak::IsReadable(val8);

            if (isPointer) {
                uint64_t childMt = Zoltraak::ReadSafe<uint64_t>(val8, 0);
                bool isNetObj = (childMt != 0) && ((childMt & 0x7) == 0)
                                && ((childMt >> 40) >= 0x7F) && Zoltraak::IsReadable(childMt);

                entry.value.type = 24; // Object
                entry.nativeSize = 8;
                char valBuf[64];
                snprintf(valBuf, sizeof(valBuf), "Object@%llX", static_cast<unsigned long long>(val8));
                entry.value.value = valBuf;
                entry.value.ptrTarget = val8;
                entry.name += isNetObj ? " (ref)" : " (ptr)";
                entry.value.typeName = VariantTypeName(entry.value.type);
                callback(entry);
                fieldCount++;
                off += 8; // Pointer consumed 8 bytes
                emittedPtr = true;
            }
        }

        if (!emittedPtr) {
            // Read as 4-byte value (int, float, bool, enum)
            uint32_t val4;
            memcpy(&val4, buf + off, sizeof(val4));

            entry.nativeSize = 4;

            // Heuristic: float detection
            float fval;
            memcpy(&fval, &val4, sizeof(fval));
            bool isLikelyFloat = (val4 != 0) && !std::isinf(fval) && !std::isnan(fval)
                                 && std::abs(fval) > 1e-10f && std::abs(fval) < 1e10f
                                 && (val4 & 0x7F800000) != 0 && (val4 & 0x7F800000) != 0x7F800000;

            if (isLikelyFloat && (val4 > 0xFFFF || (val4 & 0xFF000000) != 0)) {
                entry.value.type = 3; // FLOAT
                char fmtBuf[32];
                snprintf(fmtBuf, sizeof(fmtBuf), "%.4f", fval);
                entry.value.value = fmtBuf;
            } else {
                entry.value.type = 2; // INT
                entry.value.value = std::to_string(static_cast<int32_t>(val4));
            }

            entry.value.typeName = VariantTypeName(entry.value.type);
            callback(entry);
            fieldCount++;
            off += 4;
        }
    }

    Solitaer::Info("WALK", "ReadCSharpManagedFields: emitted %d fields for obj %p (managed scan %d bytes)",
        fieldCount, reinterpret_cast<void*>(objAddr), scanEnd);
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

    // Read member names FIRST — used to cross-validate CowData count
    auto nameMap = ReadGDScriptMemberNames(gdscriptPtr);

    // Determine expected member count from member_indices (ground truth)
    int expectedCount = 0;
    for (auto& kv : nameMap) {
        if (kv.first + 1 > expectedCount) expectedCount = kv.first + 1;
    }

    // Read count from CowData header (before data pointer)
    // Standard Godot 4.x: [rc(4) + pad(4)] + [size(8)] + [data...]
    //   → size at ptr-8 (read as uint32_t = low 32 bits)
    // Custom builds may reorder: [size(8)] + [rc(4) + pad(4)] + [data...]
    //   → size at ptr-16, rc at ptr-8
    // Godot 3.x: uint32_t at ptr-4
    uint32_t count = 0;
    int usedSizeOff = 0;
    if (GodotVer::IsGodot3()) {
        count = Zoltraak::ReadSafe<uint32_t>(membersPtr - 4, 0);
        usedSizeOff = 4;
    } else {
        // Try multiple CowData header layouts, prefer one matching nameMap count
        struct { int off; const char* tag; } cowLayouts[] = {
            {8,  "std"},   // Standard: rc(4)+pad(4)+size(8)+data
            {16, "alt1"},  // Alt: size(8)+rc(4)+pad(4)+data
            {4,  "alt2"},  // Alt: size(4)+data (compact header)
            {12, "alt3"},  // Alt: rc(4)+size(4)+pad(4)+data
        };
        for (auto& lay : cowLayouts) {
            uint32_t c = Zoltraak::ReadSafe<uint32_t>(membersPtr - lay.off, 0);
            if (c > 0 && c <= 500) {
                if (count == 0) { count = c; usedSizeOff = lay.off; }
                // Prefer layout that matches nameMap expected count
                if (expectedCount > 0 && static_cast<int>(c) == expectedCount) {
                    count = c;
                    usedSizeOff = lay.off;
                    Solitaer::Info("WALK", "ReadGDScriptMembers: CowData count=%u matched nameMap at ptr-%d (%s)",
                        count, lay.off, lay.tag);
                    break;
                }
            }
        }
    }

    // Cross-validate: if CowData count is much less than nameMap expects,
    // use nameMap count (verified by reading Variant types at those positions)
    if (expectedCount > 0 && static_cast<int>(count) < expectedCount) {
        int variantStride = DynOff::VARIANT_STRIDE;
        int validTypes = 0;
        int maxType = GodotVer::VariantTypeMax();
        for (int i = 0; i < expectedCount; i++) {
            uint64_t varAddr = membersPtr + static_cast<uint64_t>(i) * variantStride;
            int32_t rawType = Zoltraak::ReadSafe<int32_t>(varAddr + DynOff::VARIANT_TYPE, -1);
            if (rawType >= 0 && rawType < maxType) validTypes++;
        }
        if (validTypes >= expectedCount / 2) {
            Solitaer::Info("WALK", "ReadGDScriptMembers: CowData count=%u too low, "
                "nameMap expects %d (verified %d/%d valid types). Overriding.",
                count, expectedCount, validTypes, expectedCount);
            count = static_cast<uint32_t>(expectedCount);
        }
    }

    if (count == 0) {
        Solitaer::Debug("WALK", "ReadGDScriptMembers: members count=0 for obj %p (CowData at %p)",
            reinterpret_cast<void*>(objAddr), reinterpret_cast<void*>(membersPtr));
        return;
    }
    if (count > 500) {
        Solitaer::Warn("WALK", "ReadGDScriptMembers: members count=%u exceeds sanity limit (CowData at %p)",
            count, reinterpret_cast<void*>(membersPtr));
        return;
    }

    // Auto-detect Variant stride. Force re-probe if count is validated (>= 4)
    // since the early probe may have used an unreliable count.
    bool forceProbe = (count >= 4 && !nameMap.empty());
    ProbeVariantStride(membersPtr, count, forceProbe);

    Solitaer::Info("WALK", "GDScript members: %u variables, %d names resolved (membersPtr=%p, stride=0x%X, sizeOff=%d)",
        count, static_cast<int>(nameMap.size()),
        reinterpret_cast<void*>(membersPtr), DynOff::VARIANT_STRIDE, usedSizeOff);
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
        // Not a Godot Object — might be a .NET managed object address.
        // If .NET game, check if the first qword looks like a MethodTable pointer.
        if (DynOff::bIsDotNetGame.load(std::memory_order_acquire)) {
            uint64_t mt = Zoltraak::ReadSafe<uint64_t>(objAddr, 0);
            if (mt && (mt & 0x7) == 0 && (mt >> 40) >= 0x7F && Zoltraak::IsReadable(mt)) {
                Solitaer::Info("WALK", "WalkInstance: %p is not a Godot Object but has MethodTable %p — reading as .NET managed object",
                    reinterpret_cast<void*>(objAddr), reinterpret_cast<void*>(mt));

                // Try Metode reflection first for real field names
                const std::string& mtTypeName = ResolveManagedTypeName(mt);
                if (!mtTypeName.empty()) {
                    auto layout = Zolt::Himmel::GetTypeLayoutCached(mtTypeName);
                    if (!layout.fields.empty()) {
                        Solitaer::Info("WALK", "WalkInstance: Metode resolved '%s' — %zu fields",
                            mtTypeName.c_str(), layout.fields.size());
                        constexpr int MAX_READ = 0x400;
                        int readSize = (layout.instanceSize > 0 && layout.instanceSize <= MAX_READ)
                                       ? layout.instanceSize : MAX_READ;
                        std::vector<uint8_t> dbuf(readSize);
                        if (Zoltraak::ReadSafe(objAddr, dbuf.data(), readSize)) {
                            for (auto& fi : layout.fields) {
                                if (fi.isStatic || fi.offset < 0) continue;
                                FieldEntry entry;
                                entry.className = mtTypeName;
                                entry.offset = fi.offset;
                                entry.nativeSize = fi.size > 0 ? fi.size : 4;
                                entry.name = fi.name;
                                entry.value.rawAddr = objAddr + fi.offset;
                                // Value reading — same logic as ReadCSharpManagedFields Metode path
                                if (fi.offset + fi.size > readSize) {
                                    entry.value.type = 0;
                                    entry.value.typeName = fi.typeName;
                                    entry.value.value = "(out of range)";
                                } else if (!fi.isValueType && fi.size == 8) {
                                    uint64_t refVal;
                                    memcpy(&refVal, dbuf.data() + fi.offset, sizeof(refVal));
                                    entry.value.type = 24;
                                    entry.nativeSize = 8;
                                    entry.value.typeName = fi.typeName;
                                    if (refVal != 0 && Zoltraak::IsReadable(refVal)) {
                                        char valBuf[256];
                                        snprintf(valBuf, sizeof(valBuf), "%s@%llX",
                                            fi.typeName.c_str(), static_cast<unsigned long long>(refVal));
                                        entry.value.value = valBuf;
                                        entry.value.ptrTarget = refVal;
                                    } else {
                                        entry.value.value = refVal == 0 ? "null" : "(invalid)";
                                    }
                                } else if (fi.size == 2) {
                                    uint16_t val2;
                                    memcpy(&val2, dbuf.data() + fi.offset, sizeof(val2));
                                    entry.value.type = 2;
                                    entry.value.typeName = fi.typeName;
                                    if (fi.typeName == "System.UInt16" || fi.typeName == "System.Char")
                                        entry.value.value = std::to_string(val2);
                                    else
                                        entry.value.value = std::to_string(static_cast<int16_t>(val2));
                                } else if (fi.size == 4) {
                                    uint32_t val4;
                                    memcpy(&val4, dbuf.data() + fi.offset, sizeof(val4));
                                    if (fi.typeName == "System.Single") {
                                        float fval; memcpy(&fval, &val4, sizeof(fval));
                                        entry.value.type = 3;
                                        entry.value.typeName = "float";
                                        char fmtBuf[32];
                                        snprintf(fmtBuf, sizeof(fmtBuf), "%.4f", fval);
                                        entry.value.value = fmtBuf;
                                    } else if (fi.typeName == "System.Boolean") {
                                        entry.value.type = 1;
                                        entry.value.typeName = "bool";
                                        entry.value.value = val4 ? "true" : "false";
                                    } else {
                                        entry.value.type = 2;
                                        entry.value.typeName = fi.typeName;
                                        entry.value.value = std::to_string(static_cast<int32_t>(val4));
                                    }
                                } else if (fi.size == 1) {
                                    uint8_t val1 = dbuf[fi.offset];
                                    if (fi.typeName == "System.Boolean") {
                                        entry.value.type = 1; entry.value.typeName = "bool";
                                        entry.value.value = val1 ? "true" : "false";
                                    } else {
                                        entry.value.type = 2; entry.value.typeName = fi.typeName;
                                        entry.value.value = std::to_string(val1);
                                    }
                                } else if (fi.size == 8 && fi.isValueType) {
                                    uint64_t val8;
                                    memcpy(&val8, dbuf.data() + fi.offset, sizeof(val8));
                                    if (fi.typeName == "System.Double") {
                                        double dval; memcpy(&dval, &val8, sizeof(dval));
                                        entry.value.type = 3; entry.value.typeName = "double";
                                        char fmtBuf[32]; snprintf(fmtBuf, sizeof(fmtBuf), "%.4f", dval);
                                        entry.value.value = fmtBuf;
                                    } else {
                                        entry.value.type = 2; entry.value.typeName = fi.typeName;
                                        entry.value.value = std::to_string(static_cast<int64_t>(val8));
                                    }
                                } else {
                                    entry.value.type = 0; entry.value.typeName = fi.typeName;
                                    char hexBuf[128]; hexBuf[0] = '\0'; int hexLen = 0;
                                    int showBytes = (fi.size < 32) ? fi.size : 32;
                                    for (int b = 0; b < showBytes && fi.offset + b < readSize; b++)
                                        hexLen += snprintf(hexBuf + hexLen, sizeof(hexBuf) - hexLen,
                                            "%02X", dbuf[fi.offset + b]);
                                    entry.value.value = hexBuf;
                                }
                                callback(entry);
                            }
                        }
                        return;
                    }
                }

                // Fallback: heuristic scan (no Metode or type not found)
                constexpr int MANAGED_SCAN_SIZE = 0x200;
                constexpr int FIELD_START = 0x08;
                uint8_t buf[MANAGED_SCAN_SIZE];
                if (Zoltraak::ReadSafe(objAddr, buf, MANAGED_SCAN_SIZE)) {
                    uint32_t baseSize = Zoltraak::ReadSafe<uint32_t>(mt + 0x04, 0);
                    int scanEnd = MANAGED_SCAN_SIZE;
                    if (baseSize >= 0x10 && baseSize <= 0x2000) {
                        scanEnd = static_cast<int>(baseSize);
                        if (scanEnd > MANAGED_SCAN_SIZE) scanEnd = MANAGED_SCAN_SIZE;
                    }

                    for (int off = FIELD_START; off + 4 <= scanEnd; ) {
                        FieldEntry entry;
                        entry.className = "(managed)";
                        entry.offset = off;
                        entry.value.rawAddr = objAddr + off;

                        char nameBuf[64];
                        snprintf(nameBuf, sizeof(nameBuf), "+0x%02X", off);
                        entry.name = nameBuf;

                        bool emittedPtr = false;
                        if ((off & 0x7) == 0 && off + 8 <= scanEnd) {
                            uint64_t val8;
                            memcpy(&val8, buf + off, sizeof(val8));
                            bool isPtr = (val8 != 0) && ((val8 & 0x7) == 0) && ((val8 >> 40) != 0)
                                         && Zoltraak::IsReadable(val8);
                            if (isPtr) {
                                uint64_t childMt = Zoltraak::ReadSafe<uint64_t>(val8, 0);
                                bool isNetObj = (childMt != 0) && ((childMt & 0x7) == 0)
                                                && ((childMt >> 40) >= 0x7F) && Zoltraak::IsReadable(childMt);
                                entry.value.type = 24;
                                entry.nativeSize = 8;
                                char valBuf[64];
                                snprintf(valBuf, sizeof(valBuf), "Object@%llX", static_cast<unsigned long long>(val8));
                                entry.value.value = valBuf;
                                entry.value.ptrTarget = val8;
                                entry.name += isNetObj ? " (ref)" : " (ptr)";
                                entry.value.typeName = VariantTypeName(entry.value.type);
                                callback(entry);
                                off += 8;
                                emittedPtr = true;
                            }
                        }
                        if (!emittedPtr) {
                            uint32_t val4;
                            memcpy(&val4, buf + off, sizeof(val4));
                            entry.nativeSize = 4;
                            float fval;
                            memcpy(&fval, &val4, sizeof(fval));
                            bool isFloat = (val4 != 0) && !std::isinf(fval) && !std::isnan(fval)
                                           && std::abs(fval) > 1e-10f && std::abs(fval) < 1e10f
                                           && (val4 & 0x7F800000) != 0 && (val4 & 0x7F800000) != 0x7F800000;
                            if (isFloat && (val4 > 0xFFFF || (val4 & 0xFF000000) != 0)) {
                                entry.value.type = 3;
                                char fmtBuf[32];
                                snprintf(fmtBuf, sizeof(fmtBuf), "%.4f", fval);
                                entry.value.value = fmtBuf;
                            } else {
                                entry.value.type = 2;
                                entry.value.value = std::to_string(static_cast<int32_t>(val4));
                            }
                            entry.value.typeName = VariantTypeName(entry.value.type);
                            callback(entry);
                            off += 4;
                        }
                    }
                }
                return;
            }
        }
        Solitaer::Debug("WALK", "WalkInstance: could not determine class for object at %p",
            reinterpret_cast<void*>(objAddr));
        return;
    }

    // Step 2: Get property list from ClassDB (includes inherited properties)
    auto properties = Diagoldze::GetPropertyList(className.c_str());
    if (properties.empty()) {
        Solitaer::Debug("WALK", "WalkInstance: no ClassDB properties for class '%s', trying GDScript members",
            className.c_str());
        // Don't return — still try GDScript/CSharp members below
        ProbeScriptInstanceOffset(objAddr);
        ReadGDScriptMembers(objAddr, callback);
        ReadCSharpManagedFields(objAddr, callback);
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
        } else {
            Solitaer::Warn("WALK", "WalkInstance: ptrcall dispatch failed (game thread not "
                "pumping messages). %d native properties will show no values. "
                "Script members are unaffected.",
                static_cast<int>(ptrcallJobs.size()));
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

    // Step 6: Read .NET/C# managed object fields (if .NET Godot game detected)
    // This reads raw fields from the managed wrapper object via CSharpInstance → GCHandle chain.
    // Only runs for non-GDScript ScriptInstance objects in .NET Godot games.
    ReadCSharpManagedFields(objAddr, callback);
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
// Ubel — 尤蓓爾 — Early script probe for GDScript member access
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

        // Multi-object vote for GDSCRIPT_MEMBERS_OFF + VARIANT_STRIDE.
        // The single-object probe may pick the wrong offset if the first object
        // has an empty members Vector (NULL at expected offset).
        // Scan ObjectDB for objects with valid ScriptInstances and vote on which
        // offset (+0x20, +0x28, +0x30, +0x38) consistently has valid CowData.
        int sizeOff = GodotVer::IsGodot3() ? 4 : 8;
        int maxType = GodotVer::VariantTypeMax();
        int offVotes[4] = {};  // votes for +0x20, +0x28, +0x30, +0x38
        const int offCandidates[] = {0x20, 0x28, 0x30, 0x38};
        int totalChecked = 0;
        bool strideFound = false;
        uint64_t strideProbePtr = 0;
        uint32_t strideProbeCount = 0;

        Ausetiese::ForEachEx([&](int /*index*/, uint64_t objAddr) {
            if (totalChecked >= 200) return false;
            uint64_t scriptInst = Zoltraak::ReadSafe<uint64_t>(
                objAddr + DynOff::OBJECT_SCRIPT_INSTANCE, 0);
            if (!scriptInst || !Zoltraak::IsReadable(scriptInst)) return true;

            uint64_t owner = Zoltraak::ReadSafe<uint64_t>(
                scriptInst + DynOff::GDSCRIPT_OWNER_OFF, 0);
            if (owner != objAddr) return true;

            totalChecked++;

            // Vote on each candidate offset
            for (int i = 0; i < 4; i++) {
                uint64_t mptr = Zoltraak::ReadSafe<uint64_t>(
                    scriptInst + offCandidates[i], 0);
                if (!mptr || !Zoltraak::IsReadable(mptr) || mptr % 8 != 0) continue;
                // Use STANDARD CowData header offset only.
                // Multi-offset probing without nameMap validation picks garbage counts,
                // causing ProbeVariantStride to read past arrays into random data.
                uint32_t cnt = Zoltraak::ReadSafe<uint32_t>(mptr - sizeOff, 0);
                if (cnt == 0 || cnt > 500) continue;
                int32_t ft = Zoltraak::ReadSafe<int32_t>(mptr, -1);
                if (ft >= 0 && ft < maxType) {
                    offVotes[i]++;
                    // Track best candidate for stride probe (need 4+ members)
                    if (!strideFound && cnt >= 4 && cnt > strideProbeCount) {
                        strideProbePtr = mptr;
                        strideProbeCount = cnt;
                    }
                }
            }
            return true;
        });

        // Pick the offset with most votes
        int bestIdx = 0;
        for (int i = 1; i < 4; i++) {
            if (offVotes[i] > offVotes[bestIdx]) bestIdx = i;
        }
        int votedOff = offCandidates[bestIdx];
        Solitaer::Info("WALK", "  Members offset vote (%d objects): +0x20=%d +0x28=%d +0x30=%d +0x38=%d → +0x%X",
            totalChecked, offVotes[0], offVotes[1], offVotes[2], offVotes[3], votedOff);
        if (votedOff != DynOff::GDSCRIPT_MEMBERS_OFF) {
            Solitaer::Info("WALK", "  Updating GDSCRIPT_MEMBERS_OFF: 0x%X -> 0x%X",
                DynOff::GDSCRIPT_MEMBERS_OFF, votedOff);
            DynOff::GDSCRIPT_MEMBERS_OFF = votedOff;
        }

        // Probe VARIANT_STRIDE if we found an object with 4+ members at the voted offset
        if (strideProbePtr && strideProbeCount >= 4) {
            ProbeVariantStride(strideProbePtr, strideProbeCount);
        } else {
            // For custom builds where CowData header at ptr-8 stores refcount (=1)
            // instead of size, NO object will have count >= 4 from the standard offset.
            // That's OK — stride stays at default 0x18 (confirmed correct for Godot 4.x).
            // The stride will be re-probed with a validated count in ReadGDScriptMembers
            // on the first walk_instance call.
            Solitaer::Info("WALK", "  No object with count>=4 at standard CowData offset, "
                "keeping default stride 0x%X", DynOff::VARIANT_STRIDE);
        }
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
    int scannedTotal = 0, scannedWithScript = 0;
    uint64_t closestDist = UINT64_MAX;
    uint64_t closestObj = 0;
    uint64_t closestPtr = 0;
    uint32_t closestCount = 0;

    Ausetiese::ForEachEx([&](int /*index*/, uint64_t objAddr) {
        scannedTotal++;
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

        scannedWithScript++;

        // Check if addr falls within [membersPtr, membersPtr + count * variantStride)
        uint64_t rangeStart = membersPtr;
        uint64_t rangeEnd   = membersPtr + static_cast<uint64_t>(count) * variantStride;

        if (addr >= rangeStart && addr < rangeEnd) {
            uint64_t displacement = addr - rangeStart;
            result.objectAddr    = objAddr;
            result.memberIndex   = static_cast<int>(displacement / variantStride);
            result.variantOffset = static_cast<int>(displacement % variantStride);

            Solitaer::Debug("WALK", "FindScriptMemberByAddress: %p → Object %p, "
                "member[%d] + 0x%X (members at %p, count=%u, stride=0x%X)",
                reinterpret_cast<void*>(addr),
                reinterpret_cast<void*>(objAddr),
                result.memberIndex, result.variantOffset,
                reinterpret_cast<void*>(membersPtr), count, variantStride);
            return false; // found — stop
        }

        // Track closest range for diagnostic
        uint64_t dist = (addr >= rangeEnd) ? (addr - rangeEnd) :
                        (addr < rangeStart) ? (rangeStart - addr) : 0;
        if (dist < closestDist) {
            closestDist = dist;
            closestObj = objAddr;
            closestPtr = membersPtr;
            closestCount = count;
        }

        return true; // continue
    });

    if (!result.objectAddr) {
        Solitaer::Info("WALK", "FindScriptMemberByAddress: scanned %d objects, %d with scripts, "
            "stride=0x%X. Closest: obj=%p members=%p count=%u dist=0x%llX",
            scannedTotal, scannedWithScript, variantStride,
            reinterpret_cast<void*>(closestObj),
            reinterpret_cast<void*>(closestPtr),
            closestCount, closestDist);
    }

    return result;
}

// Public wrapper for ReadGDScriptMemberNames (used by Qual mailbox)
std::unordered_map<int, std::string> GetMemberNames(uint64_t gdscriptPtr) {
    return ReadGDScriptMemberNames(gdscriptPtr);
}

// ============================================================
// FindContainerOwnerByAddress — Reverse-lookup for addresses inside
// Array/Dictionary containers owned by GDScript members.
// Ubel — 尤蓓爾 (Assassin Specialist) — Deep container owner search
// ============================================================
ContainerOwnerMatch FindContainerOwnerByAddress(uint64_t addr) {
    ContainerOwnerMatch result;
    if (!addr || DynOff::OBJECT_SCRIPT_INSTANCE < 0) return result;

    int variantStride = DynOff::VARIANT_STRIDE;
    int sizeOff = GodotVer::IsGodot3() ? 4 : 8;
    int arraysScanned = 0, dictsScanned = 0;

    s_bulkScanQuiet = true;
    Ausetiese::ForEachEx([&](int /*index*/, uint64_t objAddr) {
        // Read ScriptInstance pointer
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

        uint32_t count = Zoltraak::ReadSafe<uint32_t>(membersPtr - sizeOff, 0);
        if (count == 0 || count > 500) return true;

        // Helper: check if addr is within a Dictionary's linked list elements
        auto checkDict = [&](uint64_t dictDataAddr) -> bool {
            auto di = ReadDictionaryInternals(dictDataAddr);
            dictsScanned++;
            if (di.count <= 0 || !di.head) return false;
            int p = di.elemPrefix;
            size_t elemSize = static_cast<size_t>(p) + 16 + static_cast<size_t>(variantStride) * 2;
            uint64_t cur = di.head;
            int walked = 0;
            int maxWalk = (std::min)(di.count, 1000);
            while (cur && walked < maxWalk) {
                if (addr >= cur && addr < cur + elemSize) return true;
                uint64_t next = Zoltraak::ReadSafe<uint64_t>(cur + p, 0);
                if (next == cur) break;
                cur = next;
                walked++;
            }
            return false;
        };

        // Strategy 1: Type-aware member scan (for correctly detected members)
        for (uint32_t m = 0; m < count; m++) {
            uint64_t varAddr = membersPtr + static_cast<uint64_t>(m) * variantStride;
            int32_t rawType = Zoltraak::ReadSafe<int32_t>(varAddr + DynOff::VARIANT_TYPE, -1);
            int type4x = rawType;
            if (GodotVer::IsGodot3()) type4x = MapType3xTo4x(rawType);
            uint64_t data = varAddr + DynOff::VARIANT_DATA;

            if (type4x == 28) {
                auto [arrCount, cowPtr] = ReadArrayInternals(data);
                arraysScanned++;
                if (arrCount > 0 && cowPtr) {
                    uint64_t rangeEnd = cowPtr + static_cast<uint64_t>(arrCount) * variantStride;
                    if (addr >= cowPtr && addr < rangeEnd) {
                        result.objectAddr = objAddr;
                        result.memberIndex = static_cast<int>(m);
                        result.containerType = "Array";
                        return false;
                    }
                }
            } else if (type4x == 27) {
                if (checkDict(data)) {
                    result.objectAddr = objAddr;
                    result.memberIndex = static_cast<int>(m);
                    result.containerType = "Dictionary";
                    return false;
                }
            }
        }

        // Strategy 2: Raw GDScriptInstance pointer scan.
        // Scan all 8-byte aligned values from +0x20 to +0x80 within the
        // GDScriptInstance for potential Array/Dictionary private pointers.
        // This catches containers at unexpected offsets (native fields,
        // different layouts, or members with wrong stride/type detection).
        for (int off = 0x20; off < 0x80; off += 8) {
            uint64_t rawVal = Zoltraak::ReadSafe<uint64_t>(scriptInst + off, 0);
            if (!rawVal || !Zoltraak::IsReadable(rawVal) || rawVal % 8 != 0) continue;
            // Skip known fields (owner, script)
            if (off == DynOff::GDSCRIPT_OWNER_OFF || off == DynOff::GDSCRIPT_SCRIPT_OFF)
                continue;

            // Try as Dictionary (Variant data field: _p pointer)
            // Variant[DICTIONARY].data = pointer to DictionaryPrivate
            if (checkDict(scriptInst + off)) {
                result.objectAddr = objAddr;
                result.memberIndex = -1;
                result.containerType = "ScriptDict";
                Solitaer::Debug("WALK", "FindContainerOwner: %p in ScriptDict at inst+0x%X of Object %p",
                    reinterpret_cast<void*>(addr), off, reinterpret_cast<void*>(objAddr));
                return false;
            }

            // Try as Array (Variant data field: _p pointer to ArrayPrivate)
            auto [arrCount, cowPtr] = ReadArrayInternals(scriptInst + off);
            if (arrCount > 0 && cowPtr) {
                arraysScanned++;
                uint64_t rangeEnd = cowPtr + static_cast<uint64_t>(arrCount) * variantStride;
                if (addr >= cowPtr && addr < rangeEnd) {
                    result.objectAddr = objAddr;
                    result.memberIndex = -1;
                    result.containerType = "ScriptArray";
                    return false;
                }
            }
        }
        return true; // continue scanning
    });

    s_bulkScanQuiet = false;

    if (!result.objectAddr) {
        Solitaer::Info("WALK", "FindContainerOwner: NOT FOUND. Scanned %d arrays, %d dicts (stride=0x%X)",
            arraysScanned, dictsScanned, variantStride);
    }

    return result;
}

// ============================================================
// FindNativeContainerByAddress — Phase 6: Scan ALL ObjectDB objects' raw
// memory for native Dictionary/Array properties containing the target address.
// Unlike FindContainerOwnerByAddress (Phase 5) which only checks GDScript
// members, this scans the C++ Object body directly for pointer-sized values
// that could be DictionaryPrivate* or ArrayPrivate* holding the target.
// Ubel — 尤蓓爾 (Assassin Specialist) — Native property body scan
// ============================================================
ContainerOwnerMatch FindNativeContainerByAddress(uint64_t addr) {
    ContainerOwnerMatch result;
    if (!addr) return result;

    int variantStride = DynOff::VARIANT_STRIDE;
    int objScanned = 0, dictsHit = 0, arraysHit = 0;
    constexpr int SCAN_SIZE = 0x200; // Scan first 512 bytes of each Object body
    constexpr int PTR_ALIGN = 8;
    constexpr int TIME_BUDGET_SEC = 10; // Abort after 10 seconds
    bool timedOut = false;

    Solitaer::Info("WALK", "FindNativeContainer: scanning ObjectDB bodies for %p (scanSize=0x%X, budget=%ds)",
        reinterpret_cast<void*>(addr), SCAN_SIZE, TIME_BUDGET_SEC);

    auto startTime = std::chrono::steady_clock::now();

    s_bulkScanQuiet = true;
    Ausetiese::ForEachEx([&](int /*index*/, uint64_t objAddr) {
        objScanned++;

        // Check time budget every 1000 objects to avoid chrono overhead per-object
        if ((objScanned & 0x3FF) == 0) {
            auto elapsed = std::chrono::steady_clock::now() - startTime;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= TIME_BUDGET_SEC) {
                timedOut = true;
                return false; // Stop scanning
            }
        }

        // Bulk-read the Object body
        uint8_t buf[SCAN_SIZE];
        if (!Zoltraak::ReadSafe(objAddr, buf, SCAN_SIZE)) return true;

        // Scan 8-byte aligned offsets, skipping vtable at +0
        for (int off = PTR_ALIGN; off + PTR_ALIGN <= SCAN_SIZE; off += PTR_ALIGN) {
            uint64_t val;
            memcpy(&val, buf + off, sizeof(val));

            // Quick filter: must look like a heap pointer (high bits set, aligned)
            if (!val || (val & 0x7) != 0) continue;
            if ((val >> 40) == 0) continue; // Too low to be a user-mode heap address

            // Try as Dictionary: val is a DictionaryPrivate* pointer directly
            // (native Dictionary storage = just the pointer, no Variant wrapper)
            // We construct a fake "data address" on the stack: ReadDictionaryInternals
            // expects the address of a location containing the DictionaryPrivate* pointer.
            // Since we already have the value, write it to a temporary and pass that address.
            // Easier: just pass objAddr + off, which is where the pointer lives in-process.
            {
                auto di = ReadDictionaryInternals(objAddr + off);
                if (di.count > 0 && di.head) {
                    dictsHit++;
                    int p = di.elemPrefix;
                    // Walk the linked list to see if addr falls within an element
                    size_t elemSize = static_cast<size_t>(p) + 16 + static_cast<size_t>(variantStride) * 2;
                    uint64_t cur = di.head;
                    int walked = 0;
                    int maxWalk = (std::min)(di.count, 1000);
                    while (cur && walked < maxWalk) {
                        if (addr >= cur && addr < cur + elemSize) {
                            result.objectAddr = objAddr;
                            result.memberIndex = -1;
                            result.containerType = "NativeDict";
                            Solitaer::Info("WALK",
                                "FindNativeContainer: FOUND in NativeDict at Object %p +0x%X "
                                "(dictCount=%d, elem=%p prefix=%d)",
                                reinterpret_cast<void*>(objAddr), off, di.count,
                                reinterpret_cast<void*>(cur), p);
                            return false; // stop iteration
                        }
                        uint64_t next = Zoltraak::ReadSafe<uint64_t>(cur + p, 0);
                        if (next == cur) break;
                        cur = next;
                        walked++;
                    }
                }
            }

            // Try as Array: val is an ArrayPrivate* pointer directly
            {
                auto [arrCount, cowPtr] = ReadArrayInternals(objAddr + off);
                if (arrCount > 0 && cowPtr) {
                    arraysHit++;
                    uint64_t rangeEnd = cowPtr + static_cast<uint64_t>(arrCount) * variantStride;
                    if (addr >= cowPtr && addr < rangeEnd) {
                        result.objectAddr = objAddr;
                        result.memberIndex = -1;
                        result.containerType = "NativeArray";
                        Solitaer::Info("WALK",
                            "FindNativeContainer: FOUND in NativeArray at Object %p +0x%X "
                            "(arrCount=%d, range=%p..%p)",
                            reinterpret_cast<void*>(objAddr), off, arrCount,
                            reinterpret_cast<void*>(cowPtr),
                            reinterpret_cast<void*>(rangeEnd));
                        return false;
                    }
                }
            }
        }
        return true; // continue
    });

    s_bulkScanQuiet = false;

    Solitaer::Info("WALK",
        "FindNativeContainer: scanned %d objects, validated %d dicts + %d arrays. %s%s",
        objScanned, dictsHit, arraysHit,
        result.objectAddr ? "FOUND" : "NOT FOUND",
        timedOut ? " (TIMED OUT)" : "");

    return result;
}

} // namespace Zolt::Ubel
