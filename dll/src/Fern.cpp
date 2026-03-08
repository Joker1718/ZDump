// Fern — 費倫 (Apprentice — PipeServer)
// Named pipe IPC server for communication with ZoltDumpUI.
// Protocol: newline-delimited JSON over \\.\pipe\ZoltDumpBfx
// See docs/pipe-protocol.md for full command specification.

#define LOG_CAT "PIPE"

#include "Fern.h"
#include "Solliter.h"
#include "Grimoire.h"
#include "Frieren.h"
#include "Ausetiese.h"
#include "DiAgolze.h"
#include "Flamme.h"
#include "Serie.h"
#include "Ubel.h"
#include "Zoltraak.h"
#include "BuildInfo.h"

#include <Windows.h>
#include <atomic>
#include <string>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>

#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Zolt::Fern {

// ============================================================
// State
// ============================================================
static HANDLE            s_hPipe    = INVALID_HANDLE_VALUE;
static HANDLE            s_hThread  = nullptr;
static std::atomic<bool> s_running{false};
static std::atomic<bool> s_connected{false};
static std::mutex        s_writeMtx; // Serialize writes to the pipe

// ============================================================
// Helpers
// ============================================================

// Format a uint64_t address as uppercase hex string (no 0x prefix)
static std::string AddrToHex(uint64_t addr) {
    if (!addr) return "0";
    char buf[17];
    snprintf(buf, sizeof(buf), "%llX", static_cast<unsigned long long>(addr));
    return buf;
}

// Parse a hex string (no 0x prefix) to uint64_t
static uint64_t HexToAddr(const std::string& hex) {
    if (hex.empty()) return 0;
    return strtoull(hex.c_str(), nullptr, 16);
}

// Encode a byte buffer as uppercase hex string
static std::string BytesToHex(const uint8_t* data, size_t len) {
    std::string result;
    result.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02X", data[i]);
        result += buf;
    }
    return result;
}

// Decode a hex string to bytes, returns number of bytes written
static size_t HexToBytes(const std::string& hex, uint8_t* out, size_t maxLen) {
    size_t len = hex.size() / 2;
    if (len > maxLen) len = maxLen;
    for (size_t i = 0; i < len; ++i) {
        unsigned int byte = 0;
        sscanf(hex.c_str() + i * 2, "%2x", &byte);
        out[i] = static_cast<uint8_t>(byte);
    }
    return len;
}

// Build a success response with the echoed ID
static json OkResponse(int id) {
    return json{{"id", id}, {"ok", true}};
}

// Build an error response
static json ErrorResponse(int id, const std::string& msg) {
    return json{{"id", id}, {"ok", false}, {"error", msg}};
}

// Accept both "addr" and "address" from request payloads
static std::string GetAddrField(const json& req) {
    if (req.contains("address")) return req["address"].get<std::string>();
    if (req.contains("addr"))    return req["addr"].get<std::string>();
    return "";
}

// Accept both "hex" and "data" from request payloads
static std::string GetHexField(const json& req) {
    if (req.contains("data")) return req["data"].get<std::string>();
    if (req.contains("hex"))  return req["hex"].get<std::string>();
    return "";
}

// Read object name (StringName) by probing node name offsets
static std::string ReadObjectName(uint64_t objAddr) {
    if (!objAddr) return "";

    if (DynOff::NODE_NAME >= 0) {
        std::string name = Flamme::Resolve(objAddr + DynOff::NODE_NAME);
        if (!name.empty()) return name;
    }

    for (int off = 0x80; off <= 0x300; off += 8) {
        uint64_t candidate = Zoltraak::ReadSafe<uint64_t>(objAddr + off, 0);
        if (!candidate || candidate < 0x10000) continue;
        if (!Zoltraak::IsReadable(candidate)) continue;
        std::string name = Flamme::ResolveAt(candidate);
        if (!name.empty() && name.size() >= 1 && name.size() <= 64) {
            return name;
        }
    }

    return "";
}

// Godot Variant::Type enum to string name mapping
static const char* VariantTypeName(int type) {
    static const char* names[] = {
        "Nil",            // 0
        "bool",           // 1
        "int",            // 2
        "float",          // 3
        "String",         // 4
        "Vector2",        // 5
        "Vector2i",       // 6
        "Rect2",          // 7
        "Rect2i",         // 8
        "Vector3",        // 9
        "Vector3i",       // 10
        "Transform2D",    // 11
        "Vector4",        // 12
        "Vector4i",       // 13
        "Plane",          // 14
        "Quaternion",     // 15
        "AABB",           // 16
        "Basis",          // 17
        "Transform3D",    // 18
        "Projection",     // 19
        "Color",          // 20
        "StringName",     // 21
        "NodePath",       // 22
        "RID",            // 23
        "Object",         // 24
        "Callable",       // 25
        "Signal",         // 26
        "Dictionary",     // 27
        "Array",          // 28
        "PackedByteArray",    // 29
        "PackedInt32Array",   // 30
        "PackedInt64Array",   // 31
        "PackedFloat32Array", // 32
        "PackedFloat64Array", // 33
        "PackedStringArray",  // 34
        "PackedVector2Array", // 35
        "PackedVector3Array", // 36
        "PackedColorArray",   // 37
        "PackedVector4Array", // 38
    };
    if (type >= 0 && type < static_cast<int>(std::size(names)))
        return names[type];
    return "Unknown";
}

