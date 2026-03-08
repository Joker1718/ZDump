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

#include <nlohmann/json.hpp>

using json = nlohmann::json;

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

// Read object name (StringName) by probing node name offsets
// This mirrors Serie's ProbeNodeName logic
static std::string ReadObjectName(uint64_t objAddr) {
    if (!objAddr) return "";

    // Try known NODE_NAME offset first
    if (DynOff::NODE_NAME >= 0) {
        std::string name = Flamme::Resolve(objAddr + DynOff::NODE_NAME);
        if (!name.empty()) return name;
    }

    // Probe common offsets for StringName fields
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

    // Module info
    wchar_t processPath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, processPath, MAX_PATH);
    char moduleName[MAX_PATH] = {};
    WideCharToMultiByte(CP_UTF8, 0, processPath, -1, moduleName, MAX_PATH, nullptr, nullptr);
    // Extract filename only
    const char* sep = strrchr(moduleName, '\\');
    resp["module_name"] = sep ? (sep + 1) : moduleName;
    resp["module_base"] = AddrToHex(Zoltraak::GetModuleBase());
    resp["pid"]         = static_cast<int>(GetCurrentProcessId());

    LOG_INFO("cmd:init -> version=%s pid=%d", ptrs.godotVersion.c_str(), GetCurrentProcessId());
    return resp;
}

// cmd: get_pointers
static json HandleGetPointers(int id, const json& /*req*/) {
    const auto& ptrs = Frieren::GetPointers();

    json resp = OkResponse(id);
    resp["object_db"]          = AddrToHex(ptrs.ObjectDB);
    resp["object_db_slot_max"] = Ausetiese::GetSlotMax();
    resp["class_db"]           = AddrToHex(ptrs.ClassDB);
    resp["scene_tree"]         = AddrToHex(ptrs.SceneTree);
    resp["engine"]             = AddrToHex(ptrs.Engine);

    json stats;
    stats["object_db_method"]   = ptrs.objectDBMethod;
    stats["class_db_method"]    = ptrs.classDBMethod;
    stats["scene_tree_method"]  = ptrs.sceneTreeMethod;
    stats["engine_method"]      = ptrs.engineMethod;
    stats["patterns_tried"]     = ptrs.objectDBPatternsTried + ptrs.classDBPatternsTried
                                + ptrs.sceneTreePatternsTried + ptrs.enginePatternsTried;
    resp["scan_stats"] = stats;

    return resp;
}

// cmd: get_object_count
static json HandleGetObjectCount(int id, const json& /*req*/) {
    json resp = OkResponse(id);
    resp["count"] = Ausetiese::GetObjectCount();
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
        if (index < offset) return true;        // Skip until offset
        if (added >= limit) return false;        // Enough items

        std::string name      = ReadObjectName(objAddr);
        std::string className = DiAgolze::GetObjectClass(objAddr);

        uint64_t objId = Zoltraak::ReadSafe<uint64_t>(
            objAddr + DynOff::OBJECT_INSTANCE_ID, 0);

        // Check is_ref_counted from slot metadata (bit 0 of metadata word)
        // We don't have easy access to slot metadata here, so skip for now

        json obj;
        obj["addr"]       = AddrToHex(objAddr);
        obj["name"]       = name;
        obj["class_name"] = className;
        obj["object_id"]  = std::to_string(objId);
        obj["slot_index"] = index;

        objects.push_back(std::move(obj));
        added++;
        return true;
    });

    json resp = OkResponse(id);
    resp["total"]   = total;
    resp["scanned"] = scanned;
    resp["objects"]  = std::move(objects);
    return resp;
}

// cmd: get_object
static json HandleGetObject(int id, const json& req) {
    std::string addrStr = req.value("addr", std::string{});
    if (addrStr.empty()) return ErrorResponse(id, "Missing 'addr' field");

    uint64_t addr = HexToAddr(addrStr);
    if (!addr || !Zoltraak::IsReadable(addr))
        return ErrorResponse(id, "Address unreadable: " + addrStr);

    std::string name      = ReadObjectName(addr);
    std::string className = DiAgolze::GetObjectClass(addr);
    uint64_t objId = Zoltraak::ReadSafe<uint64_t>(
        addr + DynOff::OBJECT_INSTANCE_ID, 0);
    int slotIndex = Ausetiese::FindByAddress(addr);

    json resp = OkResponse(id);
    resp["addr"]       = AddrToHex(addr);
    resp["name"]       = name;
    resp["class_name"] = className;
    resp["object_id"]  = std::to_string(objId);
    resp["slot_index"] = slotIndex;

    return resp;
}

