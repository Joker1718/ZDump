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

    // ----- SceneTree probe constants -----
    constexpr int            STREE_PROBE_MAX = 0x4000;               // Max offset to scan for root Window*
                                                                        // Godot 4.5+ SceneTree can exceed 0x2000 bytes
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

    // ----- GDScriptInstance layout -----
    // 4.x: GDScriptInstance { vtable(8), owner_id(8), owner(8), script(8), members(8...) }
    // 3.x: GDScriptInstance { vtable(8), owner(8), script(8), members(8...) } — no owner_id
    // Defaults are for 4.x; adjusted at init for 3.x
    inline int GDSCRIPT_OWNER_OFF    = 0x10;  // Object* owner within GDScriptInstance
    inline int GDSCRIPT_SCRIPT_OFF   = 0x18;  // Ref<GDScript> script
    inline int GDSCRIPT_MEMBERS_OFF  = 0x20;  // Vector<Variant> members (CowData pointer)
    inline int VARIANT_STRIDE        = 0x18;  // sizeof(Variant) = 24 bytes for 4.x float precision
                                              // (40 bytes / 0x28 if REAL_T_IS_DOUBLE — extremely rare)
    inline std::atomic<bool> bScriptInstanceProbed{false};

    // ----- .NET / CSharp Godot detection -----
    // Set to true at init if GodotSharp module is loaded (C# Godot build).
    // When true, WalkInstance will attempt to read managed object fields
    // via CSharpInstance → GCHandle → .NET managed object chain.
    inline std::atomic<bool> bIsDotNetGame{false};

    // CSharpInstance layout (confirmed via diagnostic hexdump):
    //   +0x00: vtable (same for all CSharpInstance objects)
    //   +0x08: Object* owner (back-link to Godot Object)
    //   +0x10: flags (usually 0)
    //   +0x18: Ref<CSharpScript> script
    //   +0x20: GCHandle (pointer into CLR handle table; *gchandle = managed object)
    inline int CSHARP_OWNER_OFF   = 0x08;   // Object* owner within CSharpInstance
    inline int CSHARP_GCHANDLE_OFF = 0x20;  // GCHandle within CSharpInstance

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

    // ----- StringName _Data layout -----
    // 3.x:     _Data { SafeRefCount(4B), pad(4B), const char* cname(8B), String name(8B wchar_t CowData), ... }
    // 4.0-4.4: _Data { SafeRefCount(4B), SafeNumeric(4B), const char* cname(8B), String name(8B char32 CowData), ... }
    // 4.5+:    _Data { SafeRefCount(4B), SafeNumeric(4B), String name(8B char32 CowData), ... }  ← cname REMOVED
    inline int STRINGNAME_DATA_PTR = 0x00;  // _Data* pointer within StringName
    inline int SDATA_CNAME         = 0x08;  // const char* cname within _Data (ASCII, .rdata pointer) — absent in 4.5+
    inline int SDATA_NAME          = 0x10;  // String name within _Data (CowData<char32_t>._ptr) — moves to 0x08 in 4.5+
    inline int SDATA_HASH          = 0x1C;  // uint32_t hash within _Data

    // ----- ClassDB HashMap layout (4.x) -----
    inline int CLASSDB_ALLOC_OFFSET = 0x28; // PagedAllocator size before HashMap core fields (probed by Frieren)

    // ----- 3.x HashMap layout (bucket-chain, separate chaining) -----
    // HashMap<K,V> is 16 bytes inline: hash_table(8B) + power(1B+3B pad) + elements(4B)
    // No PagedAllocator prefix, no head/tail linked list, no capacity_index.
    inline constexpr int HASHMAP_3X_SIZE          = 16;   // Total inline size
    inline constexpr int HASHMAP_3X_HASHTABLE_OFF = 0x00; // Element** hash_table
    inline constexpr int HASHMAP_3X_POWER_OFF     = 0x08; // uint8_t hash_table_power
    inline constexpr int HASHMAP_3X_ELEMENTS_OFF  = 0x0C; // uint32_t elements (count)

    // 3.x HashMap::Element layout (bucket chain, singly-linked)
    inline constexpr int ELEM_3X_HASH_OFF  = 0x00; // uint32_t hash
    inline constexpr int ELEM_3X_NEXT_OFF  = 0x08; // Element* next (after 4B pad)
    inline constexpr int ELEM_3X_KEY_OFF   = 0x10; // TKey (e.g. StringName 8B)
    inline constexpr int ELEM_3X_VALUE_OFF = 0x18; // TData (e.g. ClassInfo or Object*)

    // ----- SceneTree layout -----
    inline int STREE_ROOT_OFFSET   = -1;    // Window* root within SceneTree (probe required)
    inline int STREE_SCENE_OFFSET  = -1;    // Node* current_scene within SceneTree (probe required)

    // ----- ClassInfo layout offsets (probed by Diagoldze::ProbeClassInfoLayout) -----
    inline int CLASSINFO_NAME_OFFSET      = -1;  // StringName 'name' field within ClassInfo
    inline int CLASSINFO_INHERITS_OFFSET  = -1;  // StringName 'inherits' field within ClassInfo
    inline int CLASSINFO_PROPLIST_OFFSET  = -1;  // List<PropertyInfo> property_list within ClassInfo
    inline int CLASSINFO_METHODMAP_OFFSET = -1;  // HashMap<StringName, MethodBind*> method_map within ClassInfo

    // ----- PropertyInfo field offsets within List::Element (probed) -----
    inline int PROPINFO_TYPE_OFFSET   = 0;   // Variant::Type (always first field, offset 0)
    inline int PROPINFO_NAME_OFFSET   = -1;  // String name offset within PropertyInfo
    inline int PROPINFO_HINT_OFFSET   = -1;  // PropertyHint offset within PropertyInfo
    inline int PROPINFO_USAGE_OFFSET  = -1;  // usage flags offset within PropertyInfo
    inline int PROPINFO_TOTAL_SIZE    = -1;  // total PropertyInfo size in bytes
    inline int ELEMENT_NEXT_OFFSET    = -1;  // Element.next_ptr offset (after PropertyInfo value)

    // ----- MethodBind ptrcall vtable index -----
    // MSVC ABI: ptrcall at vtable[2], Itanium ABI: vtable[3]
    // Set by Diagoldze after ABI detection
    inline int METHODBIND_PTRCALL_VTIDX = 2;

    // ----- Validation state -----
    inline std::atomic<bool> bOffsetsValidated{false};
    inline std::atomic<bool> bNodeOffsetsValidated{false};
    inline std::atomic<bool> bStringNameProbed{false};
    inline std::atomic<bool> bClassInfoProbed{false};
}