// ============================================================
// Command Handlers
// ============================================================
using HandlerFn = std::function<json(int id, const json& req)>;

// cmd: init
static json HandleInit(int id, const json& /*req*/) {
    const auto& ptrs = Frieren::GetPointers();

    json resp = OkResponse(id);
    resp["godot_version"]       = ptrs.godotVersion;
    resp["godot_version_major"] = ptrs.godotMajor;
    resp["godot_version_minor"] = ptrs.godotMinor;
    resp["godot_version_patch"] = ptrs.godotPatch;

    // Module info
    wchar_t processPath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, processPath, MAX_PATH);
    char modulePathUtf8[MAX_PATH] = {};
    WideCharToMultiByte(CP_UTF8, 0, processPath, -1, modulePathUtf8, MAX_PATH, nullptr, nullptr);

    // Extract process name (filename without extension)
    std::string procName = fs::path(modulePathUtf8).stem().string();

    resp["process_name"] = procName;
    resp["module_name"]  = fs::path(modulePathUtf8).filename().string();
    resp["module_path"]  = modulePathUtf8;
    resp["module_base"]  = AddrToHex(Zoltraak::GetModuleBase());
    resp["pid"]          = static_cast<int>(GetCurrentProcessId());

    LOG_INFO("cmd:init -> version=%s pid=%d", ptrs.godotVersion.c_str(), GetCurrentProcessId());
    return resp;
}

// cmd: get_pointers
static json HandleGetPointers(int id, const json& /*req*/) {
    const auto& ptrs = Frieren::GetPointers();

    json resp = OkResponse(id);
    resp["object_db"]   = AddrToHex(ptrs.ObjectDB);
    resp["class_db"]    = AddrToHex(ptrs.ClassDB);
    resp["scene_tree"]  = AddrToHex(ptrs.SceneTree);
    resp["engine"]      = AddrToHex(ptrs.Engine);

    // Flat fields (UI expects these at top level)
    resp["slot_max"]     = Ausetiese::GetSlotMax();
    resp["slot_count"]   = Ausetiese::GetSlotMax(); // slot_count ~ slot_max
    resp["object_count"] = Ausetiese::GetObjectCount();

    // Scan methods and pattern IDs (flat, not nested)
    resp["object_db_method"]     = ptrs.objectDBMethod;
    resp["class_db_method"]      = ptrs.classDBMethod;
    resp["scene_tree_method"]    = ptrs.sceneTreeMethod;
    resp["object_db_pattern_id"]   = ptrs.objectDBPatternId ? ptrs.objectDBPatternId : "";
    resp["class_db_pattern_id"]    = ptrs.classDBPatternId ? ptrs.classDBPatternId : "";
    resp["scene_tree_pattern_id"]  = ptrs.sceneTreePatternId ? ptrs.sceneTreePatternId : "";
    resp["object_db_scan_addr"]    = AddrToHex(ptrs.ObjectDB);
    resp["class_db_scan_addr"]     = AddrToHex(ptrs.ClassDB);
    resp["scene_tree_scan_addr"]   = AddrToHex(ptrs.SceneTree);

    return resp;
}

// cmd: get_object_count
static json HandleGetObjectCount(int id, const json& /*req*/) {
    json resp = OkResponse(id);
    resp["count"]    = Ausetiese::GetObjectCount();
    resp["slot_max"] = Ausetiese::GetSlotMax();
    return resp;
}

