// Qual — 全 (Wholeness — Mailbox)
// Shared memory mailbox for CE Lua scripts.
//
// Polling thread checks the mailbox every ~10ms.
// Uses existing subsystem APIs (Ausetiese, Ubel, Diagoldze, etc.)
//
// Thread safety model:
//   CE Lua writes all fields, then writes cmd (trigger) LAST.
//   Polling thread reads cmd, processes, writes status=DONE then cmd=IDLE.
//   WriteProcessMemory (CE side) is kernel-serializing.

#include "Qual.h"
#include "Ausetiese.h"
#include "Diagoldze.h"
#include "Ubel.h"
#include "Flamme.h"
#include "Serie.h"
#include "Zoltraak.h"
#include "Solitaer.h"
#include "Grimoire.h"

#include <Windows.h>
#include <atomic>
#include <cstring>
#include <string>
#include <algorithm>
#include <cctype>

// The exported mailbox — zero-initialized by default, starts with STATUS_INIT
extern "C" __declspec(dllexport) Zolt::Qual::MailboxData g_zoltMailbox = {};

namespace Zolt::Qual {

// Polling thread state
static std::atomic<bool> s_running{false};
static HANDLE s_hThread = nullptr;

// Forward declarations
static void HandleFindInstance();
static void HandleGetMemberAddr();
static void HandleFindNode();
static void HandleFindInstanceWithMember();
static void SetError(int32_t code, const char* msg);
static void SetDone(int32_t resultCode);

// ---- Polling thread ----

static DWORD WINAPI PollingThreadProc(LPVOID /*param*/) {
    Solitaer::LogPipe("Qual: mailbox polling thread started (STATUS_INIT until DLL ready)");

    // Mark as initializing — CE Lua should wait for this to clear
    g_zoltMailbox.status = STATUS_INIT;

    while (s_running.load(std::memory_order_acquire)) {
        int32_t cmd = g_zoltMailbox.cmd;

        if (cmd != CMD_IDLE) {
            // Reject commands while still initializing
            if (g_zoltMailbox.status == STATUS_INIT) {
                SetError(-10, "DLL still initializing");
                Sleep(10);
                continue;
            }

            // Mark as processing
            g_zoltMailbox.status = STATUS_PROCESSING;

            Solitaer::Info("PIPE", "Qual: received cmd=%d", cmd);

            switch (cmd) {
            case CMD_FIND_INSTANCE:
                HandleFindInstance();
                break;
            case CMD_GET_MEMBER_ADDR:
                HandleGetMemberAddr();
                break;
            case CMD_FIND_NODE:
                HandleFindNode();
                break;
            case CMD_FIND_INSTANCE_WITH_MEMBER:
                HandleFindInstanceWithMember();
                break;
            case CMD_PING:
                SetDone(0);
                break;
            default:
                SetError(-1, "Unknown command");
                break;
            }
        }

        Sleep(10);
    }

    Solitaer::LogPipe("Qual: mailbox polling thread stopped");
    return 0;
}

// ---- Public API ----

void StartPolling() {
    if (s_running.load()) return;

    Solitaer::LogPipe("Qual: starting mailbox polling");

    // Initialize mailbox to INIT state
    memset(&g_zoltMailbox, 0, sizeof(g_zoltMailbox));
    g_zoltMailbox.status = STATUS_INIT;

    s_running.store(true, std::memory_order_release);
    s_hThread = CreateThread(nullptr, 0, PollingThreadProc, nullptr, 0, nullptr);
    if (!s_hThread) {
        s_running.store(false);
        Solitaer::Warn("PIPE", "Qual: failed to create polling thread (err=%lu)", GetLastError());
    }
}

void StopPolling() {
    if (!s_running.load()) return;

    Solitaer::LogPipe("Qual: stopping mailbox polling");
    s_running.store(false, std::memory_order_release);

    if (s_hThread) {
        WaitForSingleObject(s_hThread, 3000);
        CloseHandle(s_hThread);
        s_hThread = nullptr;
    }

    // Clear mailbox
    memset(&g_zoltMailbox, 0, sizeof(g_zoltMailbox));
}

void NotifyReady() {
    if (g_zoltMailbox.status == STATUS_INIT) {
        g_zoltMailbox.status = STATUS_IDLE;
        Solitaer::Info("PIPE", "Qual: mailbox ready (STATUS_IDLE)");
    }
}

// ---- Command handlers ----

// CMD_FIND_INSTANCE — Find an Object by class name in ObjectDB
// Input: className (partial, case-insensitive match supported)
// Output: objectAddr
static void HandleFindInstance() {
    char className[256];
    memcpy(className, g_zoltMailbox.className, sizeof(className));
    className[255] = '\0';

    if (className[0] == '\0') {
        SetError(-1, "Empty class name");
        return;
    }

    Solitaer::Info("PIPE", "Qual: FIND_INSTANCE class='%s'", className);

    // Convert to lowercase for case-insensitive comparison
    std::string target(className);
    std::string targetLower = target;
    std::transform(targetLower.begin(), targetLower.end(), targetLower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    uint64_t foundAddr = 0;

    Ausetiese::ForEachEx([&](int /*index*/, uint64_t objAddr) {
        std::string cls = Diagoldze::GetObjectClass(objAddr);
        if (cls.empty()) return true;

        // Exact match first
        if (cls == target) {
            foundAddr = objAddr;
            return false; // stop
        }

        // Case-insensitive match
        std::string clsLower = cls;
        std::transform(clsLower.begin(), clsLower.end(), clsLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (clsLower == targetLower) {
            foundAddr = objAddr;
            return false; // stop
        }

        return true; // continue
    });

    if (foundAddr) {
        g_zoltMailbox.objectAddr = foundAddr;
        Solitaer::Info("PIPE", "Qual: FIND_INSTANCE '%s' -> %p",
                       className, reinterpret_cast<void*>(foundAddr));
        SetDone(0);
    } else {
        g_zoltMailbox.objectAddr = 0;
        char msg[256];
        snprintf(msg, sizeof(msg), "No instance of '%s' found", className);
        SetError(-2, msg);
    }
}

// CMD_GET_MEMBER_ADDR — Get a GDScript member's data address
// Input: className + memberName
// Output: objectAddr, memberAddr, memberIndex, memberType
//
// Flow:
//   1. Find Object by className (if objectAddr is 0)
//   2. Read ScriptInstance → members Vector
//   3. Resolve member names via ReadGDScriptMemberNames
//   4. Find member by name → return Variant data address
static void HandleGetMemberAddr() {
    char className[256], memberName[256];
    memcpy(className, g_zoltMailbox.className, sizeof(className));
    memcpy(memberName, g_zoltMailbox.memberName, sizeof(memberName));
    className[255] = '\0';
    memberName[255] = '\0';

    if (memberName[0] == '\0') {
        SetError(-1, "Empty member name");
        return;
    }

    Solitaer::Info("PIPE", "Qual: GET_MEMBER_ADDR class='%s' member='%s'",
                   className, memberName);

    // Step 1: Find object (reuse objectAddr if already set, otherwise search by class)
    uint64_t objAddr = g_zoltMailbox.objectAddr;
    if (!objAddr && className[0] != '\0') {
        // Search by class name
        std::string target(className);
        std::string targetLower = target;
        std::transform(targetLower.begin(), targetLower.end(), targetLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        Ausetiese::ForEachEx([&](int /*index*/, uint64_t addr) {
            std::string cls = Diagoldze::GetObjectClass(addr);
            if (cls.empty()) return true;
            std::string clsLower = cls;
            std::transform(clsLower.begin(), clsLower.end(), clsLower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (cls == target || clsLower == targetLower) {
                // Verify this object has a ScriptInstance
                if (DynOff::OBJECT_SCRIPT_INSTANCE >= 0) {
                    uint64_t si = Zoltraak::ReadSafe<uint64_t>(
                        addr + DynOff::OBJECT_SCRIPT_INSTANCE, 0);
                    if (si && Zoltraak::IsReadable(si)) {
                        objAddr = addr;
                        return false;
                    }
                }
            }
            return true;
        });
    }

    if (!objAddr) {
        SetError(-2, "No instance found with ScriptInstance");
        return;
    }

    // Step 2: Read ScriptInstance
    if (DynOff::OBJECT_SCRIPT_INSTANCE < 0) {
        SetError(-3, "ScriptInstance offset not probed");
        return;
    }

    uint64_t scriptInst = Zoltraak::ReadSafe<uint64_t>(
        objAddr + DynOff::OBJECT_SCRIPT_INSTANCE, 0);
    if (!scriptInst || !Zoltraak::IsReadable(scriptInst)) {
        SetError(-3, "Object has no readable ScriptInstance");
        return;
    }

    // Verify owner backlink
    uint64_t owner = Zoltraak::ReadSafe<uint64_t>(scriptInst + DynOff::GDSCRIPT_OWNER_OFF, 0);
    if (owner != objAddr) {
        SetError(-3, "ScriptInstance owner mismatch");
        return;
    }

    // Step 3: Read members Vector
    uint64_t membersPtr = Zoltraak::ReadSafe<uint64_t>(
        scriptInst + DynOff::GDSCRIPT_MEMBERS_OFF, 0);
    if (!membersPtr || !Zoltraak::IsReadable(membersPtr)) {
        SetError(-4, "Members Vector not readable");
        return;
    }

    int sizeOff = GodotVer::IsGodot3() ? 4 : 8;
    uint32_t count = Zoltraak::ReadSafe<uint32_t>(membersPtr - sizeOff, 0);
    if (count == 0 || count > 500) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Invalid member count: %u", count);
        SetError(-4, msg);
        return;
    }

    // Step 4: Get GDScript object for name resolution
    uint64_t gdscriptPtr = Zoltraak::ReadSafe<uint64_t>(
        scriptInst + DynOff::GDSCRIPT_SCRIPT_OFF, 0);

    // Resolve member names
    auto nameMap = Ubel::GetMemberNames(gdscriptPtr);

    // Step 5: Find member by name
    std::string targetName(memberName);
    std::string targetNameLower = targetName;
    std::transform(targetNameLower.begin(), targetNameLower.end(), targetNameLower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    int foundIndex = -1;

    // Search resolved names first
    for (auto& [idx, name] : nameMap) {
        if (name == targetName) {
            foundIndex = idx;
            break;
        }
        std::string nameLower = name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (nameLower == targetNameLower) {
            foundIndex = idx;
            break;
        }
    }

    // Fallback: try script_var_N format
    if (foundIndex < 0 && targetName.rfind("script_var_", 0) == 0) {
        try {
            foundIndex = std::stoi(targetName.substr(11));
        } catch (...) {}
    }

    if (foundIndex < 0 || foundIndex >= static_cast<int>(count)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Member '%s' not found (%d names resolved, %u total)",
                 memberName, static_cast<int>(nameMap.size()), count);
        SetError(-5, msg);
        return;
    }

    // Step 6: Calculate address and read type
    int stride = DynOff::VARIANT_STRIDE;
    uint64_t varAddr = membersPtr + foundIndex * stride;
    uint64_t dataAddr = varAddr + DynOff::VARIANT_DATA;
    int32_t varType = Zoltraak::ReadSafe<int32_t>(varAddr + DynOff::VARIANT_TYPE, -1);

    g_zoltMailbox.objectAddr   = objAddr;
    g_zoltMailbox.memberAddr   = dataAddr;
    g_zoltMailbox.memberIndex  = foundIndex;
    g_zoltMailbox.memberType   = varType;

    Solitaer::Info("PIPE", "Qual: GET_MEMBER_ADDR '%s.%s' → member[%d] type=%d addr=%p",
                   className, memberName, foundIndex, varType,
                   reinterpret_cast<void*>(dataAddr));
    SetDone(0);
}

// CMD_FIND_NODE — Find a node by name in the scene tree
// Input: nodeName (searches by Node name, not path)
// Output: objectAddr
static void HandleFindNode() {
    char nodeName[256];
    memcpy(nodeName, g_zoltMailbox.nodeName, sizeof(nodeName));
    nodeName[255] = '\0';

    if (nodeName[0] == '\0') {
        SetError(-1, "Empty node name");
        return;
    }

    Solitaer::Info("PIPE", "Qual: FIND_NODE name='%s'", nodeName);

    // Search ObjectDB for nodes with matching name
    std::string target(nodeName);
    uint64_t foundAddr = 0;

    Ausetiese::ForEachEx([&](int /*index*/, uint64_t objAddr) {
        std::string cls = Diagoldze::GetObjectClass(objAddr);
        if (cls.empty()) return true;

        // Only check Node-derived classes
        if (!Diagoldze::IsNodeDerived(cls)) return true;

        // Read node name via Flamme
        std::string name = Serie::GetNodeName(objAddr);
        if (name == target) {
            foundAddr = objAddr;
            return false;
        }
        return true;
    });

    if (foundAddr) {
        g_zoltMailbox.objectAddr = foundAddr;
        Solitaer::Info("PIPE", "Qual: FIND_NODE '%s' -> %p",
                       nodeName, reinterpret_cast<void*>(foundAddr));
        SetDone(0);
    } else {
        g_zoltMailbox.objectAddr = 0;
        char msg[256];
        snprintf(msg, sizeof(msg), "Node '%s' not found", nodeName);
        SetError(-2, msg);
    }
}

// Apply a FilterOp comparison: returns true if (actual op threshold) passes.
static bool ApplyFilterOp(int32_t op, int64_t actual, int64_t threshold) {
    switch (op) {
    case Zolt::Qual::FILTER_GT: return actual > threshold;
    case Zolt::Qual::FILTER_LT: return actual < threshold;
    case Zolt::Qual::FILTER_NE: return actual != threshold;
    case Zolt::Qual::FILTER_EQ: return actual == threshold;
    case Zolt::Qual::FILTER_GE: return actual >= threshold;
    case Zolt::Qual::FILTER_LE: return actual <= threshold;
    default: return true; // FILTER_NONE or unknown → pass
    }
}

// Read the int64 value of a Variant at the given member index.
// For int/bool types reads the data portion; for real reads as double and casts.
static int64_t ReadMemberValue(uint64_t si, int memberIdx) {
    uint64_t membersPtr = Zoltraak::ReadSafe<uint64_t>(
        si + DynOff::GDSCRIPT_MEMBERS_OFF, 0);
    if (!membersPtr) return 0;

    int stride = DynOff::VARIANT_STRIDE;
    uint64_t varAddr = membersPtr + memberIdx * stride;
    int32_t varType = Zoltraak::ReadSafe<int32_t>(varAddr + DynOff::VARIANT_TYPE, -1);

    uint64_t dataAddr = varAddr + DynOff::VARIANT_DATA;

    // Godot Variant types: BOOL=1, INT=2, REAL/FLOAT=3
    switch (varType) {
    case 1: // BOOL
        return Zoltraak::ReadSafe<int32_t>(dataAddr, 0) ? 1 : 0;
    case 2: // INT
        return Zoltraak::ReadSafe<int64_t>(dataAddr, 0);
    case 3: { // REAL/FLOAT — Godot 3.x uses double, 4.x uses double
        double d = Zoltraak::ReadSafe<double>(dataAddr, 0.0);
        return static_cast<int64_t>(d);
    }
    default:
        return Zoltraak::ReadSafe<int64_t>(dataAddr, 0);
    }
}

// CMD_FIND_INSTANCE_WITH_MEMBER — Find an Object by class name that has a specific GDScript member
// Input: className (e.g. "Reference") + memberName (e.g. "gold")
//        nodeName (optional): secondary member name filter — object must ALSO have this member
//        targetOp + targetValue (optional): value criterion for target member (e.g. gold > 30)
//        filterOp + filterValue (optional): value criterion for filter member (e.g. current_xp != 0)
// Output: objectAddr + memberAddr + memberIndex + memberType
//
// Solves the "509 Reference objects" problem: iterates ObjectDB instances of the
// given class and for each checks if the GDScript instance has the named member,
// with optional value-based criteria for disambiguation.
// Returns the FIRST match with its resolved member address.
static void HandleFindInstanceWithMember() {
    char className[256], memberName[256], filterName[256];
    memcpy(className, g_zoltMailbox.className, sizeof(className));
    memcpy(memberName, g_zoltMailbox.memberName, sizeof(memberName));
    memcpy(filterName, g_zoltMailbox.nodeName, sizeof(filterName));
    className[255] = '\0';
    memberName[255] = '\0';
    filterName[255] = '\0';

    // Read value criteria
    int32_t targetOp    = g_zoltMailbox.targetOp;
    int64_t targetValue = g_zoltMailbox.targetValue;
    int32_t filterOp    = g_zoltMailbox.filterOp;
    int64_t filterValue = g_zoltMailbox.filterValue;

    if (className[0] == '\0') {
        SetError(-1, "Empty class name");
        return;
    }
    if (memberName[0] == '\0') {
        SetError(-1, "Empty member name");
        return;
    }

    bool hasFilter = (filterName[0] != '\0');
    bool hasTargetCriteria = (targetOp != Zolt::Qual::FILTER_NONE);
    bool hasFilterCriteria = (filterOp != Zolt::Qual::FILTER_NONE && hasFilter);

    Solitaer::Info("PIPE", "Qual: FIND_INSTANCE_WITH_MEMBER class='%s' member='%s' filter='%s' "
                   "targetOp=%d targetVal=%lld filterOp=%d filterVal=%lld",
                   className, memberName, hasFilter ? filterName : "(none)",
                   targetOp, static_cast<long long>(targetValue),
                   filterOp, static_cast<long long>(filterValue));

    std::string targetClass(className);
    std::string targetClassLower = targetClass;
    std::transform(targetClassLower.begin(), targetClassLower.end(), targetClassLower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::string targetMember(memberName);
    std::string targetMemberLower = targetMember;
    std::transform(targetMemberLower.begin(), targetMemberLower.end(), targetMemberLower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::string filterMember, filterMemberLower;
    if (hasFilter) {
        filterMember = std::string(filterName);
        filterMemberLower = filterMember;
        std::transform(filterMemberLower.begin(), filterMemberLower.end(), filterMemberLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }

    uint64_t foundObj = 0;
    int foundIndex = -1;
    uint64_t foundGdScript = 0;
    int scannedCount = 0;
    int memberMatchCount = 0;
    int criteriaFailCount = 0;

    Ausetiese::ForEachEx([&](int /*index*/, uint64_t objAddr) {
        std::string cls = Diagoldze::GetObjectClass(objAddr);
        if (cls.empty()) return true;

        // Class name match (case-insensitive)
        std::string clsLower = cls;
        std::transform(clsLower.begin(), clsLower.end(), clsLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (cls != targetClass && clsLower != targetClassLower) return true;

        scannedCount++;

        // Check for ScriptInstance
        if (DynOff::OBJECT_SCRIPT_INSTANCE < 0) return true;
        uint64_t si = Zoltraak::ReadSafe<uint64_t>(
            objAddr + DynOff::OBJECT_SCRIPT_INSTANCE, 0);
        if (!si || !Zoltraak::IsReadable(si)) return true;

        // Verify owner backlink
        uint64_t owner = Zoltraak::ReadSafe<uint64_t>(si + DynOff::GDSCRIPT_OWNER_OFF, 0);
        if (owner != objAddr) return true;

        // Get GDScript pointer for name resolution
        uint64_t gdscriptPtr = Zoltraak::ReadSafe<uint64_t>(
            si + DynOff::GDSCRIPT_SCRIPT_OFF, 0);

        // Resolve member names for this instance
        auto nameMap = Ubel::GetMemberNames(gdscriptPtr);

        // Search for the target member and filter member indices
        int targetIdx = -1;
        int filterIdx = -1;

        for (auto& [idx, name] : nameMap) {
            std::string nameLower = name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (name == targetMember || nameLower == targetMemberLower) {
                targetIdx = idx;
            }
            if (hasFilter && (name == filterMember || nameLower == filterMemberLower)) {
                filterIdx = idx;
            }
        }

        if (targetIdx < 0) return true; // doesn't have target member

        memberMatchCount++;

        // If filter member name is set, object must also have it
        if (hasFilter && filterIdx < 0) return true;

        // ── Value criteria checks ──
        // Check target member value criterion (e.g. gold > 30)
        if (hasTargetCriteria) {
            int64_t val = ReadMemberValue(si, targetIdx);
            if (!ApplyFilterOp(targetOp, val, targetValue)) {
                criteriaFailCount++;
                return true; // value criterion failed — skip
            }
        }

        // Check filter member value criterion (e.g. current_xp != 0)
        if (hasFilterCriteria && filterIdx >= 0) {
            int64_t val = ReadMemberValue(si, filterIdx);
            if (!ApplyFilterOp(filterOp, val, filterValue)) {
                criteriaFailCount++;
                return true; // value criterion failed — skip
            }
        }

        foundObj = objAddr;
        foundIndex = targetIdx;
        foundGdScript = gdscriptPtr;
        return false; // stop — found the match
    });

    if (!foundObj || foundIndex < 0) {
        char msg[256];
        if (criteriaFailCount > 0) {
            snprintf(msg, sizeof(msg),
                     "Found %d '%s' with '%s' but %d failed value criteria (scanned %d)",
                     memberMatchCount, className, memberName, criteriaFailCount, scannedCount);
        } else if (hasFilter && memberMatchCount > 0) {
            snprintf(msg, sizeof(msg),
                     "Found %d '%s' with '%s' but none also had '%s' (scanned %d)",
                     memberMatchCount, className, memberName, filterName, scannedCount);
        } else {
            snprintf(msg, sizeof(msg), "No '%s' instance with member '%s' (scanned %d)",
                     className, memberName, scannedCount);
        }
        SetError(-2, msg);
        return;
    }

    // Calculate member address (same logic as HandleGetMemberAddr)
    uint64_t si = Zoltraak::ReadSafe<uint64_t>(
        foundObj + DynOff::OBJECT_SCRIPT_INSTANCE, 0);
    uint64_t membersPtr = Zoltraak::ReadSafe<uint64_t>(
        si + DynOff::GDSCRIPT_MEMBERS_OFF, 0);

    int stride = DynOff::VARIANT_STRIDE;
    uint64_t varAddr = membersPtr + foundIndex * stride;
    uint64_t dataAddr = varAddr + DynOff::VARIANT_DATA;
    int32_t varType = Zoltraak::ReadSafe<int32_t>(varAddr + DynOff::VARIANT_TYPE, -1);

    g_zoltMailbox.objectAddr  = foundObj;
    g_zoltMailbox.memberAddr  = dataAddr;
    g_zoltMailbox.memberIndex = foundIndex;
    g_zoltMailbox.memberType  = varType;

    Solitaer::Info("PIPE", "Qual: FIND_INSTANCE_WITH_MEMBER '%s.%s' filter='%s' "
                   "→ obj=%p member[%d] type=%d addr=%p (matched %d, criteria_fail %d, scanned %d)",
                   className, memberName, hasFilter ? filterName : "(none)",
                   reinterpret_cast<void*>(foundObj), foundIndex, varType,
                   reinterpret_cast<void*>(dataAddr), memberMatchCount, criteriaFailCount, scannedCount);
    SetDone(0);
}

// ---- Helpers ----

static void SetError(int32_t code, const char* msg) {
    g_zoltMailbox.result = code;
    if (msg) {
        strncpy(const_cast<char*>(g_zoltMailbox.errorMsg), msg, sizeof(g_zoltMailbox.errorMsg) - 1);
        const_cast<char*>(g_zoltMailbox.errorMsg)[sizeof(g_zoltMailbox.errorMsg) - 1] = '\0';
    }
    Solitaer::Warn("PIPE", "Qual: error=%d msg='%s'", code, msg ? msg : "");

    // Signal completion — MUST write status BEFORE clearing cmd
    g_zoltMailbox.status = STATUS_DONE;
    g_zoltMailbox.cmd = CMD_IDLE;
}

static void SetDone(int32_t resultCode) {
    g_zoltMailbox.result = resultCode;

    // Signal completion — MUST write status BEFORE clearing cmd
    g_zoltMailbox.status = STATUS_DONE;
    g_zoltMailbox.cmd = CMD_IDLE;
}

} // namespace Zolt::Qual
