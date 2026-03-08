#pragma once

// Grimoire — 魔導書 (Grimoire)
// Constants, magic strings, and dynamic offset namespace for Zolt-Dump.
// All magic strings and configuration values are centralized here.

#include <atomic>
#include <string>

namespace Zolt {

// ============================================================
// Application identity
// ============================================================
namespace Grimoire {
    // Pipe name for UI communication
    // Fern — 費倫 — Named pipe IPC channel
    constexpr const char* PIPE_NAME = "\\\\.\\pipe\\ZoltDumpBfx";

    // Application name
    constexpr const char* APP_NAME = "Zolt-Dump";

    // DLL name
    constexpr const char* DLL_NAME = "ZoltDumper.dll";

    // Log directory name (under %LOCALAPPDATA%)
    constexpr const char* LOG_DIR_NAME = "ZoltDump";

    // Hint cache file prefix
    constexpr const char* HINT_CACHE_PREFIX = "ZoltDump";

    // Export function prefix
    constexpr const char* EXPORT_PREFIX = "Zolt_";

    // CE Plugin pipe name (for Mailbox)
    constexpr const char* MAILBOX_NAME = "ZoltDumpMailbox";

    // ----- Logging constants -----
    constexpr const wchar_t* LOG_SUBFOLDER   = L"Logs";
    constexpr int            LOG_ROTATE_MAX  = 4;                    // 4-file rotation (0..3)
    constexpr size_t         LOG_MAX_SIZE    = 8 * 1024 * 1024;      // 8 MB per file
    constexpr int            LOG_EARLY_BUF   = 100;                  // Max buffered lines before files open

    // ----- Pipe constants -----
    constexpr int            PIPE_BUF_SIZE   = 65536;                // 64 KB pipe buffer
}

// ============================================================
// DynOff — Dynamic Offsets (runtime-adjusted)
// Like UE5CEDumper's DynOff, these are mutable at runtime.
// Validated via Frieren::ValidateAndFixOffsets()
// ============================================================
namespace DynOff {
    // ----- Object base layout -----
    // These offsets are version-dependent and validated at runtime
    inline int OBJECT_VTABLE           = 0x00;  // vtable pointer (always 0)
    inline int OBJECT_EXTENSION        = 0x08;  // ObjectGDExtension*
    inline int OBJECT_EXT_INSTANCE     = 0x10;  // GDExtensionClassInstancePtr
    inline int OBJECT_INSTANCE_ID      = 0x18;  // ObjectID (8 bytes)
    inline int OBJECT_SCRIPT_INSTANCE  = -1;    // ScriptInstance* (version-dependent, probe required)

    // ----- ObjectSlot layout (fixed) -----
    inline int SLOT_STRIDE         = 16;    // 8 bytes metadata + 8 bytes pointer
    inline int SLOT_OBJECT_OFFSET  = 8;     // Object* at offset 8 within slot

    // ----- Node::Data layout -----
    // These must be probed at runtime
    inline int NODE_DATA_OFFSET    = -1;    // Offset from Node* to Data struct
    inline int NODE_PARENT         = -1;    // Node* parent within Data
    inline int NODE_CHILDREN       = -1;    // HashMap<StringName, Node*> children
    inline int NODE_NAME           = -1;    // StringName name
    inline int NODE_TREE           = -1;    // SceneTree* tree

    // ----- Variant layout -----
    inline int VARIANT_TYPE        = 0x00;  // Variant::Type (4 bytes)
    inline int VARIANT_DATA        = 0x08;  // Union data start (after padding)

    // ----- StringName _Data layout (stable across Godot 4.0-4.4) -----
    // _Data { SafeRefCount(4B), SafeNumeric(4B), const char* cname(8B), String name(8B), int idx(4B), uint32_t hash(4B), ... }
    inline int STRINGNAME_DATA_PTR = 0x00;  // _Data* pointer within StringName
    inline int SDATA_CNAME         = 0x08;  // const char* cname within _Data (raw ASCII, .rdata pointer)
    inline int SDATA_NAME          = 0x10;  // String name within _Data (CowData<char32_t>._ptr, heap pointer)
    inline int SDATA_HASH          = 0x1C;  // uint32_t hash within _Data

    // ----- ClassDB HashMap layout -----
    inline int CLASSDB_ALLOC_OFFSET = 0x28; // PagedAllocator size before HashMap core fields (probed by Frieren)

    // ----- SceneTree layout -----
    inline int STREE_ROOT_OFFSET   = -1;    // Window* root within SceneTree (probe required)
    inline int STREE_SCENE_OFFSET  = -1;    // Node* current_scene within SceneTree (probe required)

    // ----- Validation state -----
    inline std::atomic<bool> bOffsetsValidated{false};
    inline std::atomic<bool> bNodeOffsetsValidated{false};
    inline std::atomic<bool> bStringNameProbed{false};
}

} // namespace Zolt