// cmd: get_offsets
static json HandleGetOffsets(int id, const json& /*req*/) {
    json resp = OkResponse(id);

    json offsets;
    offsets["OBJECT_VTABLE"]         = DynOff::OBJECT_VTABLE;
    offsets["OBJECT_EXTENSION"]      = DynOff::OBJECT_EXTENSION;
    offsets["OBJECT_EXT_INSTANCE"]   = DynOff::OBJECT_EXT_INSTANCE;
    offsets["OBJECT_INSTANCE_ID"]    = DynOff::OBJECT_INSTANCE_ID;
    offsets["OBJECT_SCRIPT_INSTANCE"]= DynOff::OBJECT_SCRIPT_INSTANCE;
    offsets["SLOT_STRIDE"]           = DynOff::SLOT_STRIDE;
    offsets["SLOT_OBJECT_OFFSET"]    = DynOff::SLOT_OBJECT_OFFSET;
    offsets["NODE_DATA_OFFSET"]      = DynOff::NODE_DATA_OFFSET;
    offsets["NODE_PARENT"]           = DynOff::NODE_PARENT;
    offsets["NODE_CHILDREN"]         = DynOff::NODE_CHILDREN;
    offsets["NODE_NAME"]             = DynOff::NODE_NAME;
    offsets["NODE_TREE"]             = DynOff::NODE_TREE;
    offsets["VARIANT_TYPE"]          = DynOff::VARIANT_TYPE;
    offsets["VARIANT_DATA"]          = DynOff::VARIANT_DATA;
    offsets["STRINGNAME_DATA_PTR"]   = DynOff::STRINGNAME_DATA_PTR;
    offsets["SDATA_CNAME"]           = DynOff::SDATA_CNAME;
    offsets["SDATA_NAME"]            = DynOff::SDATA_NAME;
    offsets["SDATA_HASH"]            = DynOff::SDATA_HASH;
    offsets["CLASSDB_ALLOC_OFFSET"]  = DynOff::CLASSDB_ALLOC_OFFSET;
    offsets["STREE_ROOT_OFFSET"]     = DynOff::STREE_ROOT_OFFSET;
    offsets["STREE_SCENE_OFFSET"]    = DynOff::STREE_SCENE_OFFSET;
    offsets["validated"]             = DynOff::bOffsetsValidated.load();
    offsets["node_validated"]        = DynOff::bNodeOffsetsValidated.load();
    offsets["stringname_probed"]     = DynOff::bStringNameProbed.load();

    resp["offsets"] = offsets;
    return resp;
}

// cmd: get_object_list
static json HandleGetObjectList(int id, const json& req) {
    int offset = req.value("offset", 0);
    int limit  = req.value("limit", 200);
    if (limit <= 0) limit = 200;
    if (limit > 5000) limit = 5000;

    json objects = json::array();
    int total   = Ausetiese::GetSlotMax();
    int scanned = 0;
    int added   = 0;

    Ausetiese::ForEachEx([&](int index, uint64_t objAddr) {
        scanned++;
        if (index < offset) return true;
        if (added >= limit) return false;

        std::string name      = ReadObjectName(objAddr);
        std::string className = DiAgolze::GetObjectClass(objAddr);

        uint64_t objId = Zoltraak::ReadSafe<uint64_t>(
            objAddr + DynOff::OBJECT_INSTANCE_ID, 0);

        json obj;
        obj["address"]    = AddrToHex(objAddr);
        obj["name"]       = name;
        obj["class_name"] = className;
        obj["object_id"]  = static_cast<int64_t>(objId);
        obj["slot_index"] = index;

        objects.push_back(std::move(obj));
        added++;
        return true;
    });

    json resp = OkResponse(id);
    resp["total"]   = total;
    resp["offset"]  = offset;
    resp["scanned"] = scanned;
    resp["objects"]  = std::move(objects);
    return resp;
}

// cmd: get_object
static json HandleGetObject(int id, const json& req) {
    std::string addrStr = GetAddrField(req);
    if (addrStr.empty()) return ErrorResponse(id, "Missing 'address' field");

    uint64_t addr = HexToAddr(addrStr);
    if (!addr || !Zoltraak::IsReadable(addr))
        return ErrorResponse(id, "Address unreadable: " + addrStr);

    std::string name      = ReadObjectName(addr);
    std::string className = DiAgolze::GetObjectClass(addr);
    uint64_t objId = Zoltraak::ReadSafe<uint64_t>(
        addr + DynOff::OBJECT_INSTANCE_ID, 0);
    int slotIndex = Ausetiese::FindByAddress(addr);

    Solliter::Debug("PIPE:cmd", "get_object: addr=%s name='%s' class='%s' slot=%d",
        addrStr.c_str(), name.c_str(), className.c_str(), slotIndex);

    json resp = OkResponse(id);
    resp["address"]    = AddrToHex(addr);
    resp["name"]       = name;
    resp["class_name"] = className;
    resp["object_id"]  = static_cast<int64_t>(objId);
    resp["slot_index"] = slotIndex;
    resp["is_node"]    = !className.empty(); // approximate
    resp["is_ref_counted"] = false;

    return resp;
}

// cmd: find_object (by node path, e.g. "/root/World3D/Player")
static json HandleFindObject(int id, const json& req) {
    std::string path = req.value("path", std::string{});
    if (path.empty()) return ErrorResponse(id, "Missing 'path' field");

    std::vector<std::string> segments;
    std::istringstream ss(path);
    std::string segment;
    while (std::getline(ss, segment, '/')) {
        if (!segment.empty()) segments.push_back(segment);
    }

    if (segments.empty())
        return ErrorResponse(id, "Invalid path: " + path);

    uint64_t current = Serie::GetRoot();
    if (!current) {
        json resp = OkResponse(id);
        resp["found"] = false;
        return resp;
    }

    std::string rootName = ReadObjectName(current);
    size_t startSeg = 0;
    if (!rootName.empty() && rootName == segments[0]) {
        startSeg = 1;
    }

    for (size_t i = startSeg; i < segments.size(); ++i) {
        auto children = Serie::GetChildren(current);
        bool found = false;
        for (auto& child : children) {
            if (child.name == segments[i]) {
                current = child.addr;
                found = true;
                break;
            }
        }
        if (!found) {
            json resp = OkResponse(id);
            resp["found"] = false;
            return resp;
        }
    }

    std::string name      = ReadObjectName(current);
    std::string className = DiAgolze::GetObjectClass(current);

    json resp = OkResponse(id);
    resp["found"]      = true;
    resp["address"]    = AddrToHex(current);
    resp["name"]       = name;
    resp["class_name"] = className;
    resp["node_path"]  = path;
    return resp;
}

