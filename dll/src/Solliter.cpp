// Solliter — 索莉緹爾 (Mana Concealment — Logger)
// 5-category per-process logging system for Zolt-Dump.

#include "Solliter.h"
#include <cstdio>
#include <ctime>

namespace Zolt::Solliter {

static HMODULE s_hModule = nullptr;
static bool    s_initialized = false;

// TODO: Implement log directory creation under %LOCALAPPDATA%\ZoltDump\Logs\<pid>\
// TODO: Implement 4-file rotation with 8MB max per file
// TODO: Implement per-category file handles (init.log, scan.log, offsets.log, pipe.log, walk.log)

static void WriteLog(const char* /*category*/, const char* msg) {
    // TODO: Write to category-specific log file with timestamp
    // Stub: output to debug console
    OutputDebugStringA("[Zolt] ");
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}

void Init(HMODULE hModule) {
    if (s_initialized) return;
    s_hModule = hModule;
    // TODO: Create log directory structure
    // TODO: Open per-category log files
    s_initialized = true;
    LogInit("Solliter logger initialized");
}

void LogInit(const char* msg) {
    WriteLog("init", msg);
}

void LogScan(const char* msg) {
    WriteLog("scan", msg);
}

void LogOffsets(const char* msg) {
    WriteLog("offsets", msg);
}

void LogPipe(const char* msg) {
    WriteLog("pipe", msg);
}

void LogWalk(const char* msg) {
    WriteLog("walk", msg);
}

} // namespace Zolt::Solliter
