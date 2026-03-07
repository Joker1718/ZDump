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

    // ----- StringName layout -----
    inline int STRINGNAME_DATA_PTR = 0x00;  // _Data* pointer
    inline int SDATA_NAME          = -1;    // String name within _Data (probe required)
    inline int SDATA_HASH          = -1;    // uint32_t hash within _Data

    // ----- Validation state -----
    inline std::atomic<bool> bOffsetsValidated{false};
    inline std::atomic<bool> bNodeOffsetsValidated{false};
}

} // namespace Zolt
