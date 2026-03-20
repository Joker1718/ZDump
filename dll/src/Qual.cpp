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
#include <vector>
#include <algorithm>
#include <cctype>

// The exported mailbox — zero-initialized by default, starts with STATUS_INIT
extern "C" __declspec(dllexport) Zolt::Qual::MailboxData g_zoltMailbox = {};

// Bulk results buffer for CMD_FIND_MANAGED_INSTANCES
// CE Lua reads via getAddress("g_zoltManagedResults") + offset
extern "C" __declspec(dllexport) ManagedSearchResults g_zoltManagedResults = { 0, MANAGED_RESULTS_MAX, {} };

namespace Zolt::Qual {

// Polling thread state
static std::atomic<bool> s_running{false};
static HANDLE s_hThread = nullptr;

// Forward declarations
static void HandleFindInstance();
static void HandleGetMemberAddr();
static void HandleFindNode();
static void HandleFindInstanceWithMember();
static void HandleFindInstanceByPath();
static void HandleFindManagedInstances();
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
            case CMD_FIND_INSTANCE_BY_PATH:
                HandleFindInstanceByPath();
                break;
            case CMD_FIND_MANAGED_INSTANCES:
                HandleFindManagedInstances();
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

    // Scan members until invalid Variant type to determine real count.
    // Don't trust CowData count — custom builds may store refcount instead of size.
    int stride = DynOff::VARIANT_STRIDE;
    int maxType2 = GodotVer::VariantTypeMax();
    uint32_t count = 0;
    for (uint32_t m = 0; m < 500; m++) {
        int32_t mType = Zoltraak::ReadSafe<int32_t>(
            membersPtr + static_cast<uint64_t>(m) * stride, -1);
        if (mType < 0 || mType >= maxType2) break;
        count = m + 1;
    }
    if (count == 0) {
        SetError(-4, "No valid members found (scan-until-invalid)");
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
    uint64_t varAddr = membersPtr + foundIndex * stride;
    uint64_t dataAddr = varAddr + DynOff::VARIANT_DATA;
    int32_t varType = Zoltraak::ReadSafe<int32_t>(varAddr + DynOff::VARIANT_TYPE, -1);

    // ── Dict/Array key traversal via matchStr ──
    // When matchStr is set, the member should be a Dictionary or Array.
    // Walk into it to find the element matching the key/index.
    char dictKey[256];
    memcpy(dictKey, g_zoltMailbox.matchStr, sizeof(dictKey));
    dictKey[255] = '\0';

    if (dictKey[0] != '\0') {
        // Map type to 4.x for unified check
        int type4x = varType;
        if (GodotVer::IsGodot3()) {
            // 3.x → 4.x mapping: Dictionary=18→27, Array=19→28
            if (type4x == 18) type4x = 27;
            else if (type4x == 19) type4x = 28;
        }

        if (type4x == 27) { // Dictionary
            uint64_t elemAddr = Ubel::FindDictValueByKey(dataAddr, std::string(dictKey));
            if (!elemAddr) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Dict key '%s' not found in member '%s'",
                         dictKey, memberName);
                SetError(-6, msg);
                return;
            }
            uint64_t elemVarAddr = elemAddr - DynOff::VARIANT_DATA;
            int32_t elemType = Zoltraak::ReadSafe<int32_t>(elemVarAddr + DynOff::VARIANT_TYPE, -1);

            g_zoltMailbox.objectAddr   = objAddr;
            g_zoltMailbox.memberAddr   = elemAddr;
            g_zoltMailbox.memberIndex  = foundIndex;
            g_zoltMailbox.memberType   = elemType;

            Solitaer::Info("PIPE", "Qual: GET_MEMBER_ADDR '%s.%s' dictKey='%s' → type=%d addr=%p",
                           className, memberName, dictKey, elemType,
                           reinterpret_cast<void*>(elemAddr));
            SetDone(0);
            return;
        }
        else if (type4x == 28) { // Array
            int idx = -1;
            try { idx = std::stoi(dictKey); } catch (...) {}
            if (idx < 0) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Invalid array index '%s' for member '%s'",
                         dictKey, memberName);
                SetError(-6, msg);
                return;
            }
            uint64_t elemAddr = Ubel::FindArrayValueByIndex(dataAddr, idx);
            if (!elemAddr) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Array index %d out of range in member '%s'",
                         idx, memberName);
                SetError(-6, msg);
                return;
            }
            uint64_t elemVarAddr = elemAddr - DynOff::VARIANT_DATA;
            int32_t elemType = Zoltraak::ReadSafe<int32_t>(elemVarAddr + DynOff::VARIANT_TYPE, -1);

            g_zoltMailbox.objectAddr   = objAddr;
            g_zoltMailbox.memberAddr   = elemAddr;
            g_zoltMailbox.memberIndex  = foundIndex;
            g_zoltMailbox.memberType   = elemType;

            Solitaer::Info("PIPE", "Qual: GET_MEMBER_ADDR '%s.%s' arrayIdx=%d → type=%d addr=%p",
                           className, memberName, idx, elemType,
                           reinterpret_cast<void*>(elemAddr));
            SetDone(0);
            return;
        }
        // Not a container type — fall through to normal result
        Solitaer::Warn("PIPE", "Qual: GET_MEMBER_ADDR matchStr='%s' but member type=%d is not Dict/Array, ignoring",
                        dictKey, varType);
    }

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

    // ── Dict/Array key traversal via matchStr ──
    char dictKey[256];
    memcpy(dictKey, g_zoltMailbox.matchStr, sizeof(dictKey));
    dictKey[255] = '\0';

    if (dictKey[0] != '\0') {
        int type4x = varType;
        if (GodotVer::IsGodot3()) {
            if (type4x == 18) type4x = 27;
            else if (type4x == 19) type4x = 28;
        }

        if (type4x == 27) { // Dictionary
            uint64_t elemAddr = Ubel::FindDictValueByKey(dataAddr, std::string(dictKey));
            if (!elemAddr) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Dict key '%s' not found in '%s.%s' (scanned %d)",
                         dictKey, className, memberName, scannedCount);
                SetError(-6, msg);
                return;
            }
            uint64_t elemVarAddr = elemAddr - DynOff::VARIANT_DATA;
            int32_t elemType = Zoltraak::ReadSafe<int32_t>(elemVarAddr + DynOff::VARIANT_TYPE, -1);

            g_zoltMailbox.objectAddr  = foundObj;
            g_zoltMailbox.memberAddr  = elemAddr;
            g_zoltMailbox.memberIndex = foundIndex;
            g_zoltMailbox.memberType  = elemType;

            Solitaer::Info("PIPE", "Qual: FIND_INSTANCE_WITH_MEMBER '%s.%s' dictKey='%s' "
                           "→ obj=%p type=%d addr=%p (scanned %d)",
                           className, memberName, dictKey,
                           reinterpret_cast<void*>(foundObj), elemType,
                           reinterpret_cast<void*>(elemAddr), scannedCount);
            SetDone(0);
            return;
        }
        else if (type4x == 28) { // Array
            int idx = -1;
            try { idx = std::stoi(dictKey); } catch (...) {}
            if (idx >= 0) {
                uint64_t elemAddr = Ubel::FindArrayValueByIndex(dataAddr, idx);
                if (elemAddr) {
                    uint64_t elemVarAddr = elemAddr - DynOff::VARIANT_DATA;
                    int32_t elemType = Zoltraak::ReadSafe<int32_t>(elemVarAddr + DynOff::VARIANT_TYPE, -1);

                    g_zoltMailbox.objectAddr  = foundObj;
                    g_zoltMailbox.memberAddr  = elemAddr;
                    g_zoltMailbox.memberIndex = foundIndex;
                    g_zoltMailbox.memberType  = elemType;

                    Solitaer::Info("PIPE", "Qual: FIND_INSTANCE_WITH_MEMBER '%s.%s' arrayIdx=%d "
                                   "→ obj=%p type=%d addr=%p (scanned %d)",
                                   className, memberName, idx,
                                   reinterpret_cast<void*>(foundObj), elemType,
                                   reinterpret_cast<void*>(elemAddr), scannedCount);
                    SetDone(0);
                    return;
                }
            }
            Solitaer::Warn("PIPE", "Qual: matchStr='%s' but array lookup failed, returning member addr", dictKey);
        }
        else {
            Solitaer::Warn("PIPE", "Qual: matchStr='%s' but member type=%d is not Dict/Array, ignoring",
                            dictKey, varType);
        }
    }

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

