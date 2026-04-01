// Genau — 葛納烏 (Combat Examiner — Runtime Diagnostics)
// Runtime assertion guards and diagnostic probes.
// All events are logged via Solitaer with category "GENAU".

#include "Genau.h"
#include "Solitaer.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>

namespace Zolt::Genau {

// ============================================================
// Session-scoped counters (reset each DLL load)
// ============================================================
static std::atomic<int> s_infoCount{0};
static std::atomic<int> s_warningCount{0};
static std::atomic<int> s_fatalCount{0};

// ============================================================
// Report implementation
// ============================================================

void Report(Severity severity, const char* expr, const char* file,
            int line, const char* fmt, ...)
{
    // Format the user message
    char msgBuf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    va_end(args);

    // Extract just the filename from the full path
    const char* shortFile = file;
    if (file) {
        const char* lastSlash = strrchr(file, '\\');
        if (!lastSlash) lastSlash = strrchr(file, '/');
        if (lastSlash) shortFile = lastSlash + 1;
    }

    // Update counters
    switch (severity) {
    case Severity::Info:
        s_infoCount.fetch_add(1, std::memory_order_relaxed);
        if (expr) {
            Solitaer::Info("GENAU", "[INFO] %s:%d — %s (expr: %s)",
                           shortFile, line, msgBuf, expr);
        } else {
            Solitaer::Info("GENAU", "[INFO] %s:%d — %s",
                           shortFile, line, msgBuf);
        }
        break;

    case Severity::Warning:
        s_warningCount.fetch_add(1, std::memory_order_relaxed);
        Solitaer::Warn("GENAU", "[WARN] %s:%d — FAILED: %s — %s",
                        shortFile, line, expr ? expr : "(check)", msgBuf);
        break;

    case Severity::Fatal:
        s_fatalCount.fetch_add(1, std::memory_order_relaxed);
        Solitaer::Error("GENAU", "[FATAL] %s:%d — ASSERT FAILED: %s — %s",
                         shortFile, line, expr ? expr : "(assert)", msgBuf);
        break;
    }
}

// ============================================================
// Statistics
// ============================================================

DiagStats GetStats()
{
    DiagStats stats;
    stats.infoCount    = s_infoCount.load(std::memory_order_relaxed);
    stats.warningCount = s_warningCount.load(std::memory_order_relaxed);
    stats.fatalCount   = s_fatalCount.load(std::memory_order_relaxed);
    return stats;
}

void ResetStats()
{
    s_infoCount.store(0, std::memory_order_relaxed);
    s_warningCount.store(0, std::memory_order_relaxed);
    s_fatalCount.store(0, std::memory_order_relaxed);
}

} // namespace Zolt::Genau
