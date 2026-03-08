#pragma once

// Solliter — 索莉緹爾 (Mana Concealment — Logger)
// 5-category per-process logging system for Zolt-Dump.
// Categories: Init, Scan, Offsets, Pipe, Walk
// Logs to %LOCALAPPDATA%\ZoltDump\Logs\{ProcessName}\
// Ported from UE5CEDumper Logger with Zolt-Dump adaptations.

#include <Windows.h>
#include <chrono>
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

// Hex-dump a block of memory to the specified log category.
// Reads from the process address space via Zoltraak::ReadSafe.
// Formats as: "+0xNNNN: XX XX XX XX  XX XX XX XX  |........|"
void HexDump(const char* cat, uint64_t baseAddr, size_t maxBytes = 256);

// Log a message at most once every intervalMs milliseconds for a given key.
// Returns true if the message was logged, false if throttled.
bool Throttle(const char* key, int intervalMs, const char* cat, const char* fmt, ...);

// RAII scoped timer — logs elapsed time on destruction.
struct ScopedTimer {
    const char* cat;
    const char* label;
    std::chrono::steady_clock::time_point start;
    ScopedTimer(const char* c, const char* l)
        : cat(c), label(l), start(std::chrono::steady_clock::now()) {}
    ~ScopedTimer();
};

// Legacy API (backward compatible, delegates to new functions)
void LogInit(const char* msg);
void LogScan(const char* msg);
void LogOffsets(const char* msg);
void LogPipe(const char* msg);
void LogWalk(const char* msg);

} // namespace Zolt::Solliter

// Timing macro: creates a scoped timer that logs on scope exit
#define LOG_TIMED(cat, label) \
    Zolt::Solliter::ScopedTimer _logTimer_##__LINE__(cat, label)

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
