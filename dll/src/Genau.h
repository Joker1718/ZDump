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

// Genau — 葛納烏 (Combat Examiner — Runtime Diagnostics)
// Runtime assertion guards and diagnostic probes for DLL modules.
// Named after the first-class mage examiner who filters candidates through combat.
// Provides ZOLT_ASSERT / ZOLT_CHECK macros that log via Solitaer and optionally abort.

#include <cstdint>

namespace Zolt::Genau {

// Severity levels for diagnostic events
enum class Severity : uint8_t {
    Info,       // Diagnostic observation (passing note)
    Warning,    // Suspicious but non-fatal (examiner's raised eyebrow)
    Fatal,      // Invariant violated, abort operation (failed the exam)
};

// Report a diagnostic event. Called by ZOLT_ASSERT / ZOLT_CHECK macros.
// Fatal severity triggers an immediate return from the calling function.
// All events are logged via Solitaer with category "GENAU".
void Report(Severity severity, const char* expr, const char* file,
            int line, const char* fmt, ...);

// Get cumulative diagnostic counters for this session (since DLL load).
struct DiagStats {
    int infoCount    = 0;
    int warningCount = 0;
    int fatalCount   = 0;
};
DiagStats GetStats();

// Reset session counters (for testing).
void ResetStats();

} // namespace Zolt::Genau

// ============================================================
// Assertion macros
// ============================================================

// ZOLT_ASSERT(expr, fmt, ...) — Fatal if expr is false. Logs and returns from caller.
// Use for invariants that indicate a programming error or corrupted state.
#define ZOLT_ASSERT(expr, ...) \
    do { \
        if (!(expr)) { \
            Zolt::Genau::Report(Zolt::Genau::Severity::Fatal, \
                #expr, __FILE__, __LINE__, __VA_ARGS__); \
            return; \
        } \
    } while (0)

// ZOLT_ASSERT_RET(expr, retval, fmt, ...) — Fatal if expr is false. Returns retval.
#define ZOLT_ASSERT_RET(expr, retval, ...) \
    do { \
        if (!(expr)) { \
            Zolt::Genau::Report(Zolt::Genau::Severity::Fatal, \
                #expr, __FILE__, __LINE__, __VA_ARGS__); \
            return (retval); \
        } \
    } while (0)

// ZOLT_CHECK(expr, fmt, ...) — Warning if expr is false. Logs but continues execution.
// Use for suspicious conditions that are recoverable.
#define ZOLT_CHECK(expr, ...) \
    do { \
        if (!(expr)) { \
            Zolt::Genau::Report(Zolt::Genau::Severity::Warning, \
                #expr, __FILE__, __LINE__, __VA_ARGS__); \
        } \
    } while (0)

// ZOLT_DIAG(fmt, ...) — Info-level diagnostic note. Always logs.
#define ZOLT_DIAG(...) \
    Zolt::Genau::Report(Zolt::Genau::Severity::Info, \
        nullptr, __FILE__, __LINE__, __VA_ARGS__)