// ============================================================
// GodotVer — Detected Godot version state (set by Frieren after scan)
// Used by Flamme, Ubel, Diagoldze for version-specific behavior.
// ============================================================
namespace GodotVer {
    inline uint32_t Major = 0;
    inline uint32_t Minor = 0;
    inline uint32_t Patch = 0;

    // Version predicates
    inline bool IsGodot3()    { return Major == 3; }
    inline bool IsGodot4()    { return Major == 4; }
    inline bool IsGodot45Plus() { return Major > 4 || (Major == 4 && Minor >= 5); }

    // Variant::Type max valid value (exclusive sentinel)
    // 3.x: 27 types (0-26), 4.0-4.4: 38 types (0-37), 4.5+: 39 types (0-38)
    inline int VariantTypeMax() {
        if (Major == 3) return 27;
        if (Major == 4 && Minor >= 5) return 39;
        if (Major >= 4) return 38;
        return 39; // Unknown version, use largest
    }
}

// ============================================================
// Heiter — 海塔 (Priest — GameThread Dispatch) constants
// ============================================================
namespace Heiter {
    constexpr unsigned long      SYNC_TIMEOUT_MS   = 500;     // Default sync dispatch timeout (fast fail for UX)
    constexpr unsigned long long DRAIN_BUDGET_MS   = 2;       // Max ms to drain work queue per frame
    constexpr int                RETRY_ATTEMPTS    = 60;      // Window discovery retries (60 × 500ms = 30s)
    constexpr unsigned long      RETRY_INTERVAL_MS = 500;     // ms between window discovery retries
    constexpr size_t             MAX_QUEUE_SIZE    = 256;     // Max queued work items (overflow protection)
    constexpr int                MAX_CONSEC_TIMEOUTS = 3;     // Consecutive timeouts before disabling ptrcall
}

} // namespace Zolt
