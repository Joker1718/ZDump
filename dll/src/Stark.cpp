// Stark — 修塔爾克 (Brave Warrior — Export API)
// C ABI export functions for CE Lua and external callers.
// Orchestrates initialization: Solliter -> Frieren -> subsystem init.

#include "Stark.h"
#include "Frieren.h"
#include "Ausetiese.h"
#include "DiAgolze.h"
#include "Serie.h"
#include "Fern.h"
#include "Solliter.h"
#include "BuildInfo.h"

#include <filesystem>

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

extern "C" {

int Zolt_Init() {
    if (s_initialized) return 0;
    Zolt::Solliter::LogInit("Zolt_Init called");
    s_initialized = true;
    return 0;
}

int Zolt_AutoStart() {
    Zolt::Solliter::LogInit("Zolt_AutoStart called");

    // Initialize process-specific logging
    wchar_t processPath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, processPath, MAX_PATH);
    std::wstring processName = fs::path(processPath).stem().wstring();
    Zolt::Solliter::InitProcessMirror(processName);

    Zolt::Solliter::Summary("Zolt-Dump v%s (%s) starting in '%ls'",
        ZOLT_VERSION_STRING, ZOLT_GIT_SHORT, processName.c_str());

    // Run Frieren scan (discovers all Godot globals)
    Zolt::Frieren::EnginePointers ptrs;
    bool success = Zolt::Frieren::FindAll(ptrs);

    // Store results for C ABI getters
    s_objectDBAddr  = ptrs.ObjectDB;
    s_classDBAddr   = ptrs.ClassDB;
    s_sceneTreeAddr = ptrs.SceneTree;
    s_engineAddr    = ptrs.Engine;
    s_godotVersion  = ptrs.godotVersion;

    // Initialize subsystems with discovered addresses
    if (s_objectDBAddr)  Zolt::Ausetiese::Init(s_objectDBAddr);
    if (s_classDBAddr)   Zolt::DiAgolze::Init(s_classDBAddr);
    if (s_sceneTreeAddr) Zolt::Serie::Init(s_sceneTreeAddr);

    // TODO: Start Fern pipe server (Phase 4)
    // Zolt::Fern::Start();

    s_initialized = true;

    Zolt::Solliter::Summary("Zolt_AutoStart %s (ObjDB=%p ClassDB=%p STree=%p Engine=%p)",
        success ? "succeeded" : "partial",
        reinterpret_cast<void*>(s_objectDBAddr),
        reinterpret_cast<void*>(s_classDBAddr),
        reinterpret_cast<void*>(s_sceneTreeAddr),
        reinterpret_cast<void*>(s_engineAddr));

    return success ? 0 : 1;
}

void Zolt_Shutdown() {
    Zolt::Solliter::LogInit("Zolt_Shutdown called");
    Zolt::Fern::Stop();
    Zolt::Solliter::Shutdown();
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

} // extern "C"

namespace Zolt::Stark {

int AutoStart() {
    return Zolt_AutoStart();
}

} // namespace Zolt::Stark