// cmd: find_object (by node path, e.g. "/root/World3D/Player")
static json HandleFindObject(int id, const json& req) {
    std::string path = req.value("path", std::string{});
    if (path.empty()) return ErrorResponse(id, "Missing 'path' field");

    // Split path by '/'
    std::vector<std::string> segments;
    std::istringstream ss(path);
    std::string segment;
    while (std::getline(ss, segment, '/')) {
        if (!segment.empty()) segments.push_back(segment);
    }

    if (segments.empty())
        return ErrorResponse(id, "Invalid path: " + path);

    // Start from root
    uint64_t current = Serie::GetRoot();
    if (!current)
        return ErrorResponse(id, "SceneTree root not available");

    // Check if first segment matches root
    std::string rootName = ReadObjectName(current);
    size_t startSeg = 0;
    if (!rootName.empty() && rootName == segments[0]) {
        startSeg = 1;
    }

    // Walk down the tree
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
            return ErrorResponse(id, "Node not found: " + segments[i] + " in path " + path);
        }
    }

    std::string name      = ReadObjectName(current);
    std::string className = DiAgolze::GetObjectClass(current);

    json resp = OkResponse(id);
    resp["addr"]       = AddrToHex(current);
    resp["name"]       = name;
    resp["class_name"] = className;
    resp["path"]       = path;
    return resp;
}