// cmd: find_by_address
static json HandleFindByAddress(int id, const json& req) {
    std::string addrStr = GetAddrField(req);
    if (addrStr.empty()) return ErrorResponse(id, "Missing 'address' field");

    uint64_t addr = HexToAddr(addrStr);
    int slotIndex = Ausetiese::FindByAddress(addr);

    if (slotIndex < 0) {
        json resp = OkResponse(id);
        resp["found"]   = false;
        resp["address"] = addrStr;
        return resp;
    }

    std::string name      = ReadObjectName(addr);
    std::string className = DiAgolze::GetObjectClass(addr);

    json resp = OkResponse(id);
    resp["found"]      = true;
    resp["address"]    = AddrToHex(addr);
    resp["name"]       = name;
    resp["class_name"] = className;
    resp["slot_index"] = slotIndex;
    return resp;
}

// cmd: walk_class
static json HandleWalkClass(int id, const json& req) {
    std::string className = req.value("class_name", std::string{});
    if (className.empty()) return ErrorResponse(id, "Missing 'class_name' field");

    auto classInfo = DiAgolze::GetClassInfo(className.c_str());
    if (classInfo.name.empty())
        return ErrorResponse(id, "Class not found: " + className);

    json properties = json::array();
    for (auto& prop : classInfo.properties) {
        json f;
        f["name"]        = prop.name;
        f["type"]        = VariantTypeName(prop.type);
        f["offset"]      = prop.offset;
        f["size"]        = 0;
        f["hint"]        = prop.hint;
        f["hint_string"] = prop.hintString;
        f["usage"]       = static_cast<int>(prop.usage);
        properties.push_back(std::move(f));
    }

    json methods = json::array();
    for (auto& m : classInfo.methods) {
        json method;
        method["name"]        = m.name;
        method["return_type"] = m.returnType;
        method["arg_count"]   = m.argCount;
        methods.push_back(std::move(method));
    }

    json resp = OkResponse(id);
    resp["class_name"]  = classInfo.name;
    resp["super_name"]  = classInfo.parentName;
    resp["class_addr"]  = AddrToHex(classInfo.nativePtr);
    resp["properties"]  = std::move(properties);
    resp["methods"]     = std::move(methods);
    return resp;
}

// Map a raw Variant::Type to 4.x type number for unified helper logic.
// For 4.x, returns the same value. For 3.x, remaps the different enum numbering.
static int NormalizeVariantType(int rawType) {
    if (!GodotVer::IsGodot3()) return rawType;
    // Godot 3.x → 4.x mapping
    static const int map3to4[] = {
        0, 1, 2, 3, 4, 5,    // 0-5: same (NIL..VECTOR2)
        7,                     // 6: RECT2 → 7
        9,                     // 7: VECTOR3 → 9
        11,                    // 8: TRANSFORM2D → 11
        14,                    // 9: PLANE → 14
        15,                    // 10: QUAT → 15
        16,                    // 11: AABB → 16
        17,                    // 12: BASIS → 17
        18,                    // 13: TRANSFORM → 18
        20,                    // 14: COLOR → 20
        22,                    // 15: NODE_PATH → 22
        23,                    // 16: _RID → 23
        24,                    // 17: OBJECT → 24
        27,                    // 18: DICTIONARY → 27
        28,                    // 19: ARRAY → 28
        29,                    // 20: POOL_BYTE → 29
        30,                    // 21: POOL_INT → 30
        32,                    // 22: POOL_REAL → 32
        34,                    // 23: POOL_STRING → 34
        35,                    // 24: POOL_VEC2 → 35
        36,                    // 25: POOL_VEC3 → 36
        37,                    // 26: POOL_COLOR → 37
    };
    if (rawType >= 0 && rawType < static_cast<int>(std::size(map3to4)))
        return map3to4[rawType];
    return rawType;
}

// Helper: Determine if a Variant::Type represents a pointer-like type
// Uses 4.x type numbers via NormalizeVariantType.
static bool IsPointerType(int rawType) {
    int t = NormalizeVariantType(rawType);
    // Object, Callable, Signal, Dictionary, Array, and all Packed arrays
    return t == 24 || t == 25 || t == 26 ||
           t == 27 || t == 28 ||
           (t >= 29 && t <= 38);
}

