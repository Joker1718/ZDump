// Stark — 修塔爾克 (Brave Warrior — Export API)
// C ABI export functions for CE Lua and external callers.
// Orchestrates initialization: Solitaer -> Frieren -> subsystem init.

#include "Stark.h"
#include "Frieren.h"
#include "Ausetiese.h"
#include "Diagoldze.h"
#include "Serie.h"
#include "Ubel.h"
#include "Flamme.h"
#include "Fern.h"
#include "Qual.h"
#include "Heiter.h"
#include "Denken.h"
#include "Solitaer.h"
#include "Zoltraak.h"
#include "Grimoire.h"
#include "BuildInfo.h"

#include <filesystem>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace fs = std::filesystem;

// Global state
static uint64_t    s_objectDBAddr  = 0;
static uint64_t    s_classDBAddr   = 0;
static uint64_t    s_sceneTreeAddr = 0;
static uint64_t    s_engineAddr    = 0;
static std::string s_godotVersion;
static bool        s_initialized   = false;

// Version string buffer (static storage for C ABI return)
static char s_versionBuf[64] = ZOLT_VERSION_STRING;

// Background SceneTree retry thread — runs when initial scan found the
// BSS address but the singleton pointer was NULL (game not ready yet).
// Periodically retries until SceneTree is populated or timeout (30s).
static DWORD WINAPI SceneTreeRetryThread(LPVOID /*param*/) {
    Zolt::Solitaer::Info("INIT", "SceneTree retry thread started");

    bool vtablesClassified = false;

    for (int attempt = 0; attempt < 15; ++attempt) {
        Sleep(2000);

        // Check if Serie already resolved it via lazy deref
        if (Zolt::Serie::GetRoot()) {
            Zolt::Solitaer::Summary("SceneTree resolved (lazy deref, attempt %d)", attempt + 1);
            return 0;
        }

        // On first retry, classify unresolved vtables via static code analysis
        // (needed for custom builds where RTTI is stripped)
        if (!vtablesClassified && Zolt::Diagoldze::GetUnresolvedVtableCount() > 3) {
            Zolt::Solitaer::Info("INIT", "SceneTree retry: classifying vtables via creation_func analysis");
            Zolt::Diagoldze::ClassifyUnresolvedVtables();
            vtablesClassified = true;
        }

        // Try full FindSceneTree with strict validation
        uintptr_t addr = Zolt::Frieren::FindSceneTree();
        if (addr) {
            Zolt::Serie::Init(addr);
            if (Zolt::Serie::GetRoot()) {
                s_sceneTreeAddr = addr;
                Zolt::Solitaer::Summary("SceneTree retry: found at %p (attempt %d)",
                    reinterpret_cast<void*>(addr), attempt + 1);
                return 0;
            }
        }
    }

    Zolt::Solitaer::Warn("INIT", "SceneTree retry thread gave up after 30s");
    return 1;
}

