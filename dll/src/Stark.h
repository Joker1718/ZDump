#pragma once

// Stark — 修塔爾克 (Brave Warrior — Export API)
// C ABI export functions for CE Lua and external callers.
// All exported functions are prefixed with Zolt_ per Grimoire convention.

#include <cstdint>

// All exports use C linkage for compatibility with CE Lua loadLibrary/callFunction
extern "C" {

// Initialize the dumper (called by CE Lua or proxy DLL)
// Returns 0 on success, non-zero on failure
__declspec(dllexport) int Zolt_Init();

// Auto-start: scan for globals, start pipe server
// Returns 0 on success, non-zero on failure
__declspec(dllexport) int Zolt_AutoStart();

// Shut down all subsystems (pipe server, threads, etc.)
__declspec(dllexport) void Zolt_Shutdown();

// Get version string (e.g. "1.0.0.42")
__declspec(dllexport) const char* Zolt_GetVersion();

// Get the ObjectDB address (0 if not found)
__declspec(dllexport) uint64_t Zolt_GetObjectDBAddr();

// Get the ClassDB address (0 if not found)
__declspec(dllexport) uint64_t Zolt_GetClassDBAddr();

// Get the SceneTree address (0 if not found)
__declspec(dllexport) uint64_t Zolt_GetSceneTreeAddr();

// Get the Engine address (0 if not found)
__declspec(dllexport) uint64_t Zolt_GetEngineAddr();

// Get the detected Godot version string
__declspec(dllexport) const char* Zolt_GetGodotVersion();

// Get object count from ObjectDB
__declspec(dllexport) int Zolt_GetObjectCount();

} // extern "C"

// Internal C++ interface used by dllmain
namespace Zolt::Stark {

int AutoStart();

} // namespace Zolt::Stark
