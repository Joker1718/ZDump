#pragma once

// Solliter — 索莉緹爾 (Mana Concealment — Logger)
// 5-category per-process logging system for Zolt-Dump.
// Categories: Init, Scan, Offsets, Pipe, Walk
// Logs to %LOCALAPPDATA%\ZoltDump\Logs\<pid>\

#include <Windows.h>
#include <cstdint>
#include <string>

namespace Zolt::Solliter {

// Initialize the logger with the DLL module handle (for path resolution)
void Init(HMODULE hModule);

// Log to the Init category (DLL load, version info, startup)
void LogInit(const char* msg);

// Log to the Scan category (AOB scanning, pattern matching)
void LogScan(const char* msg);

// Log to the Offsets category (offset validation, version detection)
void LogOffsets(const char* msg);

// Log to the Pipe category (named pipe IPC events)
void LogPipe(const char* msg);

// Log to the Walk category (scene tree traversal, object enumeration)
void LogWalk(const char* msg);

} // namespace Zolt::Solliter