// Helper: Determine if a Variant::Type represents a struct-like type
static bool IsStructType(int rawType) {
    int t = NormalizeVariantType(rawType);
    // Vector2/3/4, Rect2, Plane, Quaternion, AABB, Color, Transform types
    return (t >= 5 && t <= 20);
}

// Helper: Determine if a Variant::Type represents a container type
static bool IsContainerType(int rawType) {
    int t = NormalizeVariantType(rawType);
    return t == 27 || t == 28 || // Dictionary, Array
           (t >= 29 && t <= 38); // Packed arrays
}

// Helper: Approximate in-memory size for a Variant::Type
// Normalizes to 4.x type numbers for a single switch.
static int VariantTypeSize(int rawType) {
    int t = NormalizeVariantType(rawType);
    switch (t) {
        case 0:  return 0;    // Nil
        case 1:  return 4;    // bool
        case 2:  return 8;    // int64
        case 3:  return 8;    // double
        case 4:  return 8;    // String ptr
        case 5:  return 8;    // Vector2
        case 6:  return 8;    // Vector2i
        case 7:  return 16;   // Rect2
        case 8:  return 16;   // Rect2i
        case 9:  return 12;   // Vector3
        case 10: return 12;   // Vector3i
        case 11: return 24;   // Transform2D (6 floats)
        case 12: return 16;   // Vector4
        case 13: return 16;   // Vector4i
        case 14: return 16;   // Plane
        case 15: return 16;   // Quaternion
        case 16: return 24;   // AABB
        case 17: return 36;   // Basis (9 floats)
        case 18: return 48;   // Transform3D (12 floats)
        case 19: return 64;   // Projection (16 floats)
        case 20: return 16;   // Color
        case 21: return 8;    // StringName
        case 22: return 8;    // NodePath
        case 23: return 8;    // RID
        case 24: return 16;   // Object (id + ptr)
        default: return 8;    // Callables, Signals, containers, packed arrays
    }
}

// cmd: walk_instance
static json HandleWalkInstance(int id, const json& req) {
    std::string addrStr = GetAddrField(req);
    if (addrStr.empty()) return ErrorResponse(id, "Missing 'address' field");

    uint64_t addr = HexToAddr(addrStr);
    if (!addr || !Zoltraak::IsReadable(addr))
        return ErrorResponse(id, "Address unreadable: " + addrStr);

    std::string name      = ReadObjectName(addr);
    std::string className = DiAgolze::GetObjectClass(addr);

    Solliter::Debug("PIPE:cmd", "walk_instance: addr=%s name='%s' class='%s'",
        addrStr.c_str(), name.c_str(), className.c_str());

    json fields = json::array();
    Ubel::WalkInstance(addr, [&](const Ubel::FieldEntry& entry) {
        json f;
        f["name"]         = entry.name;
        f["type"]         = entry.value.typeName;
        f["variant_type"] = entry.value.type;
        f["value"]        = entry.value.value;
        f["address"]      = entry.value.rawAddr ? AddrToHex(entry.value.rawAddr) : "";
        f["offset"]       = -1;  // Godot doesn't expose byte offsets via ClassDB
        f["size"]         = VariantTypeSize(entry.value.type);
        f["hint"]         = entry.hint;
        f["hint_string"]  = entry.hintString;
        f["usage"]        = static_cast<int>(entry.usage);
        f["is_pointer"]   = IsPointerType(entry.value.type);
        f["is_struct"]    = IsStructType(entry.value.type);
        f["is_container"] = IsContainerType(entry.value.type);
        if (!entry.className.empty()) {
            f["class_name"] = entry.className; // owning class for the property
        }
        fields.push_back(std::move(f));
    });

    Solliter::Debug("PIPE:cmd", "walk_instance: %d fields returned for '%s' (%s)",
        static_cast<int>(fields.size()), name.c_str(), className.c_str());

    // Read parent address (for Node objects)
    std::string parentAddr;
    if (DynOff::NODE_PARENT >= 0) {
        uint64_t pa = Zoltraak::ReadSafe<uint64_t>(addr + DynOff::NODE_PARENT, 0);
        if (pa && Zoltraak::IsReadable(pa))
            parentAddr = AddrToHex(pa);
    }

    json resp = OkResponse(id);
    resp["address"]    = AddrToHex(addr);
    resp["name"]       = name;
    resp["class_name"] = className;
    resp["parent_addr"] = parentAddr;
    resp["fields"]     = std::move(fields);
    return resp;
}

// Recursive helper for walk_tree
static json BuildTreeNode(uint64_t nodeAddr, int depth, int maxDepth) {
    json node;
    node["address"]    = AddrToHex(nodeAddr);
    node["name"]       = ReadObjectName(nodeAddr);
    node["class_name"] = DiAgolze::GetObjectClass(nodeAddr);

    if (depth < maxDepth) {
        auto children = Serie::GetChildren(nodeAddr);
        node["child_count"] = static_cast<int>(children.size());

        json childArr = json::array();
        for (auto& child : children) {
            childArr.push_back(BuildTreeNode(child.addr, depth + 1, maxDepth));
        }
        node["children"] = std::move(childArr);
    } else {
        auto children = Serie::GetChildren(nodeAddr);
        node["child_count"] = static_cast<int>(children.size());
    }

    return node;
}

