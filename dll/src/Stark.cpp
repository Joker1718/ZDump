// Stark — 修塔爾克 (Brave Warrior — Export API)
// C ABI export functions for CE Lua and external callers.

#include "Stark.h"
#include "Frieren.h"
#include "Ausetiese.h"
#include "DiAgolze.h"
#include "Serie.h"
#include "Fern.h"
#include "Solliter.h"

// Global state
static uint64_t    s_objectDBAddr  = 0;
static uint64_t    s_classDBAddr   = 0;
static uint64_t    s_sceneTreeAddr = 0;
static uint64_t    s_engineAddr    = 0;
static std::string s_godotVersion;
static bool        s_initialized   = false;

// Version string buffer (static storage for C ABI return)
static char s_versionBuf[64] = "0.0.0.0";

extern "C" {

int Zolt_Init() {
    if (s_initialized) return 0;
    // TODO: Full initialization sequence
    Zolt::Solliter::LogInit("Zolt_Init called");
    s_initialized = true;
    return 0;
}

int Zolt_AutoStart() {
    // TODO: Run Frieren scans, initialize subsystems, start pipe
    Zolt::Solliter::LogInit("Zolt_AutoStart called");

    s_objectDBAddr  = Zolt::Frieren::FindObjectDB();
    s_classDBAddr   = Zolt::Frieren::FindClassDB();
    s_sceneTreeAddr = Zolt::Frieren::FindSceneTree();
    s_engineAddr    = Zolt::Frieren::FindEngine();

    s_godotVersion = Zolt::Frieren::DetectVersion();
    Zolt::Frieren::ValidateAndFixOffsets();

    if (s_objectDBAddr)  Zolt::Ausetiese::Init(s_objectDBAddr);
    if (s_classDBAddr)   Zolt::DiAgolze::Init(s_classDBAddr);
    if (s_sceneTreeAddr) Zolt::Serie::Init(s_sceneTreeAddr);

    // TODO: Start Fern pipe server
    // Zolt::Fern::Start();

    return (s_objectDBAddr && s_classDBAddr) ? 0 : 1;
}

void Zolt_Shutdown() {
    // TODO: Stop pipe server, clean up threads
    Zolt::Solliter::LogInit("Zolt_Shutdown called");
    Zolt::Fern::Stop();
    s_initialized = false;
}

const char* Zolt_GetVersion() {
    // TODO: Return actual version from BuildInfo.h
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
