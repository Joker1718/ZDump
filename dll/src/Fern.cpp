// Fern — 費倫 (Apprentice — PipeServer)
// Named pipe IPC server for communication with ZoltDumpUI.
// Protocol: newline-delimited JSON over \\.\pipe\ZoltDumpBfx
// See docs/pipe-protocol.md for full command specification.

#define LOG_CAT "PIPE"

#include "Fern.h"
#include "Solitaer.h"
#include "Grimoire.h"
#include "Frieren.h"
#include "Ausetiese.h"
#include "Diagoldze.h"
#include "Flamme.h"
#include "Serie.h"
#include "Ubel.h"
#include "Zoltraak.h"
#include "Heiter.h"
#include "BuildInfo.h"

#include <Windows.h>
#include <atomic>
#include <string>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>
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

// Sanitize a string for JSON: replace invalid UTF-8 bytes with '?'.
// nlohmann::json throws on invalid UTF-8, so all memory-sourced strings
// must pass through this before assignment.
static std::string SafeStr(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        int seqLen = 0;
        if      (c < 0x80) seqLen = 1;
        else if ((c & 0xE0) == 0xC0) seqLen = 2;
        else if ((c & 0xF0) == 0xE0) seqLen = 3;
        else if ((c & 0xF8) == 0xF0) seqLen = 4;
        if (seqLen == 0 || i + seqLen > s.size()) {
            out.push_back('?');
            ++i;
            continue;
        }
        bool valid = true;
        for (int j = 1; j < seqLen; ++j) {
            if ((static_cast<unsigned char>(s[i + j]) & 0xC0) != 0x80) {
                valid = false;
                break;
            }
        }
        if (valid) {
            out.append(s, i, seqLen);
            i += seqLen;
        } else {
            out.push_back('?');
            ++i;
        }
    }
    return out;
}

// Read object name — delegates to Serie::GetNodeName (unified probe logic)
static std::string ReadObjectName(uint64_t objAddr) {
    return Serie::GetNodeName(objAddr);
}

// Version-aware Variant::Type → display name (delegates to Ubel)
static const char* VariantTypeName(int type) {
    return Ubel::VariantTypeName(type);
}

// ============================================================
// Command Handlers
// ============================================================
using HandlerFn = json(*)(int id, const json& req);

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
        std::string className = Diagoldze::GetObjectClass(objAddr);

        uint64_t objId = Zoltraak::ReadSafe<uint64_t>(
            objAddr + DynOff::OBJECT_INSTANCE_ID, 0);

        json obj;
        obj["address"]    = AddrToHex(objAddr);
        obj["name"]       = SafeStr(name);
        obj["class_name"] = SafeStr(className);
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
    std::string className = Diagoldze::GetObjectClass(addr);
    uint64_t objId = Zoltraak::ReadSafe<uint64_t>(
        addr + DynOff::OBJECT_INSTANCE_ID, 0);
    int slotIndex = Ausetiese::FindByAddress(addr);

    Solitaer::Debug("PIPE:cmd", "get_object: addr=%s name='%s' class='%s' slot=%d",
        addrStr.c_str(), name.c_str(), className.c_str(), slotIndex);

    json resp = OkResponse(id);
    resp["address"]    = AddrToHex(addr);
    resp["name"]       = SafeStr(name);
    resp["class_name"] = SafeStr(className);
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
    std::string className = Diagoldze::GetObjectClass(current);

    json resp = OkResponse(id);
    resp["found"]      = true;
    resp["address"]    = AddrToHex(current);
    resp["name"]       = SafeStr(name);
    resp["class_name"] = SafeStr(className);
    resp["node_path"]  = path;
    return resp;
}