extern "C" {

int Zolt_Init() {
    if (s_initialized) return 0;
    Zolt::Solitaer::LogInit("Zolt_Init called");
    s_initialized = true;
    return 0;
}

int Zolt_AutoStart() {
    Zolt::Solitaer::LogInit("Zolt_AutoStart called");

    // Initialize process-specific logging
    wchar_t processPath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, processPath, MAX_PATH);
    std::wstring processName = fs::path(processPath).stem().wstring();
    Zolt::Solitaer::InitProcessMirror(processName);

    Zolt::Solitaer::Summary("Zolt-Dump v%s (%s) starting in '%ls'",
        ZOLT_VERSION_STRING, ZOLT_GIT_SHORT, processName.c_str());

    // Run Frieren scan (discovers all Godot globals)
    Zolt::Frieren::EnginePointers ptrs;
    bool success;
    {
        LOG_TIMED("INIT", "Frieren::FindAll (AOB scan)");
        success = Zolt::Frieren::FindAll(ptrs);
    }

    // Store results for C ABI getters
    s_objectDBAddr  = ptrs.ObjectDB;
    s_classDBAddr   = ptrs.ClassDB;
    s_sceneTreeAddr = ptrs.SceneTree;
    s_engineAddr    = ptrs.Engine;
    s_godotVersion  = ptrs.godotVersion;

    // Initialize subsystems with discovered addresses
    if (s_classDBAddr) {
        LOG_TIMED("INIT", "Diagoldze::Init (ClassDB)");
        Zolt::Diagoldze::Init(s_classDBAddr);
    }
    if (s_objectDBAddr) {
        LOG_TIMED("INIT", "Ausetiese::Init (ObjectDB)");
        Zolt::Ausetiese::Init(s_objectDBAddr);
    }
    if (s_sceneTreeAddr) {
        LOG_TIMED("INIT", "Serie::Init (SceneTree)");
        Zolt::Serie::Init(s_sceneTreeAddr);
    }

    // Log diagnostics for each subsystem
    Zolt::Flamme::LogDiagnostics();
    Zolt::Diagoldze::LogDiagnostics();
    Zolt::Ausetiese::LogDiagnostics();

    // Proactive RTTI scan: pre-seed the vtable→class cache by scanning ObjectDB
    if (s_objectDBAddr && s_classDBAddr) {
        LOG_TIMED("INIT", "ScanObjectVtables (RTTI)");
        std::vector<uint64_t> sampleAddrs;
        sampleAddrs.reserve(200);
        Zolt::Ausetiese::ForEachEx([&](int /*index*/, uint64_t objAddr) {
            sampleAddrs.push_back(objAddr);
            return sampleAddrs.size() < 200;
        });
        Zolt::Diagoldze::ScanObjectVtables(sampleAddrs);
    }

    // Probe ScriptInstance offset early by scanning ObjectDB for scripted nodes
    // Ubel — 優貝爾 — Early script probe for GDScript member access
    if (s_objectDBAddr && s_classDBAddr) {
        LOG_TIMED("INIT", "Ubel::ProbeScriptInstanceEarly");
        Zolt::Ubel::ProbeScriptInstanceEarly();
    }

    // .NET/C# Godot detection + CSharpInstance layout probe.
    // Must run here (not just in trigger_scan) so the CE Lua mailbox can use
    // CMD_FIND_MANAGED_INSTANCES without requiring the UI to connect first.
    {
        using namespace Zolt;
        bool isDotNet = (Zoltraak::GetModuleBase("GodotSharp") != 0);
        if (!isDotNet) {
            isDotNet = (Zoltraak::GetModuleBase("coreclr") != 0);
        }
        DynOff::bIsDotNetGame.store(isDotNet, std::memory_order_release);
        if (isDotNet) {
            Solitaer::Info("INIT", ".NET Godot game detected (GodotSharp/coreclr module found)");

            // Probe CSharpInstance layout: discover owner and GCHandle offsets
            int siOff = DynOff::OBJECT_SCRIPT_INSTANCE;
            if (siOff >= 0 && s_objectDBAddr) {
                LOG_TIMED("INIT", "CSharpInstance layout probe");
                bool probed = false;
                static const int ownerCandidates[] = { 0x08, 0x10 };
                static const int gcCandidates[] = { 0x20, 0x28 };

                Ausetiese::ForEachEx([&](int /*index*/, uint64_t objAddr) {
                    if (probed) return false;
                    uint64_t si = Zoltraak::ReadSafe<uint64_t>(objAddr + siOff, 0);
                    if (!si || !Zoltraak::IsReadable(si)) return true;

                    for (int ownerOff : ownerCandidates) {
                        uint64_t owner = Zoltraak::ReadSafe<uint64_t>(si + ownerOff, 0);
                        if (owner != objAddr) continue;

                        for (int gcOff : gcCandidates) {
                            uint64_t gcH = Zoltraak::ReadSafe<uint64_t>(si + gcOff, 0);
                            if (!gcH || !Zoltraak::IsReadable(gcH)) continue;
                            uint64_t managed = Zoltraak::ReadSafe<uint64_t>(gcH, 0);
                            if (!managed || !Zoltraak::IsReadable(managed)) continue;
                            uint64_t mt = Zoltraak::ReadSafe<uint64_t>(managed, 0);
                            if (mt < 0x7F0000000000ULL || (mt & 0x7) != 0) continue;

                            DynOff::CSHARP_OWNER_OFF = ownerOff;
                            DynOff::CSHARP_GCHANDLE_OFF = gcOff;
                            probed = true;
                            Solitaer::Info("INIT",
                                "CSharpInstance layout: owner=+0x%X gchandle=+0x%X (obj=%llX)",
                                ownerOff, gcOff, static_cast<unsigned long long>(objAddr));
                            return false;
                        }
                    }
                    return true;
                });

                if (!probed) {
                    Solitaer::Warn("INIT",
                        "CSharpInstance probe failed — using defaults (owner=+0x%X gc=+0x%X)",
                        DynOff::CSHARP_OWNER_OFF, DynOff::CSHARP_GCHANDLE_OFF);
                }
            }

            // Denken — 頓肯 — Initialize CLR hosting bridge for managed reflection.
            // Uses hostfxr already loaded by the game to attach to the running CoreCLR,
            // then loads ZoltReflector.dll for type/field enumeration.
            {
                LOG_TIMED("INIT", "Denken::Init (CLR bridge)");
                Denken::Init();
            }
        }
    }

    // Start Fern pipe server for UI communication
    Zolt::Fern::Start();

    // Initialize Heiter game thread dispatcher (WH_GETMESSAGE hook)
    {
        extern HMODULE Zolt_GetDllModule();
        bool hookReady = Zolt::Heiter::Init(Zolt_GetDllModule());
        Zolt::Solitaer::Info("INIT", "Heiter: %s",
            hookReady ? "hook installed" : "deferred (no window yet)");

        // Fallback vtable classification: if RTTI failed for many vtables
        // (common in custom Godot builds with stripped RTTI), use static code
        // analysis of ClassDB creation functions to extract vtable addresses.
        // Safe: no game function calls — pure memory reads + x86 disassembly.
        if (s_objectDBAddr && s_classDBAddr) {
            int unresolved = Zolt::Diagoldze::GetUnresolvedVtableCount();
            if (unresolved > 3) {
                LOG_TIMED("INIT", "ClassifyUnresolvedVtables (static)");
                Zolt::Diagoldze::ClassifyUnresolvedVtables();

                // Re-probe SceneTree root now that class names are available
                if (s_sceneTreeAddr && !Zolt::Serie::GetRoot()) {
                    Zolt::Solitaer::Info("INIT", "Re-probing SceneTree after vtable classification");
                    Zolt::Serie::Init(s_sceneTreeAddr);
                }
            }
        }
    }

    // If SceneTree BSS address was found but singleton is NULL,
    // spawn a background thread to periodically retry resolution.
    if (s_sceneTreeAddr && !Zolt::Serie::GetRoot()) {
        Zolt::Solitaer::Info("INIT", "SceneTree not yet populated, starting background retry thread");
        HANDLE hRetry = CreateThread(nullptr, 0, SceneTreeRetryThread, nullptr, 0, nullptr);
        if (hRetry) CloseHandle(hRetry);
    }

    s_initialized = true;

    // Condensed init summary
    Zolt::Solitaer::Summary("=============== Zolt-Dump %s Init Complete ===============", ZOLT_VERSION_STRING);
    Zolt::Solitaer::Summary("  Godot:     %s", s_godotVersion.empty() ? "(unknown)" : s_godotVersion.c_str());
    Zolt::Solitaer::Summary("  ObjectDB:  %p (%d objects, %d slots)",
        reinterpret_cast<void*>(s_objectDBAddr),
        Zolt::Ausetiese::GetObjectCount(), Zolt::Ausetiese::GetSlotMax());
    Zolt::Solitaer::Summary("  ClassDB:   %p (%d classes)",
        reinterpret_cast<void*>(s_classDBAddr),
        Zolt::Diagoldze::GetClassCount());
    Zolt::Solitaer::Summary("  SceneTree: %p (root=%s)",
        reinterpret_cast<void*>(s_sceneTreeAddr),
        Zolt::Serie::GetRoot() ? "found" : "NULL");
    Zolt::Solitaer::Summary("  Engine:    %p", reinterpret_cast<void*>(s_engineAddr));
    Zolt::Solitaer::Summary("  PropInfo:  nameOff=+0x%X hintOff=+0x%X usageOff=+0x%X size=%d",
        Zolt::DynOff::PROPINFO_NAME_OFFSET,
        Zolt::DynOff::PROPINFO_HINT_OFFSET,
        Zolt::DynOff::PROPINFO_USAGE_OFFSET,
        Zolt::DynOff::PROPINFO_TOTAL_SIZE);
    Zolt::Solitaer::Summary("  Heiter:    %s", Zolt::Heiter::IsReady() ? "hook active" : "deferred");
    Zolt::Solitaer::Summary("  Denken:    %s", Zolt::Denken::IsReady() ? "CLR bridge active" : "inactive");
    Zolt::Solitaer::Summary("  Status:    %s", success ? "OK" : "PARTIAL");
    Zolt::Solitaer::Summary("==========================================================");

    // Notify mailbox that DLL is ready for CE Lua commands
    // Qual — 全 — Transitions from STATUS_INIT to STATUS_IDLE
    Zolt::Qual::NotifyReady();

    return success ? 0 : 1;
}

