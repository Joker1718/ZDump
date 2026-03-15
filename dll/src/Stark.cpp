// Stark — 修塔爾克 (Brave Warrior — Export API)
// C ABI export functions for CE Lua and external callers.
// Orchestrates initialization: Solitaer -> Frieren -> subsystem init.

#include "Stark.h"
#include "Frieren.h"
#include "Ausetiese.h"
#include "Diagoldze.h"
#include "Serie.h"
#include "Flamme.h"
#include "Fern.h"
#include "Solitaer.h"
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

    for (int attempt = 0; attempt < 15; ++attempt) {
        Sleep(2000);

        // Check if Serie already resolved it via lazy deref
        if (Zolt::Serie::GetRoot()) {
            Zolt::Solitaer::Summary("SceneTree resolved (lazy deref, attempt %d)", attempt + 1);
            return 0;
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

    // Start Fern pipe server for UI communication
    Zolt::Fern::Start();

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
    Zolt::Solitaer::Summary("  Status:    %s", success ? "OK" : "PARTIAL");
    Zolt::Solitaer::Summary("==========================================================");

    return success ? 0 : 1;
}

void Zolt_Shutdown() {
    Zolt::Solitaer::LogInit("Zolt_Shutdown called");
    Zolt::Fern::Stop();
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

} // extern "C"

namespace Zolt::Stark {

int AutoStart() {
    return Zolt_AutoStart();
}

} // namespace Zolt::Stark