// ---- Path walker helper ----

// Walk a dot-separated property path on an object, following Object pointers.
// Returns the string value of the FINAL member in the path, or "" on failure.
// Example: WalkPropertyPath(objAddr, "item.my_id") →
//   1. Read "item" on objAddr → Object@xxx
//   2. Read "my_id" on Object@xxx → "item_tree"
static std::string WalkPropertyPath(uint64_t objAddr, const std::string& path) {
    // Split path by '.'
    std::vector<std::string> segments;
    {
        size_t start = 0;
        while (start < path.size()) {
            size_t dot = path.find('.', start);
            if (dot == std::string::npos) {
                segments.push_back(path.substr(start));
                break;
            }
            segments.push_back(path.substr(start, dot - start));
            start = dot + 1;
        }
    }

    if (segments.empty()) return "";

    uint64_t currentObj = objAddr;

    for (size_t seg = 0; seg < segments.size(); ++seg) {
        const std::string& memberName = segments[seg];
        bool isLast = (seg == segments.size() - 1);

        // Get ScriptInstance on current object
        if (DynOff::OBJECT_SCRIPT_INSTANCE < 0) return "";
        uint64_t si = Zoltraak::ReadSafe<uint64_t>(
            currentObj + DynOff::OBJECT_SCRIPT_INSTANCE, 0);
        if (!si || !Zoltraak::IsReadable(si)) return "";

        // Verify owner backlink
        uint64_t owner = Zoltraak::ReadSafe<uint64_t>(si + DynOff::GDSCRIPT_OWNER_OFF, 0);
        if (owner != currentObj) return "";

        // Read members Vector
        uint64_t membersPtr = Zoltraak::ReadSafe<uint64_t>(
            si + DynOff::GDSCRIPT_MEMBERS_OFF, 0);
        if (!membersPtr || !Zoltraak::IsReadable(membersPtr)) return "";

        int sizeOff = GodotVer::IsGodot3() ? 4 : 8;
        uint32_t count = Zoltraak::ReadSafe<uint32_t>(membersPtr - sizeOff, 0);
        if (count == 0 || count > 500) return "";

        // Resolve member names
        uint64_t gdscriptPtr = Zoltraak::ReadSafe<uint64_t>(
            si + DynOff::GDSCRIPT_SCRIPT_OFF, 0);
        auto nameMap = Ubel::GetMemberNames(gdscriptPtr);

        // Find member index (case-insensitive)
        std::string memberLower = memberName;
        std::transform(memberLower.begin(), memberLower.end(), memberLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        int foundIdx = -1;
        for (auto& [idx, name] : nameMap) {
            if (name == memberName) { foundIdx = idx; break; }
            std::string nameLower = name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (nameLower == memberLower) { foundIdx = idx; break; }
        }

        if (foundIdx < 0 || foundIdx >= static_cast<int>(count)) return "";

        // Read the Variant at this member index
        int stride = DynOff::VARIANT_STRIDE;
        uint64_t varAddr = membersPtr + foundIdx * stride;
        int32_t varType = Zoltraak::ReadSafe<int32_t>(varAddr + DynOff::VARIANT_TYPE, -1);
        uint64_t dataAddr = varAddr + DynOff::VARIANT_DATA;

        if (isLast) {
            // Last segment — read value as string and return
            Ubel::VariantValue val = Ubel::ReadVariant(varAddr);
            return val.value;
        } else {
            // Intermediate segment — must be Object type, follow pointer
            // 3.x Object = raw type 17, 4.x Object = raw type 24
            bool isObject = GodotVer::IsGodot3() ? (varType == 17) : (varType == 24);
            if (!isObject) return ""; // Not an Object

            // Object Variant data: { ObjectID id(8B); Object* obj(8B) }
            // The pointer is at offset +8, NOT +0 (which is ObjectID)
            uint64_t childObj = Zoltraak::ReadSafe<uint64_t>(dataAddr + 8, 0);
            if (!childObj || !Zoltraak::IsReadable(childObj)) return "";

            currentObj = childObj;
        }
    }

    return "";
}

// CMD_FIND_INSTANCE_BY_PATH — Find an Object by matching a nested property path value
// Input: className (e.g. "Button") + nodeName (match path, e.g. "item.my_id")
//        + matchStr (expected value, e.g. "item_tree")
//        + memberName (member to resolve on found instance, e.g. "current_number")
// Output: objectAddr + memberAddr + memberIndex + memberType
//
// Solves the "@ClassName@NNN" problem: auto-generated node names change on restart,
// but the nested property (e.g. item.my_id) stays the same. Walk a dot-separated
// path for each instance, compare the final value, and resolve the target member.
static void HandleFindInstanceByPath() {
    char className[256], memberName[256], matchPath[256], matchStr[256];
    memcpy(className, g_zoltMailbox.className, sizeof(className));
    memcpy(memberName, g_zoltMailbox.memberName, sizeof(memberName));
    memcpy(matchPath, g_zoltMailbox.nodeName, sizeof(matchPath));
    memcpy(matchStr, g_zoltMailbox.matchStr, sizeof(matchStr));
    className[255] = '\0';
    memberName[255] = '\0';
    matchPath[255] = '\0';
    matchStr[255] = '\0';

    if (matchPath[0] == '\0') {
        SetError(-1, "Empty match path");
        return;
    }
    if (matchStr[0] == '\0') {
        SetError(-1, "Empty match value");
        return;
    }
    if (memberName[0] == '\0') {
        SetError(-1, "Empty member name");
        return;
    }

    Solitaer::Info("PIPE", "Qual: FIND_INSTANCE_BY_PATH class='%s' path='%s' match='%s' resolve='%s'",
                   className, matchPath, matchStr, memberName);

    // Prepare class filter (optional, case-insensitive)
    bool hasClassFilter = (className[0] != '\0');
    std::string targetClass(className);
    std::string targetClassLower = targetClass;
    if (hasClassFilter) {
        std::transform(targetClassLower.begin(), targetClassLower.end(), targetClassLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }

    std::string matchPathStr(matchPath);
    std::string matchStrVal(matchStr);
    // Case-insensitive match for the target value
    std::string matchStrLower = matchStrVal;
    std::transform(matchStrLower.begin(), matchStrLower.end(), matchStrLower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    uint64_t foundObj = 0;
    int scannedCount = 0;
    int pathWalkCount = 0;

    Ausetiese::ForEachEx([&](int /*index*/, uint64_t objAddr) {
        // Class filter (if specified)
        if (hasClassFilter) {
            std::string cls = Diagoldze::GetObjectClass(objAddr);
            if (cls.empty()) return true;
            std::string clsLower = cls;
            std::transform(clsLower.begin(), clsLower.end(), clsLower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (cls != targetClass && clsLower != targetClassLower) return true;
        }

        scannedCount++;

        // Must have ScriptInstance
        if (DynOff::OBJECT_SCRIPT_INSTANCE < 0) return true;
        uint64_t si = Zoltraak::ReadSafe<uint64_t>(
            objAddr + DynOff::OBJECT_SCRIPT_INSTANCE, 0);
        if (!si || !Zoltraak::IsReadable(si)) return true;

        // Walk the property path
        pathWalkCount++;
        std::string pathValue = WalkPropertyPath(objAddr, matchPathStr);
        if (pathValue.empty()) return true;

        // Log first few walk results for diagnostics
        if (pathWalkCount <= 3) {
            Solitaer::Info("PIPE", "Qual: path walk #%d obj=%p → '%s'",
                           pathWalkCount, reinterpret_cast<void*>(objAddr), pathValue.c_str());
        }

        // Compare (case-insensitive, strip surrounding quotes for strings)
        std::string pathValueClean = pathValue;
        // ReadVariant wraps strings in quotes — strip them for comparison
        if (pathValueClean.size() >= 2 && pathValueClean.front() == '"' && pathValueClean.back() == '"') {
            pathValueClean = pathValueClean.substr(1, pathValueClean.size() - 2);
        }
        std::string pathValueLower = pathValueClean;
        std::transform(pathValueLower.begin(), pathValueLower.end(), pathValueLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (pathValueClean == matchStrVal || pathValueLower == matchStrLower) {
            foundObj = objAddr;
            Solitaer::Info("PIPE", "Qual: FIND_INSTANCE_BY_PATH match! obj=%p path='%s' → '%s'",
                           reinterpret_cast<void*>(objAddr), matchPath, pathValue.c_str());
            return false; // Found!
        }

        return true;
    });

    if (!foundObj) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "No instance where %s == '%s' (scanned %d, walked %d)",
                 matchPath, matchStr, scannedCount, pathWalkCount);
        SetError(-2, msg);
        return;
    }

    // Resolve the target member on the found object
    // Reuse HandleGetMemberAddr logic
    uint64_t si = Zoltraak::ReadSafe<uint64_t>(
        foundObj + DynOff::OBJECT_SCRIPT_INSTANCE, 0);
    if (!si || !Zoltraak::IsReadable(si)) {
        SetError(-3, "Found object lost ScriptInstance");
        return;
    }

    uint64_t membersPtr = Zoltraak::ReadSafe<uint64_t>(
        si + DynOff::GDSCRIPT_MEMBERS_OFF, 0);
    if (!membersPtr || !Zoltraak::IsReadable(membersPtr)) {
        SetError(-4, "Members Vector not readable on found object");
        return;
    }

    // Scan members until invalid Variant type (don't trust CowData count —
    // custom builds may store refcount instead of size at ptr-8)
    int stride = DynOff::VARIANT_STRIDE;
    int maxType2 = GodotVer::VariantTypeMax();
    uint32_t count = 0;
    for (uint32_t m = 0; m < 500; m++) {
        int32_t mType = Zoltraak::ReadSafe<int32_t>(
            membersPtr + static_cast<uint64_t>(m) * stride, -1);
        if (mType < 0 || mType >= maxType2) break;
        count = m + 1;
    }
    if (count == 0) {
        SetError(-4, "No valid members found on object (scan-until-invalid)");
        return;
    }

    uint64_t gdscriptPtr = Zoltraak::ReadSafe<uint64_t>(
        si + DynOff::GDSCRIPT_SCRIPT_OFF, 0);
    auto nameMap = Ubel::GetMemberNames(gdscriptPtr);

    std::string targetMember(memberName);
    std::string targetMemberLower = targetMember;
    std::transform(targetMemberLower.begin(), targetMemberLower.end(), targetMemberLower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    int foundIndex = -1;
    for (auto& [idx, name] : nameMap) {
        if (name == targetMember) { foundIndex = idx; break; }
        std::string nameLower = name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (nameLower == targetMemberLower) { foundIndex = idx; break; }
    }

    if (foundIndex < 0 || foundIndex >= static_cast<int>(count)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Found object but member '%s' not found (%d names resolved, %u scanned)",
                 memberName, static_cast<int>(nameMap.size()), count);
        SetError(-5, msg);
        return;
    }
    uint64_t varAddr = membersPtr + foundIndex * stride;
    uint64_t dataAddr = varAddr + DynOff::VARIANT_DATA;
    int32_t varType = Zoltraak::ReadSafe<int32_t>(varAddr + DynOff::VARIANT_TYPE, -1);

    g_zoltMailbox.objectAddr  = foundObj;
    g_zoltMailbox.memberAddr  = dataAddr;
    g_zoltMailbox.memberIndex = foundIndex;
    g_zoltMailbox.memberType  = varType;

    Solitaer::Info("PIPE", "Qual: FIND_INSTANCE_BY_PATH → obj=%p '%s'[%d] type=%d addr=%p "
                   "(scanned %d, walked %d)",
                   reinterpret_cast<void*>(foundObj), memberName, foundIndex, varType,
                   reinterpret_cast<void*>(dataAddr), scannedCount, pathWalkCount);
    SetDone(0);
}

// CMD_FIND_MANAGED_INSTANCES — Bulk: find ALL .NET managed objects matching criteria
// Input:
//   className       = Godot class filter (empty = all classes)
//   targetOp        = FilterOp for managed field comparison (FILTER_NONE = no filter)
//   targetValue     = comparison value (int64)
//   filterOp        = repurposed: managed field offset within .NET object (int32)
//   filterValue     = repurposed: field read size in bytes (4=int32, 8=int64; default 4)
// Output:
//   result          = count of matching objects
//   g_zoltManagedResults buffer populated with objectAddr + managedAddr pairs
//
// Performance: single-pass through ObjectDB with direct memory reads.
// Typical: <500ms for 50k+ objects (no CreateRemoteThread overhead).
// Timeout: 10 seconds max to prevent UI freeze on pathological cases.
static void HandleFindManagedInstances() {
    char className[256];
    memcpy(className, g_zoltMailbox.className, sizeof(className));
    className[255] = '\0';

    // nodeName field: optional MethodTable hex value for type matching
    // (e.g. "7FF7D90BE0F8" — filters to only .NET objects with this exact MT)
    char mtHexStr[256];
    memcpy(mtHexStr, g_zoltMailbox.nodeName, sizeof(mtHexStr));
    mtHexStr[255] = '\0';
    uint64_t requiredMT = 0;
    if (mtHexStr[0] != '\0') {
        char* endp = nullptr;
        requiredMT = strtoull(mtHexStr, &endp, 16);
        if (endp == mtHexStr) requiredMT = 0; // parse failed
    }

    // memberName field: optional Godot node name filter (e.g. "Creature")
    // Narrows scan to only objects whose node name contains this substring.
    // This prevents Game/HealthBar/etc. from polluting auto-discover MT.
    char nodeNameFilter[256];
    memcpy(nodeNameFilter, g_zoltMailbox.memberName, sizeof(nodeNameFilter));
    nodeNameFilter[255] = '\0';

    int32_t filterFieldOp = g_zoltMailbox.targetOp;
    int64_t filterFieldValue = g_zoltMailbox.targetValue;
    int32_t managedFieldOffset = g_zoltMailbox.filterOp;       // repurposed
    int64_t managedFieldSize = g_zoltMailbox.filterValue;      // repurposed
    if (managedFieldSize <= 0 || managedFieldSize > 8) managedFieldSize = 4;

    bool hasClassFilter = (className[0] != '\0');
    bool hasNodeNameFilter = (nodeNameFilter[0] != '\0');
    bool hasFieldFilter = (filterFieldOp != Zolt::Qual::FILTER_NONE && managedFieldOffset >= 0);
    bool hasMTFilter = (requiredMT != 0);

    Solitaer::Info("PIPE", "Qual: FIND_MANAGED_INSTANCES class='%s' node='%s' MT=%s fieldOff=0x%X fieldOp=%d fieldVal=%lld fieldSize=%lld",
                   hasClassFilter ? className : "*",
                   hasNodeNameFilter ? nodeNameFilter : "*",
                   hasMTFilter ? mtHexStr : "(any)",
                   managedFieldOffset, filterFieldOp,
                   static_cast<long long>(filterFieldValue),
                   static_cast<long long>(managedFieldSize));

    // Check .NET game
    if (!DynOff::bIsDotNetGame.load(std::memory_order_relaxed)) {
        SetError(-1, "Not a .NET Godot game");
        return;
    }

    int siOff = DynOff::OBJECT_SCRIPT_INSTANCE;
    if (siOff < 0) {
        SetError(-2, "ScriptInstance offset not probed");
        return;
    }

    // Prepare class filter
    std::string targetClass(className);
    std::string targetClassLower = targetClass;
    if (hasClassFilter) {
        std::transform(targetClassLower.begin(), targetClassLower.end(), targetClassLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }

    // Pre-compute lowercase node name filter (avoid repeated tolower in loop)
    std::string nodeNameFilterLower(nodeNameFilter);
    if (hasNodeNameFilter) {
        std::transform(nodeNameFilterLower.begin(), nodeNameFilterLower.end(), nodeNameFilterLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }

    // Clear results buffer
    g_zoltManagedResults.count = 0;

    int scannedCount = 0;
    int managedCount = 0;
    int matchedCount = 0;
    // Track L2 wrapper offset per result for smart dedup — smaller offset = more direct owner.
    // L1 (direct wrapper match) uses 0; L2 uses the wrapper field offset where child ref was found.
    int entryWrapOff[MANAGED_RESULTS_MAX] = {};

    // Timeout: 10 seconds max
    DWORD startTick = GetTickCount();
    constexpr DWORD TIMEOUT_MS = 10000;

    Ausetiese::ForEachEx([&](int /*index*/, uint64_t objAddr) {
        // Timeout check every 1000 objects
        if ((scannedCount & 0x3FF) == 0 && scannedCount > 0) {
            if (GetTickCount() - startTick > TIMEOUT_MS) {
                Solitaer::Warn("PIPE", "Qual: FIND_MANAGED_INSTANCES timeout after %d objects", scannedCount);
                return false;
            }
        }

        scannedCount++;

        // Class filter
        if (hasClassFilter) {
            std::string cls = Diagoldze::GetObjectClass(objAddr);
            if (cls.empty()) return true;
            std::string clsLower = cls;
            std::transform(clsLower.begin(), clsLower.end(), clsLower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (cls != targetClass && clsLower != targetClassLower) return true;
        }

        // Read ScriptInstance
        uint64_t scriptInst = Zoltraak::ReadSafe<uint64_t>(objAddr + siOff, 0);
        if (!scriptInst || !Zoltraak::IsReadable(scriptInst)) return true;

        // Verify CSharpInstance owner backlink
        uint64_t owner = Zoltraak::ReadSafe<uint64_t>(
            scriptInst + DynOff::CSHARP_OWNER_OFF, 0);
        if (owner != objAddr) return true;

        // Node name filter — applied AFTER ScriptInstance validation to avoid
        // expensive GetNodeName() calls on non-.NET objects. Only confirmed
        // .NET objects (with valid CSharpInstance) reach this point.
        if (hasNodeNameFilter) {
            std::string nodeName = Serie::GetNodeName(objAddr);
            if (nodeName.empty()) return true;
            // Case-insensitive substring match (filter already lowercased above)
            std::string nodeNameLower = nodeName;
            std::transform(nodeNameLower.begin(), nodeNameLower.end(), nodeNameLower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (nodeNameLower.find(nodeNameFilterLower) == std::string::npos) return true;
        }

        // Read GCHandle
        uint64_t gcHandle = Zoltraak::ReadSafe<uint64_t>(
            scriptInst + DynOff::CSHARP_GCHANDLE_OFF, 0);
        if (!gcHandle || !Zoltraak::IsReadable(gcHandle)) return true;

        // Dereference GCHandle → managed object
        uint64_t managedObj = Zoltraak::ReadSafe<uint64_t>(gcHandle, 0);
        if (!managedObj || !Zoltraak::IsReadable(managedObj)) return true;

        // MethodTable validation (first qword must be high, aligned pointer)
        uint64_t mt = Zoltraak::ReadSafe<uint64_t>(managedObj, 0);
        if (mt < 0x7F0000000000ULL || (mt & 0x7) != 0) return true;

        // NOTE: MT filter is applied INSIDE L1/L2 logic, not here.
        // For L1 (wrapper direct): MT = wrapper's MethodTable.
        // For L2 (nested child):   MT = child's MethodTable.
        // This is critical because the user provides the CHILD's MT (from Instance Finder),
        // not the wrapper's MT.

        managedCount++;

        // Track wrapper offset for smart dedup (0 = L1 direct or no-filter; >0 = L2 offset)
        int matchWrapOff = 0;

        // Apply managed field filter — with Level 2 nested object scanning.
        // The GCHandle chain gives a .NET WRAPPER object, but the actual game data
        // (HP, Side, etc.) often lives in a NESTED .NET object referenced by the wrapper.
        // Level 1: check field directly on wrapper (managedObj + offset).
        // Level 2: scan wrapper's reference fields for child .NET objects, check those.
        if (hasFieldFilter) {
            // Helper: read a managed field value at a given base + offset
            auto readManagedField = [&](uint64_t base, int offset) -> int64_t {
                uint64_t fieldAddr = base + offset;
                if (managedFieldSize == 4)
                    return static_cast<int64_t>(Zoltraak::ReadSafe<int32_t>(fieldAddr, 0));
                else if (managedFieldSize == 8)
                    return Zoltraak::ReadSafe<int64_t>(fieldAddr, 0);
                else if (managedFieldSize == 2)
                    return static_cast<int64_t>(Zoltraak::ReadSafe<int16_t>(fieldAddr, 0));
                else
                    return static_cast<int64_t>(Zoltraak::ReadSafe<int8_t>(fieldAddr, 0));
            };

            // Level 1: try the wrapper directly (MT check against wrapper MT)
            bool matched = false;
            uint64_t finalManagedObj = managedObj;
            uint64_t matchedChildMt = 0; // MT of the matched object (for auto-lock)

            {
                bool mtOk = !hasMTFilter || (mt == requiredMT);
                if (mtOk) {
                    int64_t actual = readManagedField(managedObj, managedFieldOffset);
                    if (ApplyFilterOp(filterFieldOp, actual, filterFieldValue)) {
                        matched = true;
                        matchedChildMt = mt;
                    }
                }
            }

            // Level 2: if L1 failed, scan wrapper's reference fields for nested .NET objects.
            // Same approach as Phase 4.6 in Fern.cpp — scan 8-byte aligned fields in the
            // wrapper for pointers to child .NET objects, then check their fields.
            // MT filter applies to the CHILD's MethodTable here (not the wrapper's).
            if (!matched) {
                constexpr int L2_WRAPPER_SCAN = 0x200; // scan up to 512 bytes of wrapper
                uint8_t wrapBuf[L2_WRAPPER_SCAN];
                if (Zoltraak::ReadSafe(managedObj, wrapBuf, L2_WRAPPER_SCAN)) {
                    for (int w = 8; w + 8 <= L2_WRAPPER_SCAN; w += 8) {
                        uint64_t childPtr;
                        memcpy(&childPtr, wrapBuf + w, sizeof(childPtr));

                        // Filter: must be non-null, 8-byte aligned, heap pointer
                        if (!childPtr || (childPtr & 0x7) != 0) continue;
                        if ((childPtr >> 40) == 0) continue;

                        // Validate child has a MethodTable
                        uint64_t childMt = Zoltraak::ReadSafe<uint64_t>(childPtr, 0);
                        if (childMt < 0x7F0000000000ULL || (childMt & 0x7) != 0) continue;
                        if (!Zoltraak::IsReadable(childMt)) continue;

                        // MT filter: check child's MT (user provides child MT from Instance Finder)
                        if (hasMTFilter && childMt != requiredMT) continue;

                        // Field offset must be past MethodTable
                        if (managedFieldOffset < 8) continue;

                        int64_t childActual = readManagedField(childPtr, managedFieldOffset);
                        if (ApplyFilterOp(filterFieldOp, childActual, filterFieldValue)) {
                            matched = true;
                            finalManagedObj = childPtr;
                            matchedChildMt = childMt;
                            matchWrapOff = w; // track which wrapper field led to this child
                            if (managedCount <= 5) {
                                Solitaer::Info("PIPE", "Qual: [diag] managed#%d L2 HIT wrapper=%llX +0x%X→child=%llX "
                                               "childMT=%llX field@0x%X=%lld",
                                               managedCount,
                                               static_cast<unsigned long long>(managedObj), w,
                                               static_cast<unsigned long long>(childPtr),
                                               static_cast<unsigned long long>(childMt),
                                               managedFieldOffset,
                                               static_cast<long long>(childActual));
                            }
                            break;
                        }
                    }
                }
            }

            // Debug: log first 5 non-matching managed objects for diagnostics
            if (managedCount <= 5 && !matched) {
                Solitaer::Info("PIPE", "Qual: [diag] managed#%d obj=%llX managed=%llX wrapperMT=%llX "
                               "→ NO MATCH (L1+L2)",
                               managedCount,
                               static_cast<unsigned long long>(objAddr),
                               static_cast<unsigned long long>(managedObj),
                               static_cast<unsigned long long>(mt));
            }

            if (!matched) return true;

            // Auto-lock MethodTable on first match — use the MATCHED object's MT
            // (child MT for L2 hits, wrapper MT for L1 hits) for type consistency.
            bool isL2 = (finalManagedObj != managedObj);
            if (!hasMTFilter && matchedCount == 0 && matchedChildMt != 0) {
                requiredMT = matchedChildMt;
                hasMTFilter = true;
                Solitaer::Info("PIPE", "Qual: Auto-locked MT to %llX from first matching %s object",
                               static_cast<unsigned long long>(matchedChildMt),
                               isL2 ? "L2 child" : "L1 wrapper");
            }

            // Use finalManagedObj (could be wrapper or nested child) for the result
            managedObj = finalManagedObj;
        } else {
            // No field filter — just MT filter on wrapper
            if (hasMTFilter && mt != requiredMT) return true;
        }

        // Deduplicate by managedAddr — multiple Godot objects can reference the
        // same nested .NET object (e.g., Game + Creature + HealthBar all share
        // the same .NET data child). When a duplicate is found, prefer the owner
        // with the smaller wrapper offset (L1=0 beats any L2; smaller L2 offset
        // means more direct ownership — e.g. Creature at +0x08 beats Game at +0x70).
        {
            int idx = g_zoltManagedResults.count;
            int dupIdx = -1;
            for (int d = 0; d < idx; d++) {
                if (g_zoltManagedResults.entries[d].managedAddr == managedObj) {
                    dupIdx = d;
                    break;
                }
            }
            if (dupIdx >= 0) {
                // Duplicate — replace if this match has a more direct owner (smaller wrapOff)
                if (matchWrapOff < entryWrapOff[dupIdx]) {
                    int oldOff = entryWrapOff[dupIdx];
                    g_zoltManagedResults.entries[dupIdx].objectAddr = objAddr;
                    entryWrapOff[dupIdx] = matchWrapOff;
                    Solitaer::Info("PIPE", "Qual: dedup replaced obj=%llX (wrapOff 0x%X→0x%X, better owner)",
                                   static_cast<unsigned long long>(objAddr), oldOff, matchWrapOff);
                }
                return true;
            }

            matchedCount++;
            if (idx < MANAGED_RESULTS_MAX) {
                g_zoltManagedResults.entries[idx].objectAddr = objAddr;
                g_zoltManagedResults.entries[idx].managedAddr = managedObj;
                entryWrapOff[idx] = matchWrapOff;
                g_zoltManagedResults.count = idx + 1;
            }
        }

        return true; // continue — find ALL matches
    });

    g_zoltMailbox.result = matchedCount;

    Solitaer::Info("PIPE", "Qual: FIND_MANAGED_INSTANCES → %d matched (%d managed, %d scanned) in %lu ms",
                   matchedCount, managedCount, scannedCount,
                   GetTickCount() - startTick);

    if (matchedCount == 0) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "No managed objects matched (class='%s', %d managed found, %d scanned)",
                 hasClassFilter ? className : "*", managedCount, scannedCount);
        SetError(-3, msg);
    } else {
        SetDone(matchedCount);
    }
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