void Zolt_Shutdown() {
    Zolt::Solitaer::LogInit("Zolt_Shutdown called");
    Zolt::Denken::Shutdown();   // Denken — 頓肯 — Release CLR bridge
    Zolt::Heiter::Shutdown();   // Before Fern — unblock any sync waiters first
    Zolt::Fern::Stop();
    Zolt::Qual::StopPolling();  // Qual — 全 — Stop mailbox polling
    Zolt::Solitaer::Shutdown();
    s_initialized = false;
}

const char* Zolt_GetVersion() {
    return s_versionBuf;
}

uint64_t Zolt_GetObjectDBAddr() {
    return s_objectDBAddr;
}

uint64_t Zolt_GetClassDBAddr() {
    return s_classDBAddr;
}

uint64_t Zolt_GetSceneTreeAddr() {
    return s_sceneTreeAddr;
}

uint64_t Zolt_GetEngineAddr() {
    return s_engineAddr;
}

const char* Zolt_GetGodotVersion() {
    return s_godotVersion.c_str();
}

int Zolt_GetObjectCount() {
    return Zolt::Ausetiese::GetObjectCount();
}

// ----- Phase 2: Object Enumeration exports -----

int Zolt_GetSlotMax() {
    return Zolt::Ausetiese::GetSlotMax();
}