// cmd: find_by_address
// Four-phase lookup:
//   1. Exact Object address match in ObjectDB
//   2. Range search: find Object whose body contains the address
//   3. ScriptInstance search: find Object whose GDScript members Vector contains the address
//   4. Direct probe: validate address as Godot Object via vtable check (bypasses ObjectDB)
// When a match is found inside an Object/member, returns the Object's address
// plus member_offset displacement and match_type indicator.
static json HandleFindByAddress(int id, const json& req) {
    std::string addrStr = GetAddrField(req);
    if (addrStr.empty()) return ErrorResponse(id, "Missing 'address' field");

    uint64_t addr = HexToAddr(addrStr);

    Solitaer::Info("PIPE", "find_by_address: target=%s readable=%d",
        addrStr.c_str(), Zoltraak::IsReadable(addr) ? 1 : 0);

    // Log what's at the target address for diagnostics
    if (Zoltraak::IsReadable(addr)) {
        uint64_t val0 = Zoltraak::ReadSafe<uint64_t>(addr, 0);
        uint64_t val8 = Zoltraak::ReadSafe<uint64_t>(addr + 8, 0);
        Solitaer::Debug("PIPE", "find_by_address: [%s]+0=%p +8=%p",
            addrStr.c_str(), reinterpret_cast<void*>(val0), reinterpret_cast<void*>(val8));
    }

    // Phase 1+2: Range-based lookup (includes exact match as fast path)
    auto match = Ausetiese::FindByAddressRange(addr);

    Solitaer::Debug("PIPE", "find_by_address: Phase 1+2 result: slot=%d obj=%p offset=+0x%X",
        match.slotIndex, reinterpret_cast<void*>(match.objectAddr), match.memberOffset);

    // Phase 1 exact match: return immediately (address IS an Object)
    if (match.slotIndex >= 0 && match.memberOffset == 0) {
        std::string name      = ReadObjectName(match.objectAddr);
        std::string className = Diagoldze::GetObjectClass(match.objectAddr);

        json resp = OkResponse(id);
        resp["found"]         = true;
        resp["match_type"]    = "exact";
        resp["address"]       = AddrToHex(match.objectAddr);
        resp["name"]          = SafeStr(name);
        resp["class_name"]    = SafeStr(className);
        resp["slot_index"]    = match.slotIndex;
        resp["member_offset"] = 0;
        return resp;
    }

    // Phase 3 BEFORE range fallback: ScriptInstance members search
    // GDScript variables are heap-allocated in a separate Vector<Variant>,
    // not within the Object body — so range match can be wrong.
    auto scriptMatch = Ubel::FindScriptMemberByAddress(addr);

    Solitaer::Debug("PIPE", "find_by_address: Phase 3 (script member): obj=%p memberIdx=%d",
        reinterpret_cast<void*>(scriptMatch.objectAddr), scriptMatch.memberIndex);

    if (scriptMatch.objectAddr) {
        std::string name      = ReadObjectName(scriptMatch.objectAddr);
        std::string className = Diagoldze::GetObjectClass(scriptMatch.objectAddr);
        int slotIndex = Ausetiese::FindByAddress(scriptMatch.objectAddr);

        json resp = OkResponse(id);
        resp["found"]          = true;
        resp["match_type"]     = "script_member";
        resp["address"]        = AddrToHex(scriptMatch.objectAddr);
        resp["name"]           = SafeStr(name);
        resp["class_name"]     = SafeStr(className);
        resp["slot_index"]     = slotIndex;
        resp["member_offset"]  = 0;
        resp["script_member"]  = scriptMatch.memberIndex;
        resp["variant_offset"] = scriptMatch.variantOffset;
        return resp;
    }

    // Phase 2 fallback: range match (address is within an Object's body).
    // Limit displacement to 0x1000 (4KB) — beyond that it's likely a coincidental
    // heap neighbour, not actual Object data.
    if (match.slotIndex >= 0 && match.memberOffset > 0 && match.memberOffset <= 0x1000) {
        std::string name      = ReadObjectName(match.objectAddr);
        std::string className = Diagoldze::GetObjectClass(match.objectAddr);

        json resp = OkResponse(id);
        resp["found"]         = true;
        resp["match_type"]    = "range";
        resp["address"]       = AddrToHex(match.objectAddr);
        resp["name"]          = SafeStr(name);
        resp["class_name"]    = SafeStr(className);
        resp["slot_index"]    = match.slotIndex;
        resp["member_offset"] = match.memberOffset;
        return resp;
    }

    // Phase 4: Direct probe — validate the address as a Godot Object even if
    // it's not in ObjectDB (e.g. slot_max estimation too low, or object added
    // after last scan). Checks vtable structure without ObjectDB dependency.
    bool probeValid = Ausetiese::ValidateObjectAddress(addr);
    Solitaer::Debug("PIPE", "find_by_address: Phase 4 (vtable probe): valid=%d", probeValid ? 1 : 0);
    if (probeValid) {
        std::string name      = ReadObjectName(addr);
        std::string className = Diagoldze::GetObjectClass(addr);

        Solitaer::Info("PIPE", "find_by_address: Phase 4 direct probe hit for %s (class=%s)",
            addrStr.c_str(), className.empty() ? "(unknown)" : className.c_str());

        json resp = OkResponse(id);
        resp["found"]         = true;
        resp["match_type"]    = "probe";
        resp["address"]       = AddrToHex(addr);
        resp["name"]          = SafeStr(name);
        resp["class_name"]    = SafeStr(className);
        resp["slot_index"]    = -1;  // Not in ObjectDB
        resp["member_offset"] = 0;
        return resp;
    }

    // Phase 4.5: ScriptInstance range scan — target might be a field inside a
    // C#/.NET or GDScript ScriptInstance object. For each ObjectDB object, read
    // its ScriptInstance pointer at [obj+OBJECT_SCRIPT_INSTANCE] and check if
    // the target address falls within that ScriptInstance's memory region.
    // This is critical for .NET/C# Godot builds (e.g., Slay the Spire 2) where
    // C# fields like HP live in the managed object at ScriptInstance+offset.
    if (DynOff::OBJECT_SCRIPT_INSTANCE >= 0) {
        constexpr uint64_t MAX_SCRIPT_SIZE = 0x400; // 1KB max script instance body
        uint64_t bestObj = 0;
        int bestSlot = -1;
        uint64_t bestScript = 0;
        uint64_t bestOffset = UINT64_MAX;

        Ausetiese::ForEachEx([&](int index, uint64_t objAddr) {
            uint64_t scriptInst = Zoltraak::ReadSafe<uint64_t>(
                objAddr + DynOff::OBJECT_SCRIPT_INSTANCE, 0);
            if (!scriptInst || !Zoltraak::IsReadable(scriptInst)) return true;
            if (addr >= scriptInst && addr < scriptInst + MAX_SCRIPT_SIZE) {
                uint64_t off = addr - scriptInst;
                if (off < bestOffset) {
                    bestObj = objAddr;
                    bestSlot = index;
                    bestScript = scriptInst;
                    bestOffset = off;
                }
                return false; // Early exit — ScriptInstance ranges don't overlap
            }
            return true;
        });

        Solitaer::Debug("PIPE", "find_by_address: Phase 4.5 (script instance): obj=%p script=%p offset=+0x%X",
            reinterpret_cast<void*>(bestObj), reinterpret_cast<void*>(bestScript),
            bestOffset == UINT64_MAX ? 0 : static_cast<unsigned>(bestOffset));

        if (bestObj) {
            std::string name      = ReadObjectName(bestObj);
            std::string className = Diagoldze::GetObjectClass(bestObj);
            int slotIndex = bestSlot;

            json resp = OkResponse(id);
            resp["found"]           = true;
            resp["match_type"]      = "script_instance";
            resp["address"]         = AddrToHex(bestObj);
            resp["name"]            = SafeStr(name);
            resp["class_name"]      = SafeStr(className);
            resp["slot_index"]      = slotIndex;
            resp["member_offset"]   = 0;
            resp["script_offset"]   = static_cast<int>(bestOffset);
            resp["script_instance"] = AddrToHex(bestScript);
            resp["offset_from_base"]= static_cast<int>(bestOffset);
            return resp;
        }
    }

    // Phase 4.6: .NET CSharpInstance → GCHandle → managed object scan.
    // For .NET/C# Godot builds (e.g., Slay the Spire 2), the chain is:
    //   GodotObject[+0x68] → CSharpInstance → GCHandle → .NET managed object → field
    // The .NET managed object lives on a separate heap, NOT contiguous with the
    // CSharpInstance. Phase 4.5 misses these because it only checks the CSharpInstance
    // address range. Here we dereference pointer candidates inside each CSharpInstance
    // body — one of them is the GCHandle (pointer into handle table), and the table
    // entry holds the .NET managed object address. If target falls within that managed
    // object, we've found the owning Godot Object.
    //
    // Levels of search:
    //   Level 1: target inside managed wrapper directly  (GCHandle → wrapper → target)
    //   Level 2: target inside child object of wrapper   (wrapper → field → child → target)
    //            Validates child pointer has a MethodTable (first qword in module range)
    //   Level 3: bottom-up — scan backward from target to find .NET object header,
    //            then match against all collected GCHandle dereferences
    if (DynOff::OBJECT_SCRIPT_INSTANCE >= 0) {
        constexpr int SI_SCAN_SIZE = 0x100; // Scan first 256 bytes of ScriptInstance body
        constexpr uint64_t MAX_MANAGED_L1 = 0x400; // Level 1: direct wrapper range
        constexpr uint64_t MAX_MANAGED_L2 = 0x200; // Level 2: child object range (tighter)
        uint64_t foundObj46 = 0;
        int foundSlot46 = -1;
        uint64_t foundManagedObj = 0;
        uint64_t foundManagedOff = 0;
        int foundLevel = 0;   // Which level matched (1, 2, or 3)
        int siCount = 0;      // Count of objects with ScriptInstance
        int csharpCount = 0;  // Count of non-GDScript ScriptInstances

        // Helper: validate a pointer looks like a .NET MethodTable pointer.
        // MethodTables live in module/CLR address space (typically 0x7FFx on Win64),
        // are 8-byte aligned, and point to readable memory.
        auto isLikelyMethodTable = [](uint64_t mt) -> bool {
            if (!mt || (mt & 0x7) != 0) return false;
            // Module-range check: high 24 bits should be > 0x7F0000 (i.e., addr >= 0x7F0000_00000000)
            // This filters out heap pointers which are typically < 0x7F...
            if ((mt >> 40) < 0x7F) return false;
            return Zoltraak::IsReadable(mt);
        };

        // Collect GCHandle → managed object mappings for Level 3 bottom-up search.
        // Maps: managedObj → { objAddr, slotIndex }
        struct GCEntry { uint64_t objAddr; int slot; };
        std::vector<GCEntry> gcMap; // entry per unique managed object address
        std::vector<uint64_t> gcManagedAddrs; // parallel: managed object addresses

        Ausetiese::ForEachEx([&](int index, uint64_t objAddr) {
            uint64_t si = Zoltraak::ReadSafe<uint64_t>(
                objAddr + DynOff::OBJECT_SCRIPT_INSTANCE, 0);
            if (!si || !Zoltraak::IsReadable(si)) return true;
            siCount++;

            // Bulk-read CSharpInstance/ScriptInstance body
            uint8_t siBuf[SI_SCAN_SIZE];
            if (!Zoltraak::ReadSafe(si, siBuf, SI_SCAN_SIZE)) return true;

            // Check if this is a GDScriptInstance by validating owner at known offset
            uint64_t ownerAtGdsi;
            memcpy(&ownerAtGdsi, siBuf + DynOff::GDSCRIPT_OWNER_OFF, sizeof(ownerAtGdsi));
            bool isGDScript = (ownerAtGdsi == objAddr);

            if (!isGDScript) {
                csharpCount++;
            }

            // For each pointer-sized field in the ScriptInstance body:
            for (int off = 8; off + 8 <= SI_SCAN_SIZE; off += 8) {
                uint64_t val;
                memcpy(&val, siBuf + off, sizeof(val));

                // Quick filter: must look like a heap pointer (aligned, high address)
                if (!val || (val & 0x7) != 0) continue;
                if ((val >> 40) == 0) continue;

                // Dereference: val might be a GCHandle (pointer to handle table entry)
                // The table entry contains the .NET managed object address.
                uint64_t managedObj = Zoltraak::ReadSafe<uint64_t>(val, 0);
                if (!managedObj || (managedObj & 0x7) != 0) continue;
                if ((managedObj >> 40) == 0) continue;

                // Validate managedObj looks like a .NET object (first qword = MethodTable)
                uint64_t mt = Zoltraak::ReadSafe<uint64_t>(managedObj, 0);
                if (!isLikelyMethodTable(mt)) continue;

                // This is a valid GCHandle → managed object. Collect for Level 3.
                gcMap.push_back({objAddr, index});
                gcManagedAddrs.push_back(managedObj);

                // Level 1: Check if target falls directly within this managed object
                if (addr >= managedObj && addr < managedObj + MAX_MANAGED_L1) {
                    uint64_t off46 = addr - managedObj;
                    // Keep best (smallest offset) match
                    if (!foundObj46 || off46 < foundManagedOff) {
                        foundObj46 = objAddr;
                        foundSlot46 = index;
                        foundManagedObj = managedObj;
                        foundManagedOff = off46;
                        foundLevel = 1;
                    }
                    // Don't return early — keep scanning for better match
                }

                // Level 2: Scan managed wrapper's fields for child .NET objects
                // that might contain the target. Validates child has MethodTable.
                if (!isGDScript) {
                    constexpr int WRAPPER_SCAN = 0x200;
                    uint8_t wrapBuf[WRAPPER_SCAN];
                    if (Zoltraak::ReadSafe(managedObj, wrapBuf, WRAPPER_SCAN)) {
                        for (int w = 8; w + 8 <= WRAPPER_SCAN; w += 8) {
                            uint64_t wval;
                            memcpy(&wval, wrapBuf + w, sizeof(wval));

                            if (!wval || (wval & 0x7) != 0) continue;
                            if ((wval >> 40) == 0) continue;

                            // Range check first (cheap), then validate MethodTable
                            if (addr >= wval && addr < wval + MAX_MANAGED_L2) {
                                // Validate: wval should point to a .NET managed object
                                uint64_t childMt = Zoltraak::ReadSafe<uint64_t>(wval, 0);
                                if (!isLikelyMethodTable(childMt)) continue;

                                uint64_t off46 = addr - wval;
                                if (!foundObj46 || off46 < foundManagedOff) {
                                    foundObj46 = objAddr;
                                    foundSlot46 = index;
                                    foundManagedObj = wval;
                                    foundManagedOff = off46;
                                    foundLevel = 2;
                                }
                            }
                        }
                    }
                }
            }
            return true; // Always continue — collect all GCHandles for Level 3
        });

        Solitaer::Info("PIPE", "find_by_address: Phase 4.6 stats: %d with SI, %d non-GDScript (CSharp), %d GCHandles collected",
            siCount, csharpCount, static_cast<int>(gcMap.size()));

        // Level 3: Bottom-up search. If Level 1/2 didn't find a match,
        // scan backward from target to find the .NET object header (MethodTable pointer),
        // then match the discovered base against collected GCHandle dereferences.
        if (!foundObj46 && !gcManagedAddrs.empty()) {
            Solitaer::Debug("PIPE", "find_by_address: Phase 4.6 Level 3 — bottom-up scan from %p",
                reinterpret_cast<void*>(addr));

            // Scan backward from target in 8-byte steps to find .NET object base.
            // A .NET managed object starts with a MethodTable pointer (module-range address).
            // We look for the nearest such pointer before our target.
            constexpr int MAX_BACKTRACK = 0x400; // max bytes to scan backward
            uint64_t alignedAddr = addr & ~7ULL;
            uint64_t candidateBase = 0;
            uint64_t candidateMt = 0;

            for (int back = 0; back <= MAX_BACKTRACK; back += 8) {
                uint64_t probe = alignedAddr - back;
                uint64_t val = Zoltraak::ReadSafe<uint64_t>(probe, 0);
                if (isLikelyMethodTable(val)) {
                    candidateBase = probe;
                    candidateMt = val;
                    break; // First (closest) MethodTable-like value wins
                }
            }

            if (candidateBase) {
                Solitaer::Debug("PIPE", "find_by_address: Phase 4.6 L3 candidate base=%p MT=%p",
                    reinterpret_cast<void*>(candidateBase), reinterpret_cast<void*>(candidateMt));

                // Now search collected GCHandle mappings for any wrapper that
                // directly references this candidate base, or for any wrapper whose
                // fields contain a pointer to this candidate base.
                for (size_t i = 0; i < gcManagedAddrs.size(); i++) {
                    uint64_t managedObj = gcManagedAddrs[i];

                    // Direct match: GCHandle → candidateBase
                    if (managedObj == candidateBase) {
                        foundObj46 = gcMap[i].objAddr;
                        foundSlot46 = gcMap[i].slot;
                        foundManagedObj = candidateBase;
                        foundManagedOff = addr - candidateBase;
                        foundLevel = 3;
                        Solitaer::Debug("PIPE", "find_by_address: Phase 4.6 L3 direct GCHandle match: obj=%p",
                            reinterpret_cast<void*>(foundObj46));
                        break;
                    }

                    // Indirect: wrapper field → candidateBase
                    constexpr int WRAPPER_SCAN = 0x200;
                    uint8_t wrapBuf[WRAPPER_SCAN];
                    if (!Zoltraak::ReadSafe(managedObj, wrapBuf, WRAPPER_SCAN)) continue;

                    for (int w = 8; w + 8 <= WRAPPER_SCAN; w += 8) {
                        uint64_t wval;
                        memcpy(&wval, wrapBuf + w, sizeof(wval));
                        if (wval == candidateBase) {
                            foundObj46 = gcMap[i].objAddr;
                            foundSlot46 = gcMap[i].slot;
                            foundManagedObj = candidateBase;
                            foundManagedOff = addr - candidateBase;
                            foundLevel = 3;
                            Solitaer::Debug("PIPE",
                                "find_by_address: Phase 4.6 L3 wrapper field match: obj=%p wrapper=%p +0x%X → base",
                                reinterpret_cast<void*>(foundObj46), reinterpret_cast<void*>(managedObj), w);
                            break;
                        }
                    }
                    if (foundObj46) break;
                }
            }

            if (!foundObj46) {
                Solitaer::Debug("PIPE", "find_by_address: Phase 4.6 Level 3 — no match (base=%p)",
                    reinterpret_cast<void*>(candidateBase));
            }
        }

        Solitaer::Debug("PIPE", "find_by_address: Phase 4.6 result: obj=%p managed=%p offset=+0x%X level=%d",
            reinterpret_cast<void*>(foundObj46), reinterpret_cast<void*>(foundManagedObj),
            static_cast<unsigned>(foundManagedOff), foundLevel);

        if (foundObj46) {
            std::string name      = ReadObjectName(foundObj46);
            std::string className = Diagoldze::GetObjectClass(foundObj46);

            Solitaer::Info("PIPE",
                "find_by_address: Phase 4.6 HIT (L%d)! Object=%p (%s/%s) → managed=%p +0x%X",
                foundLevel, reinterpret_cast<void*>(foundObj46), name.c_str(), className.c_str(),
                reinterpret_cast<void*>(foundManagedObj), static_cast<unsigned>(foundManagedOff));

            json resp = OkResponse(id);
            resp["found"]           = true;
            resp["match_type"]      = "managed_instance";
            resp["address"]         = AddrToHex(foundObj46);
            resp["name"]            = SafeStr(name);
            resp["class_name"]      = SafeStr(className);
            resp["slot_index"]      = foundSlot46;
            resp["managed_object"]  = AddrToHex(foundManagedObj);
            resp["offset_from_base"]= static_cast<int>(foundManagedOff);
            return resp;
        }
    }

    // Phase 5: Container owner search — target might be inside an Array or Dictionary
    // that's a value of a GDScript member variable (not the member itself).
    auto containerMatch = Ubel::FindContainerOwnerByAddress(addr);
    Solitaer::Debug("PIPE", "find_by_address: Phase 5 (container owner): obj=%p memberIdx=%d type=%s",
        reinterpret_cast<void*>(containerMatch.objectAddr), containerMatch.memberIndex,
        containerMatch.containerType.empty() ? "(none)" : containerMatch.containerType.c_str());
    if (containerMatch.objectAddr) {
        std::string name      = ReadObjectName(containerMatch.objectAddr);
        std::string className = Diagoldze::GetObjectClass(containerMatch.objectAddr);
        int slotIndex = Ausetiese::FindByAddress(containerMatch.objectAddr);

        json resp = OkResponse(id);
        resp["found"]          = true;
        resp["match_type"]     = "container_member";
        resp["address"]        = AddrToHex(containerMatch.objectAddr);
        resp["name"]           = SafeStr(name);
        resp["class_name"]     = SafeStr(className);
        resp["slot_index"]     = slotIndex;
        resp["member_offset"]  = 0;
        resp["script_member"]  = containerMatch.memberIndex;
        resp["container_type"] = containerMatch.containerType;
        return resp;
    }

    // Phase 6: Native container body scan — target might be inside a Dictionary/Array
    // that's a native C++ property on the Object body (not exposed via GDScript).
    auto nativeMatch = Ubel::FindNativeContainerByAddress(addr);
    Solitaer::Debug("PIPE", "find_by_address: Phase 6 (native container): obj=%p memberIdx=%d type=%s",
        reinterpret_cast<void*>(nativeMatch.objectAddr), nativeMatch.memberIndex,
        nativeMatch.containerType.empty() ? "(none)" : nativeMatch.containerType.c_str());
    if (nativeMatch.objectAddr) {
        std::string name      = ReadObjectName(nativeMatch.objectAddr);
        std::string className = Diagoldze::GetObjectClass(nativeMatch.objectAddr);
        int slotIndex = Ausetiese::FindByAddress(nativeMatch.objectAddr);

        json resp = OkResponse(id);
        resp["found"]          = true;
        resp["match_type"]     = "native_container";
        resp["address"]        = AddrToHex(nativeMatch.objectAddr);
        resp["name"]           = SafeStr(name);
        resp["class_name"]     = SafeStr(className);
        resp["slot_index"]     = slotIndex;
        resp["member_offset"]  = 0;
        resp["script_member"]  = nativeMatch.memberIndex;
        resp["container_type"] = nativeMatch.containerType;
        return resp;
    }

    // Shared time budget for Phase 7 + 7b + 7c combined (dict chain chase)
    constexpr int PHASE7_BUDGET_SEC = 15;
    auto phase7Start = std::chrono::steady_clock::now();
    int phase7ObjScanned = 0;
    bool phase7TimedOut = false;
    auto checkPhase7Budget = [&]() -> bool {
        if ((++phase7ObjScanned & 0x3FF) == 0) {
            auto elapsed = std::chrono::steady_clock::now() - phase7Start;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= PHASE7_BUDGET_SEC) {
                phase7TimedOut = true;
                return false; // budget exhausted
            }
        }
        return true;
    };

    // Phase 7: Backward Dictionary element chase.
    // Detects if the target is inside a HashMap element (Dictionary entry),
    // follows the prev chain to find the head, then scans ObjectDB for an
    // Object containing a DictionaryPrivate* whose HashMap head matches.
    //
    // Supports two element layouts:
    //   Standard: next(8) + prev(8) + key(stride) + value(stride)     = elemSize 0x40
    //   Extended: classPtr(8) + hash(8) + next(8) + prev(8) + key + value = elemSize 0x50
    //     (custom Godot builds with extra metadata per HashMap element)
    {
        int stride = DynOff::VARIANT_STRIDE;
        int maxType = GodotVer::VariantTypeMax();

        struct ElemLayout {
            int nextOff;    // offset of next pointer within element
            int prevOff;    // offset of prev pointer within element
            int keyOff;     // offset of key Variant
            int valOff;     // offset of value Variant
            int elemSize;   // total element size
            const char* label;
        };
        ElemLayout layouts[] = {
            {0x00, 0x08, 0x10, 0x10 + stride, 0x10 + stride * 2, "std"},
            {0x10, 0x18, 0x20, 0x20 + stride, 0x20 + stride * 2, "ext"},
        };

        Solitaer::Info("PIPE", "find_by_address: Phase 7 (dict element chase) stride=0x%X", stride);

        for (auto& lay : layouts) {
            // Try to find an element base such that target is within [base, base+elemSize)
            // and the key/value types are valid Variant types.
            for (int backOff = 0; backOff < lay.elemSize; backOff += 8) {
                if (addr < static_cast<uint64_t>(backOff)) continue;
                uint64_t elemBase = addr - backOff;

                uint64_t next = Zoltraak::ReadSafe<uint64_t>(elemBase + lay.nextOff, 0);
                uint64_t prev = Zoltraak::ReadSafe<uint64_t>(elemBase + lay.prevOff, 0);

                bool nextOk = (next == 0) || (Zoltraak::IsReadable(next) && (next % 8 == 0));
                bool prevOk = (prev == 0) || (Zoltraak::IsReadable(prev) && (prev % 8 == 0));
                if (!nextOk || !prevOk) continue;
                if (!next && !prev) continue;

                int32_t kType = Zoltraak::ReadSafe<int32_t>(elemBase + lay.keyOff, -1);
                int32_t vType = Zoltraak::ReadSafe<int32_t>(elemBase + lay.valOff, -1);
                if (kType < 0 || kType >= maxType || vType < 0 || vType >= maxType) continue;

                // For extended layout, validate the extra prefix: classPtr should be a
                // valid pointer and hash should NOT be a valid pointer (high bits set).
                if (lay.nextOff == 0x10) {
                    uint64_t classPtr = Zoltraak::ReadSafe<uint64_t>(elemBase, 0);
                    uint64_t hashVal  = Zoltraak::ReadSafe<uint64_t>(elemBase + 8, 0);
                    if (!classPtr || !Zoltraak::IsReadable(classPtr)) continue;
                    if (hashVal != 0 && Zoltraak::IsReadable(hashVal)) continue; // hash shouldn't be a valid ptr
                }

                Solitaer::Info("PIPE", "  Phase 7 [%s] elem at %p (backOff=0x%X) "
                    "next=%p prev=%p key=%d val=%d",
                    lay.label, reinterpret_cast<void*>(elemBase), backOff,
                    reinterpret_cast<void*>(next), reinterpret_cast<void*>(prev),
                    kType, vType);

                // Follow prev chain to find head (prev == NULL)
                uint64_t head = elemBase;
                int walked = 0;
                while (walked < 10000) {
                    uint64_t prevPtr = Zoltraak::ReadSafe<uint64_t>(head + lay.prevOff, 0);
                    if (!prevPtr) break;
                    if (!Zoltraak::IsReadable(prevPtr) || prevPtr % 8 != 0) { head = 0; break; }
                    head = prevPtr;
                    walked++;
                }
                if (!head || walked >= 10000) continue;

                Solitaer::Info("PIPE", "  Phase 7: head at %p (walked %d prevs)",
                    reinterpret_cast<void*>(head), walked);

                // Search ObjectDB for an Object whose body or script members contain
                // a DictionaryPrivate* that has our head as its HashMap head_element.
                // Scan the first DICT_PROBE bytes of each candidate for the head value
                // to handle any DictionaryPrivate/HashMap layout variation.
                constexpr int BODY_SCAN = 0x400;
                constexpr int DICT_PROBE = 0x200; // scan DictPrivate[0..0x200) for head ptr
                uint64_t foundObj = 0;
                int foundOff = -1;
                std::string foundHow;
                int totalPtrsChecked = 0;
                int dictProbesTried = 0;

                // Helper: check if candidate pointer is a DictPrivate whose HashMap
                // stores our head element. Scans first DICT_PROBE bytes for match.
                auto isDictWithHead = [&](uint64_t dp) -> bool {
                    dictProbesTried++;
                    uint8_t dbuf[DICT_PROBE];
                    if (!Zoltraak::ReadSafe(dp, dbuf, DICT_PROBE)) return false;
                    for (int i = 8; i + 8 <= DICT_PROBE; i += 8) {
                        uint64_t v;
                        memcpy(&v, dbuf + i, sizeof(v));
                        if (v == head) return true;
                    }
                    return false;
                };

                // Helper: check if an object is a script definition (GDScript, etc.)
                // These contain internal HashMaps (member_indices, member_info) that
                // produce false positives — they're script definitions, not game instances.
                auto isScriptDefinition = [](uint64_t objAddr) -> bool {
                    std::string cls = Diagoldze::GetObjectClass(objAddr);
                    return cls == "GDScript" || cls == "GDScriptNativeClass" ||
                           cls == "CSharpScript" || cls == "Script" ||
                           cls == "VisualScript" || cls == "ScriptExtension";
                };

                Ausetiese::ForEachEx([&](int /*index*/, uint64_t objAddr) {
                    if (!checkPhase7Budget()) return false;
                    // Skip script definition objects — their internal HashMaps
                    // (member_indices etc.) cause false positive matches
                    if (isScriptDefinition(objAddr)) return true;

                    // Scan object body for pointer-sized values
                    uint8_t buf[BODY_SCAN];
                    if (!Zoltraak::ReadSafe(objAddr, buf, BODY_SCAN)) return true;

                    for (int off = 8; off + 8 <= BODY_SCAN; off += 8) {
                        uint64_t val;
                        memcpy(&val, buf + off, sizeof(val));
                        if (!val || (val & 0x7) != 0) continue;
                        if ((val >> 40) == 0) continue; // too low for heap
                        if (!Zoltraak::IsReadable(val)) continue;
                        totalPtrsChecked++;

                        // Try val as DictionaryPrivate*
                        if (isDictWithHead(val)) {
                            foundObj = objAddr;
                            foundOff = off;
                            foundHow = "native_body";
                            return false;
                        }
                    }

                    // Also check GDScript member Variants for Dictionary data = DictPrivate*
                    if (DynOff::OBJECT_SCRIPT_INSTANCE >= 0) {
                        uint64_t si = Zoltraak::ReadSafe<uint64_t>(
                            objAddr + DynOff::OBJECT_SCRIPT_INSTANCE, 0);
                        if (si && Zoltraak::IsReadable(si)) {
                            uint64_t owner = Zoltraak::ReadSafe<uint64_t>(
                                si + DynOff::GDSCRIPT_OWNER_OFF, 0);
                            if (owner == objAddr) {
                                uint64_t mp = Zoltraak::ReadSafe<uint64_t>(
                                    si + DynOff::GDSCRIPT_MEMBERS_OFF, 0);
                                if (mp && Zoltraak::IsReadable(mp)) {
                                    // Scan members until invalid type (don't trust CowData count —
                                    // custom builds may store refcount instead of size at ptr-8)
                                    int maxType2 = GodotVer::VariantTypeMax();
                                    for (uint32_t m = 0; m < 100; m++) {
                                        int32_t mType = Zoltraak::ReadSafe<int32_t>(
                                            mp + static_cast<uint64_t>(m) * stride, -1);
                                        if (mType < 0 || mType >= maxType2) break;
                                        uint64_t d = Zoltraak::ReadSafe<uint64_t>(
                                            mp + static_cast<uint64_t>(m) * stride +
                                            DynOff::VARIANT_DATA, 0);
                                        if (!d || !Zoltraak::IsReadable(d)) continue;
                                        if (isDictWithHead(d)) {
                                            foundObj = objAddr;
                                            foundOff = static_cast<int>(m);
                                            foundHow = "script_member";
                                            return false;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    return true;
                });

                Solitaer::Info("PIPE", "  Phase 7: ObjectDB scan done. ptrs=%d probes=%d found=%s",
                    totalPtrsChecked, dictProbesTried, foundObj ? "YES" : "NO");

                if (foundObj) {
                    std::string name      = ReadObjectName(foundObj);
                    std::string className = Diagoldze::GetObjectClass(foundObj);
                    int slotIndex = Ausetiese::FindByAddress(foundObj);

                    Solitaer::Info("PIPE", "  Phase 7 HIT! Object=%p (%s/%s) %s off=0x%X",
                        reinterpret_cast<void*>(foundObj),
                        name.c_str(), className.c_str(),
                        foundHow.c_str(), foundOff);

                    json resp = OkResponse(id);
                    resp["found"]          = true;
                    resp["match_type"]     = "dict_element_chase";
                    resp["address"]        = AddrToHex(foundObj);
                    resp["name"]           = SafeStr(name);
                    resp["class_name"]     = SafeStr(className);
                    resp["slot_index"]     = slotIndex;
                    resp["member_offset"]  = foundOff;
                    resp["script_member"]  = -1;
                    resp["container_type"] = "Dictionary";
                    resp["dict_chain_len"] = walked;
                    return resp;
                }
            } // backOff
        } // layouts
    }

    // Phase 7b: Nested dictionary search.
    // If Phase 7 found a valid element chain but couldn't locate DictPrivate in any
    // ObjectDB object, the dictionary is likely nested inside another dictionary.
    // Chain: Object → member Dict → element value (Dict) → inner DictPrivate → head.
    // We scan all script member dictionaries, walk their elements, and for any
    // DICTIONARY-valued element, check if the inner DictPrivate contains our head.
    if (!phase7TimedOut) {
        int stride = DynOff::VARIANT_STRIDE;
        int maxType = GodotVer::VariantTypeMax();

        // Re-detect the element chain for the first valid layout match
        struct ElemLayout {
            int nextOff, prevOff, keyOff, valOff, elemSize;
            const char* label;
        };
        ElemLayout layouts[] = {
            {0x00, 0x08, 0x10, 0x10 + stride, 0x10 + stride * 2, "std"},
            {0x10, 0x18, 0x20, 0x20 + stride, 0x20 + stride * 2, "ext"},
        };

        // Find head from the target element (same logic as Phase 7)
        // Try ALL valid backOff/layout combos and pick the chain with the MOST
        // prev-walked elements — a longer chain is more likely to be the real dict.
        uint64_t foundHead = 0;
        int foundWalked = -1;
        for (auto& lay : layouts) {
            for (int backOff = 0; backOff < lay.elemSize; backOff += 8) {
                if (addr < static_cast<uint64_t>(backOff)) continue;
                uint64_t eb = addr - backOff;
                uint64_t nx = Zoltraak::ReadSafe<uint64_t>(eb + lay.nextOff, 0);
                uint64_t pv = Zoltraak::ReadSafe<uint64_t>(eb + lay.prevOff, 0);
                bool nxOk = (nx == 0) || (Zoltraak::IsReadable(nx) && (nx % 8 == 0));
                bool pvOk = (pv == 0) || (Zoltraak::IsReadable(pv) && (pv % 8 == 0));
                if (!nxOk || !pvOk || (!nx && !pv)) continue;
                int32_t kT = Zoltraak::ReadSafe<int32_t>(eb + lay.keyOff, -1);
                int32_t vT = Zoltraak::ReadSafe<int32_t>(eb + lay.valOff, -1);
                if (kT < 0 || kT >= maxType || vT < 0 || vT >= maxType) continue;
                if (lay.nextOff == 0x10) {
                    uint64_t cp = Zoltraak::ReadSafe<uint64_t>(eb, 0);
                    uint64_t hv = Zoltraak::ReadSafe<uint64_t>(eb + 8, 0);
                    if (!cp || !Zoltraak::IsReadable(cp)) continue;
                    if (hv != 0 && Zoltraak::IsReadable(hv)) continue;
                }
                uint64_t h = eb;
                int w = 0;
                while (w < 10000) {
                    uint64_t pp = Zoltraak::ReadSafe<uint64_t>(h + lay.prevOff, 0);
                    if (!pp) break;
                    if (!Zoltraak::IsReadable(pp) || pp % 8 != 0) { h = 0; break; }
                    h = pp; w++;
                }
                if (h && w < 10000 && w > foundWalked) {
                    foundHead = h;
                    foundWalked = w;
                }
            }
        }

        if (foundHead) {
            Solitaer::Info("PIPE", "find_by_address: Phase 7b (nested dict) head=%p walked=%d",
                reinterpret_cast<void*>(foundHead), foundWalked);

            constexpr int DICT_PROBE = 0x200;
            auto isDictWithHead = [&](uint64_t dp) -> bool {
                uint8_t dbuf[DICT_PROBE];
                if (!Zoltraak::ReadSafe(dp, dbuf, DICT_PROBE)) return false;
                for (int i = 8; i + 8 <= DICT_PROBE; i += 8) {
                    uint64_t v;
                    memcpy(&v, dbuf + i, sizeof(v));
                    if (v == foundHead) return true;
                }
                return false;
            };

            // Dict type in raw Variant type space
            int dictType = GodotVer::IsGodot3() ? 18 : 27;

            uint64_t foundObj = 0;
            int foundMember = -1;
            int nestedDictsChecked = 0;

            Ausetiese::ForEachEx([&](int /*index*/, uint64_t objAddr) {
                if (!checkPhase7Budget()) return false;
                if (DynOff::OBJECT_SCRIPT_INSTANCE < 0) return true;
                uint64_t si = Zoltraak::ReadSafe<uint64_t>(
                    objAddr + DynOff::OBJECT_SCRIPT_INSTANCE, 0);
                if (!si || !Zoltraak::IsReadable(si)) return true;
                uint64_t owner = Zoltraak::ReadSafe<uint64_t>(si + DynOff::GDSCRIPT_OWNER_OFF, 0);
                if (owner != objAddr) return true;
                uint64_t mp = Zoltraak::ReadSafe<uint64_t>(si + DynOff::GDSCRIPT_MEMBERS_OFF, 0);
                if (!mp || !Zoltraak::IsReadable(mp)) return true;

                // Scan members until invalid type (don't trust CowData count)
                int maxType2 = GodotVer::VariantTypeMax();
                for (uint32_t m = 0; m < 100; m++) {
                    uint64_t varAddr = mp + static_cast<uint64_t>(m) * stride;
                    int32_t rawType = Zoltraak::ReadSafe<int32_t>(varAddr + DynOff::VARIANT_TYPE, -1);
                    if (rawType < 0 || rawType >= maxType2) break;
                    if (rawType != dictType) continue;

                    // Read the outer DictionaryPrivate
                    uint64_t outerDP = Zoltraak::ReadSafe<uint64_t>(
                        varAddr + DynOff::VARIANT_DATA, 0);
                    if (!outerDP || !Zoltraak::IsReadable(outerDP)) continue;

                    // First check: maybe the outer dict itself holds our head
                    if (isDictWithHead(outerDP)) {
                        foundObj = objAddr;
                        foundMember = static_cast<int>(m);
                        return false;
                    }

                    // Read outer dict's head and walk elements looking for nested dicts
                    // Use ReadDictionaryInternals-style probe to find outer head
                    uint64_t hmBase = outerDP + 0x10;
                    struct { int headOff; int numOff; } pats[] = {
                        {0x18, 0x2C}, {0x10, 0x24}, {0x20, 0x34}, {0x28, 0x3C}
                    };
                    for (auto& pat : pats) {
                        uint64_t oHead = Zoltraak::ReadSafe<uint64_t>(hmBase + pat.headOff, 0);
                        uint32_t oNum = Zoltraak::ReadSafe<uint32_t>(hmBase + pat.numOff, 0);
                        if (oNum == 0 || oNum > 100000 || !oHead || !Zoltraak::IsReadable(oHead))
                            continue;

                        // Try both element prefixes for the outer dictionary
                        for (int p : {0, 0x10}) {
                            uint64_t oPrev = Zoltraak::ReadSafe<uint64_t>(oHead + p + 0x08, 0);
                            if (oPrev != 0) continue; // head's prev must be NULL

                            int valOff = p + 0x10 + stride; // value Variant offset within element
                            uint64_t cur = oHead;
                            int walked = 0;
                            int maxWalk = (std::min)(static_cast<int>(oNum), 500);
                            while (cur && walked < maxWalk) {
                                int32_t vType = Zoltraak::ReadSafe<int32_t>(cur + valOff, -1);
                                if (vType == dictType) {
                                    // Value is a Dictionary — read inner DictPrivate
                                    uint64_t innerDP = Zoltraak::ReadSafe<uint64_t>(
                                        cur + valOff + DynOff::VARIANT_DATA, 0);
                                    if (innerDP && Zoltraak::IsReadable(innerDP)) {
                                        nestedDictsChecked++;
                                        if (isDictWithHead(innerDP)) {
                                            foundObj = objAddr;
                                            foundMember = static_cast<int>(m);
                                            return false;
                                        }
                                    }
                                }
                                cur = Zoltraak::ReadSafe<uint64_t>(cur + p, 0);
                                walked++;
                            }
                        }
                    }
                }
                return true;
            });

            Solitaer::Info("PIPE", "  Phase 7b: nested dicts checked=%d found=%s",
                nestedDictsChecked, foundObj ? "YES" : "NO");

            if (foundObj) {
                std::string name      = ReadObjectName(foundObj);
                std::string className = Diagoldze::GetObjectClass(foundObj);
                int slotIndex = Ausetiese::FindByAddress(foundObj);

                Solitaer::Info("PIPE", "  Phase 7b HIT! Object=%p (%s/%s) member=%d",
                    reinterpret_cast<void*>(foundObj),
                    name.c_str(), className.c_str(), foundMember);

                json resp = OkResponse(id);
                resp["found"]          = true;
                resp["match_type"]     = "dict_element_chase";
                resp["address"]        = AddrToHex(foundObj);
                resp["name"]           = SafeStr(name);
                resp["class_name"]     = SafeStr(className);
                resp["slot_index"]     = slotIndex;
                resp["member_offset"]  = foundMember;
                resp["script_member"]  = -1;
                resp["container_type"] = "Dictionary";
                resp["dict_chain_len"] = foundWalked;
                return resp;
            }
        }

        // Phase 7c: Reverse pointer scan.
        // If Phase 7 found a valid head but couldn't locate DictPrivate in any
        // ObjectDB scan (DICT_PROBE too small, or DictPrivate not directly in
        // object body), scan memory pages near the head to find the DictPrivate
        // struct that contains a pointer to head. Then trace DictPrivate → ObjectDB.
        if (foundHead && !phase7TimedOut) {
            Solitaer::Info("PIPE", "find_by_address: Phase 7c (reverse ptr scan) head=%p",
                reinterpret_cast<void*>(foundHead));

            constexpr int PAGE_SIZE = 0x10000; // 64KB scan region
            int stride = DynOff::VARIANT_STRIDE;
            int dictType = GodotVer::IsGodot3() ? 18 : 27;

            // Scan a region around the head for any qword matching the head value.
            // The DictPrivate is usually allocated in the same heap region.
            // Try 256KB before and 64KB after the head address.
            std::vector<uint64_t> dictPrivateCandidates;

            for (int regionIdx = -4; regionIdx <= 1; regionIdx++) {
                uint64_t scanBase = (foundHead & ~0xFFFFULL) +
                    static_cast<int64_t>(regionIdx) * PAGE_SIZE;
                if (scanBase == 0) continue;

                uint8_t pageBuf[PAGE_SIZE];
                if (!Zoltraak::ReadSafe(scanBase, pageBuf, PAGE_SIZE)) continue;

                for (int off = 0; off + 8 <= PAGE_SIZE; off += 8) {
                    uint64_t v;
                    memcpy(&v, pageBuf + off, sizeof(v));
                    if (v == foundHead) {
                        uint64_t matchAddr = scanBase + off;
                        // The head pointer is at some offset within DictPrivate.
                        // Try offsets 0x08..0x100 as the head field offset.
                        for (int hOff = 0x08; hOff <= 0x100; hOff += 0x08) {
                            uint64_t dpCandidate = matchAddr - hOff;
                            if (dpCandidate == 0) continue;
                            // Basic sanity: first 4 bytes should be a small refcount (1..1000)
                            uint32_t rc = Zoltraak::ReadSafe<uint32_t>(dpCandidate, 0);
                            if (rc >= 1 && rc <= 1000) {
                                dictPrivateCandidates.push_back(dpCandidate);
                            }
                        }
                    }
                }
            }

            Solitaer::Info("PIPE", "  Phase 7c: found %zu DictPrivate candidates from reverse scan",
                dictPrivateCandidates.size());

            // For each DictPrivate candidate, search ObjectDB for who holds it.
            uint64_t foundObj = 0;
            int foundMember = -1;
            std::string foundHow;
            uint64_t foundDP = 0;

            for (auto dpCand : dictPrivateCandidates) {
                if (foundObj || phase7TimedOut) break;

                Ausetiese::ForEachEx([&](int /*index*/, uint64_t objAddr) {
                    if (!checkPhase7Budget()) return false;
                    // Skip script definitions (GDScript internal HashMaps)
                    std::string cls = Diagoldze::GetObjectClass(objAddr);
                    if (cls == "GDScript" || cls == "GDScriptNativeClass" ||
                        cls == "CSharpScript" || cls == "Script" ||
                        cls == "VisualScript" || cls == "ScriptExtension")
                        return true;

                    // Scan object body for pointer to this DictPrivate
                    constexpr int BODY_SCAN = 0x400;
                    uint8_t buf[BODY_SCAN];
                    if (!Zoltraak::ReadSafe(objAddr, buf, BODY_SCAN)) return true;

                    for (int off = 8; off + 8 <= BODY_SCAN; off += 8) {
                        uint64_t val;
                        memcpy(&val, buf + off, sizeof(val));
                        if (val == dpCand) {
                            foundObj = objAddr;
                            foundMember = off;
                            foundHow = "native_body_rev";
                            foundDP = dpCand;
                            return false;
                        }
                    }

                    // Check script members for Variant data == dpCand
                    if (DynOff::OBJECT_SCRIPT_INSTANCE >= 0) {
                        uint64_t si = Zoltraak::ReadSafe<uint64_t>(
                            objAddr + DynOff::OBJECT_SCRIPT_INSTANCE, 0);
                        if (si && Zoltraak::IsReadable(si)) {
                            uint64_t owner = Zoltraak::ReadSafe<uint64_t>(
                                si + DynOff::GDSCRIPT_OWNER_OFF, 0);
                            if (owner == objAddr) {
                                uint64_t mp = Zoltraak::ReadSafe<uint64_t>(
                                    si + DynOff::GDSCRIPT_MEMBERS_OFF, 0);
                                if (mp && Zoltraak::IsReadable(mp)) {
                                    // Scan members until invalid type (don't trust CowData count —
                                    // custom builds may store refcount instead of size at ptr-8)
                                    int maxType2 = GodotVer::VariantTypeMax();
                                    for (uint32_t m = 0; m < 100; m++) {
                                        int32_t mType = Zoltraak::ReadSafe<int32_t>(
                                            mp + static_cast<uint64_t>(m) * stride, -1);
                                        if (mType < 0 || mType >= maxType2) break;
                                        uint64_t d = Zoltraak::ReadSafe<uint64_t>(
                                            mp + static_cast<uint64_t>(m) * stride +
                                            DynOff::VARIANT_DATA, 0);
                                        if (d == dpCand) {
                                            foundObj = objAddr;
                                            foundMember = static_cast<int>(m);
                                            foundHow = "script_member_rev";
                                            foundDP = dpCand;
                                            return false;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    return true;
                });
            }

            // If direct DictPrivate not found in ObjectDB, it might be nested.
            // Walk outer dict elements looking for dpCand as a value's data field.
            // Chain: Object → member(Dict) → DictPrivate_outer → element → value(Dict)
            //        → DictPrivate_inner(==dpCand) → head
            if (!foundObj && !phase7TimedOut && !dictPrivateCandidates.empty()) {
                int nestedWalked = 0;
                for (auto dpCand : dictPrivateCandidates) {
                    if (foundObj || phase7TimedOut) break;

                    Ausetiese::ForEachEx([&](int /*index*/, uint64_t objAddr) {
                        if (!checkPhase7Budget()) return false;
                        if (DynOff::OBJECT_SCRIPT_INSTANCE < 0) return true;
                        uint64_t si = Zoltraak::ReadSafe<uint64_t>(
                            objAddr + DynOff::OBJECT_SCRIPT_INSTANCE, 0);
                        if (!si || !Zoltraak::IsReadable(si)) return true;
                        uint64_t owner = Zoltraak::ReadSafe<uint64_t>(
                            si + DynOff::GDSCRIPT_OWNER_OFF, 0);
                        if (owner != objAddr) return true;
                        uint64_t mp = Zoltraak::ReadSafe<uint64_t>(
                            si + DynOff::GDSCRIPT_MEMBERS_OFF, 0);
                        if (!mp || !Zoltraak::IsReadable(mp)) return true;

                        // Scan members until invalid type (don't trust CowData count)
                        int maxType3 = GodotVer::VariantTypeMax();
                        for (uint32_t m = 0; m < 100; m++) {
                            uint64_t varAddr = mp + static_cast<uint64_t>(m) * stride;
                            int32_t rawType = Zoltraak::ReadSafe<int32_t>(
                                varAddr + DynOff::VARIANT_TYPE, -1);
                            if (rawType < 0 || rawType >= maxType3) break;
                            if (rawType != dictType) continue;
                            uint64_t outerDP = Zoltraak::ReadSafe<uint64_t>(
                                varAddr + DynOff::VARIANT_DATA, 0);
                            if (!outerDP || !Zoltraak::IsReadable(outerDP)) continue;

                            // Walk outer dict's HashMap elements, looking for a
                            // value Variant whose data == dpCand (inner DictPrivate).
                            // Try multiple HashMap head offsets and element prefixes.
                            uint64_t hmBase = outerDP + 0x10;
                            struct { int headOff; int numOff; } pats[] = {
                                {0x18, 0x2C}, {0x10, 0x24}, {0x20, 0x34},
                                {0x28, 0x3C}, {0x08, 0x1C}, {0x00, 0x14}
                            };
                            for (auto& pat : pats) {
                                if (foundObj) break;
                                uint64_t oHead = Zoltraak::ReadSafe<uint64_t>(
                                    hmBase + pat.headOff, 0);
                                uint32_t oNum = Zoltraak::ReadSafe<uint32_t>(
                                    hmBase + pat.numOff, 0);
                                if (oNum == 0 || oNum > 100000 ||
                                    !oHead || !Zoltraak::IsReadable(oHead))
                                    continue;

                                for (int p : {0, 0x10}) {
                                    if (foundObj) break;
                                    uint64_t oPrev = Zoltraak::ReadSafe<uint64_t>(
                                        oHead + p + 0x08, 0);
                                    if (oPrev != 0) continue;

                                    int valOff = p + 0x10 + stride;
                                    uint64_t cur = oHead;
                                    int walked = 0;
                                    int maxWalk = (std::min)(
                                        static_cast<int>(oNum), 2000);
                                    while (cur && walked < maxWalk) {
                                        int32_t vType = Zoltraak::ReadSafe<int32_t>(
                                            cur + valOff, -1);
                                        if (vType == dictType) {
                                            uint64_t innerDP =
                                                Zoltraak::ReadSafe<uint64_t>(
                                                    cur + valOff +
                                                    DynOff::VARIANT_DATA, 0);
                                            if (innerDP == dpCand) {
                                                foundObj = objAddr;
                                                foundMember = static_cast<int>(m);
                                                foundHow = "nested_elem_rev";
                                                foundDP = dpCand;
                                                return false;
                                            }
                                        }
                                        nestedWalked++;
                                        cur = Zoltraak::ReadSafe<uint64_t>(
                                            cur + p, 0);
                                        walked++;
                                    }
                                }
                            }
                        }
                        return true;
                    });
                }
                Solitaer::Info("PIPE", "  Phase 7c nested walk: %d elements walked",
                    nestedWalked);
            }

            Solitaer::Info("PIPE", "  Phase 7c: found=%s dp=%p how=%s",
                foundObj ? "YES" : "NO",
                reinterpret_cast<void*>(foundDP),
                foundHow.empty() ? "(none)" : foundHow.c_str());

            if (foundObj) {
                std::string name      = ReadObjectName(foundObj);
                std::string className = Diagoldze::GetObjectClass(foundObj);
                int slotIndex = Ausetiese::FindByAddress(foundObj);

                Solitaer::Info("PIPE", "  Phase 7c HIT! Object=%p (%s/%s) %s member=%d",
                    reinterpret_cast<void*>(foundObj),
                    name.c_str(), className.c_str(),
                    foundHow.c_str(), foundMember);

                json resp = OkResponse(id);
                resp["found"]          = true;
                resp["match_type"]     = "dict_element_chase";
                resp["address"]        = AddrToHex(foundObj);
                resp["name"]           = SafeStr(name);
                resp["class_name"]     = SafeStr(className);
                resp["slot_index"]     = slotIndex;
                resp["member_offset"]  = foundMember;
                resp["script_member"]  = -1;
                resp["container_type"] = "Dictionary";
                resp["dict_chain_len"] = foundWalked;
                return resp;
            }
        }

    }

    if (phase7TimedOut) {
        Solitaer::Warn("PIPE", "find_by_address: Phase 7 dict chase timed out after %ds (%d objects scanned)",
            PHASE7_BUDGET_SEC, phase7ObjScanned);
    }

    // Not found in any phase
    Solitaer::Warn("PIPE", "find_by_address: %s NOT FOUND in any phase%s",
        addrStr.c_str(), phase7TimedOut ? " (dict chase timed out)" : "");
    json resp = OkResponse(id);
    resp["found"]   = false;
    resp["address"] = addrStr;
    return resp;
}

// cmd: walk_class
static json HandleWalkClass(int id, const json& req) {
    std::string className = req.value("class_name", std::string{});
    if (className.empty()) return ErrorResponse(id, "Missing 'class_name' field");

    auto classInfo = Diagoldze::GetClassInfo(className.c_str());
    if (classInfo.name.empty())
        return ErrorResponse(id, "Class not found: " + className);

    json properties = json::array();
    for (auto& prop : classInfo.properties) {
        json f;
        f["name"]        = SafeStr(prop.name);
        f["type"]        = VariantTypeName(prop.type);
        f["offset"]      = prop.offset;
        f["size"]        = prop.nativeSize > 0 ? prop.nativeSize : 0;
        if (prop.bitShift) f["bit_shift"] = prop.bitShift;
        if (prop.bitMask)  f["bit_mask"]  = prop.bitMask;
        f["hint"]        = prop.hint;
        f["hint_string"] = SafeStr(prop.hintString);
        f["usage"]       = static_cast<int>(prop.usage);
        properties.push_back(std::move(f));
    }

    json methods = json::array();
    for (auto& m : classInfo.methods) {
        json method;
        method["name"]        = SafeStr(m.name);
        method["return_type"] = SafeStr(m.returnType);
        method["arg_count"]   = m.argCount;
        methods.push_back(std::move(method));
    }

    json resp = OkResponse(id);
    resp["class_name"]  = SafeStr(classInfo.name);
    resp["super_name"]  = SafeStr(classInfo.parentName);
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
    std::string className = Diagoldze::GetObjectClass(addr);

    // If not a Godot Object, check if it's a .NET managed object
    bool isManagedObject = false;
    if (className.empty() && DynOff::bIsDotNetGame.load(std::memory_order_acquire)) {
        uint64_t mt = Zoltraak::ReadSafe<uint64_t>(addr, 0);
        if (mt && (mt & 0x7) == 0 && (mt >> 40) >= 0x7F && Zoltraak::IsReadable(mt)) {
            isManagedObject = true;
            className = "ManagedObject";
            // Try to read BaseSize from MethodTable for display
            uint32_t baseSize = Zoltraak::ReadSafe<uint32_t>(mt + 0x04, 0);
            if (baseSize >= 0x10 && baseSize <= 0x2000) {
                char mtName[80];
                snprintf(mtName, sizeof(mtName), "MT:%llX (size=0x%X)",
                    static_cast<unsigned long long>(mt), baseSize);
                name = mtName;
            } else {
                char mtName[64];
                snprintf(mtName, sizeof(mtName), "MT:%llX",
                    static_cast<unsigned long long>(mt));
                name = mtName;
            }
        }
    }

    Solitaer::Debug("PIPE:cmd", "walk_instance: addr=%s name='%s' class='%s'%s",
        addrStr.c_str(), name.c_str(), className.c_str(),
        isManagedObject ? " [managed]" : "");

    json fields = json::array();
    Ubel::WalkInstance(addr, [&](const Ubel::FieldEntry& entry) {
        json f;
        f["name"]         = SafeStr(entry.name);
        f["type"]         = entry.value.typeName;
        f["variant_type"] = entry.value.type;
        f["value"]        = SafeStr(entry.value.value);
        f["address"]      = entry.value.rawAddr ? AddrToHex(entry.value.rawAddr) : "";
        f["offset"]       = entry.offset;
        f["size"]         = entry.nativeSize > 0 ? entry.nativeSize : VariantTypeSize(entry.value.type);
        if (entry.bitShift) f["bit_shift"] = entry.bitShift;
        if (entry.bitMask)  f["bit_mask"]  = entry.bitMask;
        f["hint"]         = entry.hint;
        f["hint_string"]  = SafeStr(entry.hintString);
        f["usage"]        = static_cast<int>(entry.usage);
        f["is_pointer"]   = IsPointerType(entry.value.type);
        f["is_struct"]    = IsStructType(entry.value.type);
        f["is_container"] = IsContainerType(entry.value.type);
        // For container types, read element count for display
        if (IsContainerType(entry.value.type) && entry.value.rawAddr) {
            auto cInfo = Ubel::ReadContainerInfo(entry.value.rawAddr, entry.value.type);
            f["element_count"] = cInfo.elementCount;
            if (!cInfo.elementType.empty())
                f["element_type"] = SafeStr(cInfo.elementType);
        }
        f["from_ptrcall"] = entry.fromPtrcall;
        if (!entry.className.empty()) {
            f["class_name"] = SafeStr(entry.className);
        }
        // Hex column: read raw bytes at field address for hex display
        if (entry.value.rawAddr) {
            int readSize = entry.nativeSize > 0 ? entry.nativeSize : VariantTypeSize(entry.value.type);
            if (readSize <= 0) readSize = 8;
            if (readSize > 8) readSize = 8;
            uint64_t raw = 0;
            if (Zoltraak::ReadSafe(entry.value.rawAddr, &raw, readSize)) {
                char hexBuf[32];
                if (readSize <= 1)
                    snprintf(hexBuf, sizeof(hexBuf), "%02X", static_cast<uint8_t>(raw));
                else if (readSize <= 4)
                    snprintf(hexBuf, sizeof(hexBuf), "%X", static_cast<uint32_t>(raw));
                else
                    snprintf(hexBuf, sizeof(hexBuf), "%llX", static_cast<unsigned long long>(raw));
                f["hex"] = hexBuf;
            }
        }
        // For Object-type variants, provide ptr_value and ptr_class for drilldown navigation
        if (IsPointerType(entry.value.type) && entry.value.ptrTarget) {
            f["ptr_value"] = AddrToHex(entry.value.ptrTarget);
            // Try to resolve the pointed-to object's class
            std::string ptrCls = Diagoldze::GetObjectClass(entry.value.ptrTarget);
            if (!ptrCls.empty()) {
                f["ptr_class"] = SafeStr(ptrCls);
            }
        }
        fields.push_back(std::move(f));
    });

    Solitaer::Debug("PIPE:cmd", "walk_instance: %d fields returned for '%s' (%s)",
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
    resp["name"]       = SafeStr(name);
    resp["class_name"] = SafeStr(className);
    resp["parent_addr"] = parentAddr;
    resp["is_node"]    = Diagoldze::IsNodeDerived(className);
    resp["fields"]     = std::move(fields);
    return resp;
}

// cmd: walk_container — drill into Array/Dictionary/PackedArray elements
static json HandleWalkContainer(int id, const json& req) {
    std::string addrStr = GetAddrField(req);
    if (addrStr.empty()) return ErrorResponse(id, "Missing 'address' field");

    uint64_t dataAddr = HexToAddr(addrStr);
    if (!dataAddr || !Zoltraak::IsReadable(dataAddr))
        return ErrorResponse(id, "Address unreadable: " + addrStr);

    // variant_type is required to know how to interpret the container
    int variantType = req.value("variant_type", -1);
    if (variantType < 0) return ErrorResponse(id, "Missing 'variant_type' field");

    int offset = req.value("offset", 0);
    int limit  = req.value("limit", 100);
    if (limit > 1000) limit = 1000; // cap to prevent huge responses

    Solitaer::Debug("PIPE:cmd", "walk_container: addr=%s type=%d offset=%d limit=%d",
        addrStr.c_str(), variantType, offset, limit);

    // Read container info
    auto info = Ubel::ReadContainerInfo(dataAddr, variantType);

    // Read elements
    auto elements = Ubel::ReadContainerElements(dataAddr, variantType, offset, limit);

    // Build response fields (same format as walk_instance for UI reuse)
    json fields = json::array();
    for (auto& elem : elements) {
        json f;
        f["name"]         = "[" + std::to_string(elem.index) + "]";
        if (!elem.key.empty())
            f["name"]     = elem.key + " → [" + std::to_string(elem.index) + "]";
        f["type"]         = SafeStr(elem.value.typeName);
        f["variant_type"] = elem.value.type;
        f["value"]        = SafeStr(elem.value.value);
        f["address"]      = elem.value.rawAddr ? AddrToHex(elem.value.rawAddr) : "";
        f["offset"]       = -1; // Container elements don't have fixed offsets
        f["size"]         = VariantTypeSize(elem.value.type);
        f["is_pointer"]   = IsPointerType(elem.value.type);
        f["is_struct"]    = IsStructType(elem.value.type);
        f["is_container"] = IsContainerType(elem.value.type);
        f["from_ptrcall"] = false;
        // For Object-type elements, provide ptr_value and ptr_class for drilldown
        if (IsPointerType(elem.value.type) && elem.value.ptrTarget) {
            f["ptr_value"] = AddrToHex(elem.value.ptrTarget);
            std::string ptrCls = Diagoldze::GetObjectClass(elem.value.ptrTarget);
            if (!ptrCls.empty()) f["ptr_class"] = SafeStr(ptrCls);
        }
        // For nested containers, include element_count
        if (IsContainerType(elem.value.type) && elem.value.rawAddr) {
            auto nested = Ubel::ReadContainerInfo(elem.value.rawAddr, elem.value.type);
            f["element_count"] = nested.elementCount;
        }
        fields.push_back(std::move(f));
    }

    Solitaer::Info("PIPE:cmd", "walk_container: %d elements returned (total=%d, type=%s)",
        static_cast<int>(elements.size()), info.elementCount, info.containerType.c_str());

    json resp = OkResponse(id);
    resp["address"]        = AddrToHex(dataAddr);
    resp["container_type"] = SafeStr(info.containerType);
    resp["element_count"]  = info.elementCount;
    resp["element_type"]   = SafeStr(info.elementType);
    resp["key_type"]       = SafeStr(info.keyType);
    resp["fields"]         = std::move(fields);
    return resp;
}

// Recursive helper for walk_tree
static json BuildTreeNode(uint64_t nodeAddr, int depth, int maxDepth) {
    json node;
    node["address"]    = AddrToHex(nodeAddr);
    node["name"]       = SafeStr(ReadObjectName(nodeAddr));
    node["class_name"] = SafeStr(Diagoldze::GetObjectClass(nodeAddr));

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

        // Retry with short waits if root not yet available (deferred SceneTree)
        if (!startAddr) {
            for (int retry = 0; retry < 10 && !startAddr; ++retry) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                startAddr = Serie::GetRoot();
            }
        }
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
    int limit = req.value("limit", 1000);
    if (limit <= 0) limit = 1000;
    if (limit > 10000) limit = 10000;

    // Pre-compute lowercase query for partial matching
    std::string queryLower = query;
    std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    json instances = json::array();
    int found = 0;

    Ausetiese::ForEachEx([&](int index, uint64_t objAddr) {
        if (found >= limit) return false;

        std::string cls  = Diagoldze::GetObjectClass(objAddr);
        std::string name = ReadObjectName(objAddr);

        bool match = false;
        if (exact) {
            // Exact match on class name OR object name
            match = (cls == query) || (name == query);
            // Backup: try confirmed NODE_NAME offset directly.
            // ProbeObjectName may return wrong StringName for some objects
            // (e.g. a field at a lower offset gets picked up before the actual name).
            if (!match && DynOff::NODE_NAME >= 0) {
                std::string nodeName = Flamme::Resolve(objAddr + DynOff::NODE_NAME);
                if (!nodeName.empty() && nodeName != name)
                    match = (nodeName == query);
            }
        } else {
            // Partial, case-insensitive match on class name OR object name
            match = ContainsCaseInsensitive(cls, queryLower) ||
                    ContainsCaseInsensitive(name, queryLower);
        }

        if (match) {
            json obj;
            obj["address"]    = AddrToHex(objAddr);
            obj["name"]       = SafeStr(name);
            obj["class_name"] = SafeStr(cls);
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

    Diagoldze::EnumerateClasses([&](const std::string& name, uint64_t /*infoAddr*/) {
        auto info = Diagoldze::GetClassInfo(name.c_str());

        json cls;
        cls["class_name"]     = SafeStr(name);
        cls["super_name"]     = SafeStr(info.parentName);
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

    Diagoldze::EnumerateClasses([&](const std::string& className, uint64_t /*infoAddr*/) {
        if (static_cast<int>(hits.size()) >= maxHits) return;

        auto info = Diagoldze::GetClassInfo(className.c_str());
        for (auto& prop : info.properties) {
            if (static_cast<int>(hits.size()) >= maxHits) return;

            std::string propLower = prop.name;
            std::transform(propLower.begin(), propLower.end(), propLower.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (propLower.find(queryLower) != std::string::npos) {
                json hit;
                hit["class_name"]    = SafeStr(className);
                hit["super_name"]    = SafeStr(info.parentName);
                hit["property_name"] = SafeStr(prop.name);
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

    // If SceneTree still not resolved, try a full scan.
    // Re-init ObjectDB first: the game may have loaded more objects since the
    // initial DLL scan (especially with proxy DLL injection at process start).
    // For 3.x games, the ObjectDB HashMap may have been resized as well.
    if (!sceneTreeResolved) {
        // Refresh ObjectDB so ProbeRootNode sees newly-created objects
        const auto& ptrs = Frieren::GetPointers();
        if (ptrs.ObjectDB) {
            Ausetiese::Init(ptrs.ObjectDB);
        }

        uintptr_t addr = Frieren::FindSceneTree();
        if (addr) {
            Serie::Init(addr);
            sceneTreeResolved = (Serie::GetRoot() != 0);
        }
    }

    // Deferred Heiter vtable classification: runs once on first trigger_scan
    // (game is fully loaded, game thread is pumping messages).
    // Deferred vtable classification via code analysis (pure memory reads, no ptrcall).
    // Cannot run during DLL init — ClassDB and ObjectDB must be initialized first.
    {
        static std::atomic<bool> s_codeAnalysisRun{false};
        if (!s_codeAnalysisRun.exchange(true)) {
            int unresolved = Diagoldze::GetUnresolvedVtableCount();
            if (unresolved > 0) {
                Diagoldze::ClassifyVtablesViaHeiter();
            }
        }
    }

    // Deferred .NET/C# Godot detection: runs once on first trigger_scan.
    // Checks if GodotSharp module is loaded — present ONLY in .NET Godot builds.
    // When detected, WalkInstance will read managed object fields via CSharpInstance.
    {
        static std::atomic<bool> s_dotnetDetectRun{false};
        if (!s_dotnetDetectRun.exchange(true)) {
            bool isDotNet = (Zoltraak::GetModuleBase("GodotSharp") != 0);
            if (!isDotNet) {
                // Fallback: check for coreclr.dll (NativeAOT builds may not load GodotSharp as module)
                isDotNet = (Zoltraak::GetModuleBase("coreclr") != 0);
            }
            DynOff::bIsDotNetGame.store(isDotNet, std::memory_order_release);
            if (isDotNet) {
                Solitaer::Info("PIPE", ".NET Godot game detected (GodotSharp/coreclr module found)");
            }
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
    {"walk_container",     HandleWalkContainer},
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
        return resp.dump(-1, ' ', false, json::error_handler_t::replace);
    } catch (const std::exception& e) {
        LOG_ERROR("Handler exception for '%s': %s", cmd.c_str(), e.what());
        return ErrorResponse(id, std::string("Internal error: ") + e.what()).dump(-1, ' ', false, json::error_handler_t::replace);
    } catch (...) {
        // With /EHa, this catches SEH exceptions (access violations, etc.)
        LOG_ERROR("SEH/unknown exception in handler for '%s'", cmd.c_str());
        return ErrorResponse(id, "SEH exception in handler").dump();
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
