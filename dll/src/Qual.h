#pragma once

// Qual — 全 (Wholeness — Mailbox)
// Shared memory mailbox for CE Lua script communication.
// CE Lua uses readQword/writeQword (ReadProcessMemory/WriteProcessMemory)
// to communicate with the DLL without needing CreateRemoteThread.
//
// CE Lua workflow:
//   1. mb = getAddress("g_zoltMailbox")
//   2. Wait for readInteger(mb + 0x04) ~= 0xFF (STATUS_INIT = DLL still loading)
//   3. Write input fields (className, memberName, etc.)
//   4. Write cmd field (trigger) — MUST be written LAST
//   5. Poll status field until == 1 (STATUS_DONE)
//   6. Read result fields (objectAddr, memberAddr, etc.)

#include <cstdint>

namespace Zolt::Qual {

// Mailbox commands (CE writes to cmd field)
enum Cmd : int32_t {
    CMD_IDLE                     = 0,
    CMD_FIND_INSTANCE            = 1,  // Find Object by class name (returns objectAddr)
    CMD_GET_MEMBER_ADDR          = 2,  // Get GDScript member data address (className + memberName → memberAddr)
    CMD_FIND_NODE                = 3,  // Find node by name in scene tree (returns objectAddr)
    CMD_FIND_INSTANCE_WITH_MEMBER = 4, // Find Object by class + member name; nodeName = optional 2nd member filter for disambiguation
    CMD_FIND_INSTANCE_BY_PATH    = 5, // Find Object by class + nested property path string match (e.g. item.my_id == "item_tree")
    CMD_FIND_MANAGED_INSTANCES   = 6, // Bulk: find ALL .NET managed objects of a class with optional field filter
                                      //   Input:  className, targetOp+targetValue (managed field filter),
                                      //           filterOp = managed field offset (repurposed), filterValue = read size (repurposed)
                                      //   Output: result = count, results in g_zoltManagedResults buffer
    CMD_RESOLVE_FIELD_OFFSET     = 7, // Resolve .NET field name → offset via Denken reflection
                                      //   Input:  className = .NET type name (e.g. "MegaCrit.Sts2.Core.Entities.Creatures.Creature")
                                      //           memberName = field name (e.g. "_currentHp")
                                      //   Output: result = field offset (or -1 if not found), memberIndex = field size
    CMD_PING                     = 99, // Connectivity check (returns STATUS_DONE immediately)
};

// Mailbox status (DLL writes to status field)
enum Status : int32_t {
    STATUS_IDLE         = 0,    // Ready for commands
    STATUS_DONE         = 1,    // Command completed (result valid)
    STATUS_PROCESSING   = 2,    // Command in progress
    STATUS_INIT         = 0xFF, // DLL still initializing (not ready for commands)
};

// Value comparison operators for CMD_FIND_INSTANCE_WITH_MEMBER criteria
enum FilterOp : int32_t {
    FILTER_NONE = 0,  // No filter (skip this criterion)
    FILTER_GT   = 1,  // >
    FILTER_LT   = 2,  // <
    FILTER_NE   = 3,  // !=
    FILTER_EQ   = 4,  // ==
    FILTER_GE   = 5,  // >=
    FILTER_LE   = 6,  // <=
};

// Mailbox structure (exported as global variable)
// CE Lua accesses via getAddress("g_zoltMailbox") + offset reads/writes
//
// Total size: ~1384 bytes (fits in single page)
#pragma pack(push, 1)
struct MailboxData {
    volatile int32_t  cmd;              // 0x000: Cmd enum (CE writes LAST as trigger)
    volatile int32_t  status;           // 0x004: Status enum (DLL writes)
    volatile int32_t  result;           // 0x008: Return code (0=ok, negative=error)
    volatile int32_t  memberIndex;      // 0x00C: Output: member index within Vector<Variant>

    volatile uint64_t objectAddr;       // 0x010: Output: Object* address
    volatile uint64_t memberAddr;       // 0x018: Output: member data address (Variant + VARIANT_DATA)
    volatile int32_t  memberType;       // 0x020: Output: Variant::Type of member
    int32_t           reserved1;        // 0x024: Alignment padding

    char              className[256];   // 0x028: Input: class name (null-terminated)
    char              memberName[256];  // 0x128: Input: member name (null-terminated)
    char              nodeName[256];    // 0x228: Input: node name for CMD_FIND_NODE / filter member name
    char              errorMsg[256];    // 0x328: Output: error description

    // ── Value criteria for CMD_FIND_INSTANCE_WITH_MEMBER ──
    // Target member (memberName) value filter
    volatile int32_t  targetOp;         // 0x428: FilterOp for target member value
    int32_t           reserved2;        // 0x42C: Alignment padding
    volatile int64_t  targetValue;      // 0x430: Comparison value for target member

    // Secondary filter member (nodeName) value filter
    volatile int32_t  filterOp;         // 0x438: FilterOp for filter member value
    int32_t           reserved3;        // 0x43C: Alignment padding
    volatile int64_t  filterValue;      // 0x440: Comparison value for filter member

    // ── String match for CMD_FIND_INSTANCE_BY_PATH ──
    char              matchStr[256];    // 0x448: Input: expected string value to match at end of path
};
#pragma pack(pop)

static_assert(sizeof(MailboxData) <= 4096, "MailboxData must fit in a single page");

/// Start the mailbox polling thread.
/// Called from dllmain.cpp DLL_PROCESS_ATTACH.
void StartPolling();

/// Stop the mailbox polling thread and clean up.
/// Called from Zolt_Shutdown().
void StopPolling();

/// Notify mailbox that DLL init is complete (transitions from STATUS_INIT to STATUS_IDLE).
void NotifyReady();

} // namespace Zolt::Qual

// ============================================================
// Bulk results buffer for CMD_FIND_MANAGED_INSTANCES
// Separate from mailbox to keep MailboxData small.
// CE Lua reads via: getAddress("g_zoltManagedResults") + offset
//
// Layout:
//   +0x000: int32  count       (number of valid entries)
//   +0x004: int32  capacity    (max entries = 256)
//   +0x008: entry[0].objectAddr   (uint64)
//   +0x010: entry[0].managedAddr  (uint64)
//   +0x018: entry[1].objectAddr   (uint64)
//   ...
// Each entry is 16 bytes. Total: 8 + 256*16 = 4104 bytes.
// ============================================================

struct ManagedSearchEntry {
    uint64_t objectAddr;     // Godot Object*
    uint64_t managedAddr;    // .NET managed object pointer
};

constexpr int MANAGED_RESULTS_MAX = 256;

struct ManagedSearchResults {
    volatile int32_t count;                         // 0x000: Number of valid entries
    int32_t          capacity;                      // 0x004: Max capacity (256)
    ManagedSearchEntry entries[MANAGED_RESULTS_MAX]; // 0x008: Results array
};

// Exported global — CE Lua uses getAddress("g_zoltMailbox") to find it.
// No function call needed! CE resolves the symbol from the DLL export table.
extern "C" __declspec(dllexport) extern Zolt::Qual::MailboxData g_zoltMailbox;

// Exported bulk results buffer — CE Lua uses getAddress("g_zoltManagedResults")
extern "C" __declspec(dllexport) extern ManagedSearchResults g_zoltManagedResults;