// cmd: walk_tree
static json HandleWalkTree(int id, const json& req) {
    int maxDepth = req.value("depth", 3);
    if (maxDepth < 0) maxDepth = 0;
    if (maxDepth > 20) maxDepth = 20;

    // Optional: start from a specific node instead of root
    uint64_t startAddr = 0;
    std::string addrStr = GetAddrField(req);
    if (!addrStr.empty()) {
        startAddr = HexToAddr(addrStr);
    }

    if (!startAddr) {
        startAddr = Serie::GetRoot();
    }

    if (!startAddr)
        return ErrorResponse(id, "SceneTree root not available");

    json resp = OkResponse(id);
    resp["root"] = BuildTreeNode(startAddr, 0, maxDepth);
    return resp;
}

// Case-insensitive substring search helper
static bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needleLower) {
    if (needleLower.empty()) return true;
    if (haystack.size() < needleLower.size()) return false;
    return std::search(
        haystack.begin(), haystack.end(),
        needleLower.begin(), needleLower.end(),
        [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) ==
                                     static_cast<unsigned char>(b); }
    ) != haystack.end();
}

// cmd: find_instances
// Supports partial, case-insensitive matching on both object name and class name.
// When "exact"=true, reverts to exact class name match for backward compatibility.
static json HandleFindInstances(int id, const json& req) {
    std::string query = req.value("class_name", std::string{});
    if (query.empty()) return ErrorResponse(id, "Missing 'class_name' field");

    bool exact = req.value("exact", false);
    int limit = req.value("limit", 100);
    if (limit <= 0) limit = 100;
    if (limit > 10000) limit = 10000;

    // Pre-compute lowercase query for partial matching
    std::string queryLower = query;
    std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    json instances = json::array();
    int found = 0;

    Ausetiese::ForEachEx([&](int index, uint64_t objAddr) {
        if (found >= limit) return false;

        std::string cls  = DiAgolze::GetObjectClass(objAddr);
        std::string name = ReadObjectName(objAddr);

        bool match = false;
        if (exact) {
            // Exact class name match (legacy behavior)
            match = (cls == query);
        } else {
            // Partial, case-insensitive match on class name OR object name
            match = ContainsCaseInsensitive(cls, queryLower) ||
                    ContainsCaseInsensitive(name, queryLower);
        }

        if (match) {
            json obj;
            obj["address"]    = AddrToHex(objAddr);
            obj["name"]       = name;
            obj["class_name"] = cls;
            obj["slot_index"] = index;
            instances.push_back(std::move(obj));
            found++;
        }
        return true;
    });

    json resp = OkResponse(id);
    resp["class_name"] = query;
    resp["count"]      = found;
    resp["instances"]  = std::move(instances);
    return resp;
}

// cmd: read_mem
static json HandleReadMem(int id, const json& req) {
    std::string addrStr = GetAddrField(req);
    if (addrStr.empty()) return ErrorResponse(id, "Missing 'address' field");

    int size = req.value("size", 0);
    if (size <= 0 || size > 65536)
        return ErrorResponse(id, "Invalid size (must be 1-65536)");

    uint64_t addr = HexToAddr(addrStr);
    std::vector<uint8_t> buffer(size, 0);

    if (!Zoltraak::ReadSafe(addr, buffer.data(), size))
        return ErrorResponse(id, "Failed to read memory at " + addrStr);

    json resp = OkResponse(id);
    resp["address"] = addrStr;
    resp["size"]    = size;
    resp["data"]    = BytesToHex(buffer.data(), size);
    return resp;
}

// cmd: write_mem
static json HandleWriteMem(int id, const json& req) {
    std::string addrStr = GetAddrField(req);
    std::string hexData = GetHexField(req);
    if (addrStr.empty()) return ErrorResponse(id, "Missing 'address' field");
    if (hexData.empty()) return ErrorResponse(id, "Missing 'data' field");
    if (hexData.size() % 2 != 0)
        return ErrorResponse(id, "Invalid hex string (odd length)");

    size_t byteLen = hexData.size() / 2;
    if (byteLen > 65536)
        return ErrorResponse(id, "Data too large (max 65536 bytes)");

    uint64_t addr = HexToAddr(addrStr);
    std::vector<uint8_t> buffer(byteLen);
    HexToBytes(hexData, buffer.data(), byteLen);

    if (!Zoltraak::WriteMem(addr, buffer.data(), byteLen))
        return ErrorResponse(id, "Failed to write memory at " + addrStr);

    json resp = OkResponse(id);
    resp["address"] = addrStr;
    resp["written"] = static_cast<int>(byteLen);
    return resp;
}