uint64_t Zolt_GetObjectByIndex(int index) {
    return Zolt::Ausetiese::GetObjectByIndex(index);
}

int Zolt_FindObjectByAddress(uint64_t addr) {
    return Zolt::Ausetiese::FindByAddress(addr);
}

int Zolt_GetClassCount() {
    return Zolt::Diagoldze::GetClassCount();
}

// Static buffer for class name return (C ABI requires stable pointer)
static char s_classNameBuf[256] = {};

const char* Zolt_GetClassName(uint64_t objectAddr) {
    std::string name = Zolt::Diagoldze::GetObjectClass(objectAddr);
    if (name.empty()) {
        s_classNameBuf[0] = '\0';
    } else {
        strncpy_s(s_classNameBuf, name.c_str(), sizeof(s_classNameBuf) - 1);
    }
    return s_classNameBuf;
}

uint64_t Zolt_GetRootNode() {
    return Zolt::Serie::GetRoot();
}

// ----- Phase 3: Pipe Server exports -----

int Zolt_StartPipeServer() {
    Zolt::Fern::Start();
    return 0;
}

void Zolt_StopPipeServer() {
    Zolt::Fern::Stop();
}

int Zolt_IsPipeConnected() {
    return Zolt::Fern::IsConnected() ? 1 : 0;
}

// ----- Phase 4: .NET Managed Object exports -----

int Zolt_IsDotNetGame() {
    return Zolt::DynOff::bIsDotNetGame.load(std::memory_order_relaxed) ? 1 : 0;
}

uint64_t Zolt_GetManagedObject(uint64_t objectAddr) {
    if (!objectAddr || !Zolt::DynOff::bIsDotNetGame.load(std::memory_order_relaxed))
        return 0;

    // Step 1: Read ScriptInstance* from Object
    int siOff = Zolt::DynOff::OBJECT_SCRIPT_INSTANCE;
    if (siOff < 0) return 0;

    uint64_t scriptInst = Zolt::Zoltraak::ReadSafe<uint64_t>(objectAddr + siOff, 0);
    if (!scriptInst || !Zolt::Zoltraak::IsReadable(scriptInst))
        return 0;

    // Step 2: Verify it's a CSharpInstance (owner backlink at +0x08)
    uint64_t owner = Zolt::Zoltraak::ReadSafe<uint64_t>(
        scriptInst + Zolt::DynOff::CSHARP_OWNER_OFF, 0);
    if (owner != objectAddr) return 0;

    // Step 3: Read GCHandle at CSharpInstance+0x20
    uint64_t gcHandle = Zolt::Zoltraak::ReadSafe<uint64_t>(
        scriptInst + Zolt::DynOff::CSHARP_GCHANDLE_OFF, 0);
    if (!gcHandle || !Zolt::Zoltraak::IsReadable(gcHandle))
        return 0;

    // Step 4: Dereference GCHandle → managed object
    uint64_t managedObj = Zolt::Zoltraak::ReadSafe<uint64_t>(gcHandle, 0);
    if (!managedObj || !Zolt::Zoltraak::IsReadable(managedObj))
        return 0;

    // Step 5: Basic MethodTable validation (first qword should be a high pointer)
    uint64_t mt = Zolt::Zoltraak::ReadSafe<uint64_t>(managedObj, 0);
    if (mt < 0x7F0000000000ULL || (mt & 0x7) != 0)
        return 0;

    return managedObj;
}

int Zolt_GetScriptInstanceOffset() {
    return Zolt::DynOff::OBJECT_SCRIPT_INSTANCE;
}

} // extern "C"

namespace Zolt::Stark {

int AutoStart() {
    return Zolt_AutoStart();
}

} // namespace Zolt::Stark