// cmd: find_by_address
static json HandleFindByAddress(int id, const json& req) {
    std::string addrStr = req.value("addr", std::string{});
    if (addrStr.empty()) return ErrorResponse(id, "Missing 'addr' field");

    uint64_t addr = HexToAddr(addrStr);
    int slotIndex = Ausetiese::FindByAddress(addr);

    if (slotIndex < 0) {
        json resp = OkResponse(id);
        resp["found"] = false;
        resp["addr"]  = addrStr;
        return resp;
    }

    std::string name      = ReadObjectName(addr);
    std::string className = DiAgolze::GetObjectClass(addr);

    json resp = OkResponse(id);
    resp["found"]      = true;
    resp["addr"]       = AddrToHex(addr);
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

    json fields = json::array();
    for (auto& prop : classInfo.properties) {
        json f;
        f["name"]       = prop.name;
        f["class_name"] = prop.className;
        f["type"]       = prop.type;
        f["offset"]     = prop.offset;
        if (!prop.hint.empty()) f["hint"] = prop.hint;
        fields.push_back(std::move(f));
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
    resp["parent_name"] = classInfo.parentName;
    resp["native_ptr"]  = AddrToHex(classInfo.nativePtr);
    resp["fields"]      = std::move(fields);
    resp["methods"]     = std::move(methods);
    return resp;
}

// cmd: walk_instance
static json HandleWalkInstance(int id, const json& req) {
    std::string addrStr = req.value("addr", std::string{});
    if (addrStr.empty()) return ErrorResponse(id, "Missing 'addr' field");

    uint64_t addr = HexToAddr(addrStr);
    if (!addr || !Zoltraak::IsReadable(addr))
        return ErrorResponse(id, "Address unreadable: " + addrStr);

    std::string name      = ReadObjectName(addr);
    std::string className = DiAgolze::GetObjectClass(addr);

    json fields = json::array();
    Ubel::WalkInstance(addr, [&](const Ubel::FieldEntry& entry) {
        json f;
        f["name"]         = entry.name;
        f["class_name"]   = entry.className;
        f["type"]         = entry.value.typeName;
        f["variant_type"] = entry.value.type;
        f["value"]        = entry.value.value;
        f["raw_addr"]     = AddrToHex(entry.value.rawAddr);
        fields.push_back(std::move(f));
    });

    json resp = OkResponse(id);
    resp["addr"]       = AddrToHex(addr);
    resp["name"]       = name;
    resp["class_name"] = className;
    resp["fields"]     = std::move(fields);
    return resp;
}

// Recursive helper for walk_tree
static json BuildTreeNode(uint64_t nodeAddr, int depth, int maxDepth) {
    json node;
    node["addr"]       = AddrToHex(nodeAddr);
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
    if (req.contains("addr")) {
        startAddr = HexToAddr(req["addr"].get<std::string>());
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

// cmd: find_instances
static json HandleFindInstances(int id, const json& req) {
    std::string className = req.value("class_name", std::string{});
    if (className.empty()) return ErrorResponse(id, "Missing 'class_name' field");

    int limit = req.value("limit", 100);
    if (limit <= 0) limit = 100;
    if (limit > 10000) limit = 10000;

    json instances = json::array();
    int found = 0;

    Ausetiese::ForEachEx([&](int index, uint64_t objAddr) {
        if (found >= limit) return false;

        std::string cls = DiAgolze::GetObjectClass(objAddr);
        if (cls == className) {
            std::string name = ReadObjectName(objAddr);

            json obj;
            obj["addr"]       = AddrToHex(objAddr);
            obj["name"]       = name;
            obj["class_name"] = cls;
            obj["slot_index"] = index;
            instances.push_back(std::move(obj));
            found++;
        }
        return true;
    });

    json resp = OkResponse(id);
    resp["class_name"] = className;
    resp["count"]      = found;
    resp["instances"]  = std::move(instances);
    return resp;
}

// cmd: read_mem
static json HandleReadMem(int id, const json& req) {
    std::string addrStr = req.value("addr", std::string{});
    if (addrStr.empty()) return ErrorResponse(id, "Missing 'addr' field");

    int size = req.value("size", 0);
    if (size <= 0 || size > 65536)
        return ErrorResponse(id, "Invalid size (must be 1-65536)");

    uint64_t addr = HexToAddr(addrStr);
    std::vector<uint8_t> buffer(size, 0);

    if (!Zoltraak::ReadSafe(addr, buffer.data(), size))
        return ErrorResponse(id, "Failed to read memory at " + addrStr);

    json resp = OkResponse(id);
    resp["addr"] = addrStr;
    resp["size"] = size;
    resp["hex"]  = BytesToHex(buffer.data(), size);
    return resp;
}

// cmd: write_mem
static json HandleWriteMem(int id, const json& req) {
    std::string addrStr = req.value("addr", std::string{});
    std::string hexData = req.value("hex", std::string{});
    if (addrStr.empty()) return ErrorResponse(id, "Missing 'addr' field");
    if (hexData.empty()) return ErrorResponse(id, "Missing 'hex' field");
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
    resp["addr"]    = addrStr;
    resp["written"] = static_cast<int>(byteLen);
    return resp;
}

// ============================================================
// Command dispatch table
// ============================================================
static const std::unordered_map<std::string, HandlerFn> s_handlers = {
    {"init",             HandleInit},
    {"get_pointers",     HandleGetPointers},
    {"get_object_count", HandleGetObjectCount},
    {"get_offsets",      HandleGetOffsets},
    {"get_object_list",  HandleGetObjectList},
    {"get_object",       HandleGetObject},
    {"find_object",      HandleFindObject},
    {"find_by_address",  HandleFindByAddress},
    {"walk_class",       HandleWalkClass},
    {"walk_instance",    HandleWalkInstance},
    {"walk_tree",        HandleWalkTree},
    {"find_instances",   HandleFindInstances},
    {"read_mem",         HandleReadMem},
    {"write_mem",        HandleWriteMem},
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
        // Create the named pipe (one client at a time)
        s_hPipe = CreateNamedPipeA(
            Grimoire::PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,                            // Max instances (single client)
            Grimoire::PIPE_BUF_SIZE,      // Output buffer
            Grimoire::PIPE_BUF_SIZE,      // Input buffer
            0,                            // Default timeout
            nullptr                       // Default security
        );

        if (s_hPipe == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            LOG_ERROR("CreateNamedPipe failed: error %lu", err);
            Sleep(1000);
            continue;
        }

        LOG_INFO("Waiting for client connection...");

        // Wait for a client to connect (blocking)
        BOOL connected = ConnectNamedPipe(s_hPipe, nullptr);
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
            if (!s_running.load()) break; // Shutting down
            LOG_ERROR("ConnectNamedPipe failed: error %lu", GetLastError());
            CloseHandle(s_hPipe);
            s_hPipe = INVALID_HANDLE_VALUE;
            Sleep(500);
            continue;
        }

        s_connected.store(true);
        LOG_INFO("Client connected");

        // Message loop: read lines, process, respond
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
                break; // Client disconnected
            }

            readBuf[bytesRead] = '\0';
            lineBuffer += readBuf;

            // Process complete lines (newline-delimited)
            size_t pos;
            while ((pos = lineBuffer.find('\n')) != std::string::npos) {
                std::string line = lineBuffer.substr(0, pos);
                lineBuffer.erase(0, pos + 1);

                // Trim \r if present
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
            Sleep(100); // Brief pause before re-creating
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

    // Cancel blocking ConnectNamedPipe / ReadFile by closing the pipe handle
    if (s_hPipe != INVALID_HANDLE_VALUE) {
        // CancelIoEx to unblock blocking I/O on the pipe
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
