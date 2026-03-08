#pragma once

// Solliter — 索莉緹爾 (Mana Concealment — Logger)
// 5-category per-process logging system for Zolt-Dump.
// Categories: Init, Scan, Offsets, Pipe, Walk
// Logs to %LOCALAPPDATA%\ZoltDump\Logs\{ProcessName}\
// Ported from UE5CEDumper Logger with Zolt-Dump adaptations.

#include <Windows.h>
#include <cstdint>
#include <string>

namespace Zolt::Solliter {

// Initialize the logger: enables early buffering.
// Must be called from DllMain before any logging.
bool Init();

// Open category log files in a per-process subfolder.
// Call AFTER Init() and after determining the host process name.
// Creates <logDir>/<processName>/ with 5 category files.
// Flushes any early-buffered lines to the correct files.
void InitProcessMirror(const std::wstring& processName, int maxSubfolders = 20);

// Shutdown: flush and close all category files
void Shutdown();

// Category-aware log functions.
// cat: category tag string (e.g. "SCAN:ObjDB", "PIPE:cmd")
//      empty string "" routes to init.log
void Info(const char* cat, const char* fmt, ...);
void Error(const char* cat, const char* fmt, ...);
void Warn(const char* cat, const char* fmt, ...);
void Debug(const char* cat, const char* fmt, ...);

// Summary level - routed to init.log, no category tag
void Summary(const char* fmt, ...);

// Legacy API (backward compatible, delegates to new functions)
void LogInit(const char* msg);
void LogScan(const char* msg);
void LogOffsets(const char* msg);
void LogPipe(const char* msg);
void LogWalk(const char* msg);

} // namespace Zolt::Solliter

// ============================================================
// Convenience macros (same pattern as UE5CEDumper)
// Define LOG_CAT before including to set default category:
//   #define LOG_CAT "SCAN:ObjDB"
// ============================================================
#ifndef LOG_CAT
#define LOG_CAT ""
#endif

#define LOG_INFO(fmt, ...)    Zolt::Solliter::Info(LOG_CAT, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)   Zolt::Solliter::Error(LOG_CAT, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)    Zolt::Solliter::Warn(LOG_CAT, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...)   Zolt::Solliter::Debug(LOG_CAT, fmt, ##__VA_ARGS__)
#define LOG_SUMMARY(fmt, ...) Zolt::Solliter::Summary(fmt, ##__VA_ARGS__)