// cmd: list_classes — enumerate all ClassDB classes
static json HandleListClasses(int id, const json& /*req*/) {
    json classes = json::array();

    DiAgolze::EnumerateClasses([&](const std::string& name, uint64_t /*infoAddr*/) {
        auto info = DiAgolze::GetClassInfo(name.c_str());

        json cls;
        cls["class_name"]     = name;
        cls["super_name"]     = info.parentName;
        cls["property_count"] = static_cast<int>(info.properties.size());
        cls["method_count"]   = static_cast<int>(info.methods.size());
        cls["is_game_class"]  = false; // TODO: detect game vs engine classes
        classes.push_back(std::move(cls));
    });

    json resp = OkResponse(id);
    resp["classes"] = std::move(classes);
    return resp;
}

// cmd: search_properties — search property names across all classes
static json HandleSearchProperties(int id, const json& req) {
    std::string query = req.value("query", std::string{});
    if (query.empty()) return ErrorResponse(id, "Missing 'query' field");

    // Convert query to lowercase for case-insensitive search
    std::string queryLower = query;
    std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    json hits = json::array();
    int maxHits = 200;

    DiAgolze::EnumerateClasses([&](const std::string& className, uint64_t /*infoAddr*/) {
        if (static_cast<int>(hits.size()) >= maxHits) return;

        auto info = DiAgolze::GetClassInfo(className.c_str());
        for (auto& prop : info.properties) {
            if (static_cast<int>(hits.size()) >= maxHits) return;

            std::string propLower = prop.name;
            std::transform(propLower.begin(), propLower.end(), propLower.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (propLower.find(queryLower) != std::string::npos) {
                json hit;
                hit["class_name"]    = className;
                hit["super_name"]    = info.parentName;
                hit["property_name"] = prop.name;
                hit["property_type"] = VariantTypeName(prop.type);
                hit["offset"]        = prop.offset;
                hit["size"]          = 0;
                hits.push_back(std::move(hit));
            }
        }
    });

    json resp = OkResponse(id);
    resp["count"] = static_cast<int>(hits.size());
    resp["hits"]  = std::move(hits);
    return resp;
}

// cmd: get_scan_status — report scan status (scan is done at init time)
static json HandleGetScanStatus(int id, const json& /*req*/) {
    json resp = OkResponse(id);
    resp["status"]      = "complete";
    resp["progress"]    = 100;
    resp["total"]       = 100;
    resp["message"]     = "Scan completed during initialization";
    resp["is_complete"] = true;
    return resp;
}

// cmd: trigger_scan — attempt to resolve any missing pointers (e.g. SceneTree)
static json HandleTriggerScan(int id, const json& /*req*/) {
    bool sceneTreeResolved = (Serie::GetRoot() != 0);

    // If SceneTree still not resolved, try a full scan
    if (!sceneTreeResolved) {
        uintptr_t addr = Frieren::FindSceneTree();
        if (addr) {
            Serie::Init(addr);
            sceneTreeResolved = (Serie::GetRoot() != 0);
        }
    }

    json resp = OkResponse(id);
    resp["scene_tree_resolved"] = sceneTreeResolved;
    return resp;
}

// cmd: get_ce_pointer_info — stub for Phase 5+
static json HandleGetCePointerInfo(int id, const json& /*req*/) {
    json resp = OkResponse(id);
    resp["breadcrumbs"] = json::array();
    resp["fields"]      = json::array();
    return resp;
}

// ============================================================
// Command dispatch table
// ============================================================
static const std::unordered_map<std::string, HandlerFn> s_handlers = {
    {"init",               HandleInit},
    {"get_pointers",       HandleGetPointers},
    {"get_object_count",   HandleGetObjectCount},
    {"get_offsets",        HandleGetOffsets},
    {"get_object_list",    HandleGetObjectList},
    {"get_object",         HandleGetObject},
    {"find_object",        HandleFindObject},
    {"find_by_address",    HandleFindByAddress},
    {"walk_class",         HandleWalkClass},
    {"walk_instance",      HandleWalkInstance},
    {"walk_tree",          HandleWalkTree},
    {"find_instances",     HandleFindInstances},
    {"read_mem",           HandleReadMem},
    {"write_mem",          HandleWriteMem},
    {"list_classes",       HandleListClasses},
    {"search_properties",  HandleSearchProperties},
    {"get_scan_status",    HandleGetScanStatus},
    {"trigger_scan",       HandleTriggerScan},
    {"get_ce_pointer_info",HandleGetCePointerInfo},
};

// ============================================================
// Pipe I/O
// ============================================================

// Write a JSON response line to the pipe (thread-safe)
static bool WriteLine(HANDLE hPipe, const std::string& line) {
    std::lock_guard<std::mutex> lock(s_writeMtx);
    std::string data = line + "\n";
    DWORD written = 0;
    return WriteFile(hPipe, data.c_str(), static_cast<DWORD>(data.size()),
                     &written, nullptr) && written == data.size();
}

// Process a single JSON request and return the response
static std::string ProcessRequest(const std::string& line) {
    json req;
    try {
        req = json::parse(line);
    } catch (const json::parse_error& e) {
        LOG_ERROR("JSON parse error: %s", e.what());
        return json{{"id", 0}, {"ok", false}, {"error", "JSON parse error"}}.dump();
    }

    int id = req.value("id", 0);
    std::string cmd = req.value("cmd", std::string{});

    if (cmd.empty()) {
        return ErrorResponse(id, "Missing 'cmd' field").dump();
    }

    LOG_INFO("cmd:%s id=%d", cmd.c_str(), id);

    auto it = s_handlers.find(cmd);
    if (it == s_handlers.end()) {
        LOG_WARN("Unknown command: %s", cmd.c_str());
        return ErrorResponse(id, "Unknown command: " + cmd).dump();
    }

    try {
        json resp = it->second(id, req);
        return resp.dump();
    } catch (const std::exception& e) {
        LOG_ERROR("Handler exception for '%s': %s", cmd.c_str(), e.what());
        return ErrorResponse(id, std::string("Internal error: ") + e.what()).dump();
    }
}

// ============================================================
// Pipe server thread
// ============================================================
static DWORD WINAPI PipeThreadProc(LPVOID /*param*/) {
    LOG_INFO("Pipe server thread started on %s", Grimoire::PIPE_NAME);

    while (s_running.load()) {
        s_hPipe = CreateNamedPipeA(
            Grimoire::PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            Grimoire::PIPE_BUF_SIZE,
            Grimoire::PIPE_BUF_SIZE,
            0,
            nullptr
        );

        if (s_hPipe == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            LOG_ERROR("CreateNamedPipe failed: error %lu", err);
            Sleep(1000);
            continue;
        }

        LOG_INFO("Waiting for client connection...");

        BOOL connected = ConnectNamedPipe(s_hPipe, nullptr);
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
            if (!s_running.load()) break;
            LOG_ERROR("ConnectNamedPipe failed: error %lu", GetLastError());
            CloseHandle(s_hPipe);
            s_hPipe = INVALID_HANDLE_VALUE;
            Sleep(500);
            continue;
        }

        s_connected.store(true);
        LOG_INFO("Client connected");

        std::string lineBuffer;
        char readBuf[4096];

        while (s_running.load()) {
            DWORD bytesRead = 0;
            BOOL ok = ReadFile(s_hPipe, readBuf, sizeof(readBuf) - 1,
                              &bytesRead, nullptr);

            if (!ok || bytesRead == 0) {
                DWORD err = GetLastError();
                if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA) {
                    LOG_INFO("Client disconnected");
                } else if (s_running.load()) {
                    LOG_ERROR("ReadFile failed: error %lu", err);
                }
                break;
            }

            readBuf[bytesRead] = '\0';
            lineBuffer += readBuf;

            size_t pos;
            while ((pos = lineBuffer.find('\n')) != std::string::npos) {
                std::string line = lineBuffer.substr(0, pos);
                lineBuffer.erase(0, pos + 1);

                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;

                std::string response = ProcessRequest(line);
                if (!WriteLine(s_hPipe, response)) {
                    LOG_ERROR("WriteLine failed, client may have disconnected");
                    goto disconnect;
                }
            }
        }

    disconnect:
        s_connected.store(false);
        FlushFileBuffers(s_hPipe);
        DisconnectNamedPipe(s_hPipe);
        CloseHandle(s_hPipe);
        s_hPipe = INVALID_HANDLE_VALUE;

        if (s_running.load()) {
            LOG_INFO("Pipe closed, will reconnect...");
            Sleep(100);
        }
    }

    LOG_INFO("Pipe server thread exiting");
    return 0;
}

