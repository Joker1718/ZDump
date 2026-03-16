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
    CMD_IDLE            = 0,
    CMD_FIND_INSTANCE   = 1,  // Find Object by class name (returns objectAddr)
    CMD_GET_MEMBER_ADDR = 2,  // Get GDScript member data address (className + memberName → memberAddr)
    CMD_FIND_NODE       = 3,  // Find node by name in scene tree (returns objectAddr)
    CMD_PING            = 99, // Connectivity check (returns STATUS_DONE immediately)
};

// Mailbox status (DLL writes to status field)
enum Status : int32_t {
    STATUS_IDLE         = 0,    // Ready for commands
    STATUS_DONE         = 1,    // Command completed (result valid)
    STATUS_PROCESSING   = 2,    // Command in progress
    STATUS_INIT         = 0xFF, // DLL still initializing (not ready for commands)
};

// Mailbox structure (exported as global variable)
// CE Lua accesses via getAddress("g_zoltMailbox") + offset reads/writes
//
// Total size: ~1312 bytes (fits in single page)
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
    char              nodeName[256];    // 0x228: Input: node name for CMD_FIND_NODE
    char              errorMsg[256];    // 0x328: Output: error description
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

// Exported global — CE Lua uses getAddress("g_zoltMailbox") to find it.
// No function call needed! CE resolves the symbol from the DLL export table.
extern "C" __declspec(dllexport) extern Zolt::Qual::MailboxData g_zoltMailbox;
