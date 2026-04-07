/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
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

// ----- Phase 2: Object Enumeration exports -----

// Get max slot index in ObjectDB
__declspec(dllexport) int Zolt_GetSlotMax();

// Get Object* at a specific slot index (0 if empty/invalid)
__declspec(dllexport) uint64_t Zolt_GetObjectByIndex(int index);

// Find an object's slot index by its address (-1 if not found)
__declspec(dllexport) int Zolt_FindObjectByAddress(uint64_t addr);

// Get total number of registered classes
__declspec(dllexport) int Zolt_GetClassCount();

// Get class name for an Object* (returns "" if unknown)
__declspec(dllexport) const char* Zolt_GetClassName(uint64_t objectAddr);

// Get root node address from SceneTree (0 if unavailable)
__declspec(dllexport) uint64_t Zolt_GetRootNode();

// ----- Phase 3: Pipe Server exports -----

// Start the named pipe server (\\.\pipe\ZoltDumpBfx)
__declspec(dllexport) int Zolt_StartPipeServer();

// Stop the named pipe server
__declspec(dllexport) void Zolt_StopPipeServer();

// Check if a UI client is connected to the pipe (returns 1/0)
__declspec(dllexport) int Zolt_IsPipeConnected();

// ----- Phase 4: .NET Managed Object exports -----

// Check if the target game is a .NET Godot game (GodotSharp/coreclr loaded)
// Returns 1 if .NET detected, 0 otherwise
__declspec(dllexport) int Zolt_IsDotNetGame();

// Get the .NET managed object address for a Godot Object.
// Traverses: Object → CSharpInstance → GCHandle → managed object
// Returns managed object pointer (0 if not a .NET object or chain broken)
__declspec(dllexport) uint64_t Zolt_GetManagedObject(uint64_t objectAddr);

// Get the ScriptInstance offset within a Godot Object (probed at runtime).
// Returns offset (e.g. 0x68) or -1 if not yet probed.
__declspec(dllexport) int Zolt_GetScriptInstanceOffset();

} // extern "C"

// Internal C++ interface used by dllmain
namespace Zolt::Stark {

int AutoStart();

} // namespace Zolt::Stark