// ============================================================
// Public API
// ============================================================

void Start() {
    if (s_running.load()) return;

    LOG_INFO("Starting pipe server");
    s_running.store(true);

    s_hThread = CreateThread(nullptr, 0, PipeThreadProc, nullptr, 0, nullptr);
    if (!s_hThread) {
        LOG_ERROR("Failed to create pipe server thread: error %lu", GetLastError());
        s_running.store(false);
    }
}

void Stop() {
    if (!s_running.load()) return;

    LOG_INFO("Stopping pipe server");
    s_running.store(false);

    if (s_hPipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(s_hPipe, nullptr);
        DisconnectNamedPipe(s_hPipe);
        CloseHandle(s_hPipe);
        s_hPipe = INVALID_HANDLE_VALUE;
    }

    if (s_hThread) {
        WaitForSingleObject(s_hThread, 5000);
        CloseHandle(s_hThread);
        s_hThread = nullptr;
    }

    s_connected.store(false);
    LOG_INFO("Pipe server stopped");
}

bool IsConnected() {
    return s_connected.load();
}

bool SendEvent(const std::string& jsonStr) {
    if (!s_connected.load() || s_hPipe == INVALID_HANDLE_VALUE)
        return false;

    return WriteLine(s_hPipe, jsonStr);
}

} // namespace Zolt::Fern
