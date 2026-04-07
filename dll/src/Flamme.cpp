/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
// Flamme — 弗蘭梅 (Great Mage Master — StringName Resolver)
// Resolves Godot StringName pointers to readable UTF-8 strings.
//
// StringName _Data layout varies by version:
//
// Godot 3.x:
//   _Data { SafeRefCount(4B), pad(4B), const char* cname(8B @+0x08),
//           String name(CowData<wchar_t> on Win @+0x10), int idx, uint32_t hash, prev, next }
//
// Godot 4.0-4.4:
//   _Data { SafeRefCount(4B), SafeNumeric<u32>(4B), const char* cname(8B @+0x08),
//           String name(CowData<char32_t> @+0x10), int idx, uint32_t hash, prev, next }
//
// Godot 4.5+:
//   _Data { SafeRefCount(4B), SafeNumeric<u32>(4B), String name(CowData<char32_t> @+0x08),
//           uint32_t hash, prev, next }  — cname REMOVED, idx REMOVED
//
// Resolution strategy (version-adaptive):
//   1. Try cname at SDATA_CNAME — read as ASCII const char* (3.x + 4.0-4.4)
//   2. Try name at SDATA_CNAME as CowData<char32_t> via ReadChar32String (4.5+)
//   3. Try name at SDATA_NAME — read CowData<char32_t> via ReadGodotString (4.0-4.4)
//   4. Try name at SDATA_NAME as wchar_t CowData (3.x on Windows)

#include "Flamme.h"
#include "Zoltraak.h"
#include "Solitaer.h"
#include "Grimoire.h"

#include <atomic>

namespace Zolt::Flamme {

// Helper: check if a string looks like a valid identifier (ASCII printable, reasonable length)
static bool IsPlausibleName(const std::string& s) {
    if (s.empty() || s.size() > 256) return false;
    for (char c : s) {
        // Allow printable ASCII + common symbols used in Godot names
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

// Read a Godot 3.x String (CowData<wchar_t> on Windows, UTF-16) and convert to UTF-8.
// cowDataPtr is the CowData._ptr value (pointer to wchar_t[] data).
// CowData stores size at _ptr[-1] as int32_t (in element count, not bytes).
static std::string ReadGodot3String(uint64_t cowDataPtr) {
    if (!cowDataPtr || !Zoltraak::IsReadable(cowDataPtr)) return "";

    // CowData<wchar_t>._ptr points to the data. Size is at _ptr - 4 (one int32 before data).
    int32_t size = Zoltraak::ReadSafe<int32_t>(cowDataPtr - 4, 0);
    if (size <= 0 || size > 4096) return "";

    // Read wchar_t data (2 bytes per char on Windows)
    std::vector<uint16_t> wbuf(size);
    Zoltraak::ReadSafe(cowDataPtr, wbuf.data(), size * sizeof(uint16_t));

    // Convert UTF-16 to UTF-8
    std::string result;
    result.reserve(size);
    for (int i = 0; i < size; ++i) {
        uint16_t wc = wbuf[i];
        if (wc == 0) break;
        if (wc < 0x80) {
            result += static_cast<char>(wc);
        } else if (wc < 0x800) {
            result += static_cast<char>(0xC0 | (wc >> 6));
            result += static_cast<char>(0x80 | (wc & 0x3F));
        } else {
            result += static_cast<char>(0xE0 | (wc >> 12));
            result += static_cast<char>(0x80 | ((wc >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (wc & 0x3F));
        }
    }
    return result;
}

// Try to read a string from _Data at the cname offset.
// Handles all versions:
//   - 3.x & 4.0-4.4: cname is const char* (ASCII) → ReadString
//   - 4.5+: cname offset now holds String name (CowData<char32_t>._ptr) → ReadChar32String
//
// IMPORTANT: For 4.5+, the ASCII path MUST be skipped.
// char32_t data like "Object" = {0x4F,0x00,0x00,0x00, 0x62,...} looks like
// single-char ASCII "O" (0x4F followed by 0x00 null terminator).
static std::string TryCname(uint64_t dataPtr) {
    uint64_t ptr = Zoltraak::ReadSafe<uint64_t>(dataPtr + DynOff::SDATA_CNAME, 0);
    if (!ptr || !Zoltraak::IsReadable(ptr)) return "";

    if (GodotVer::IsGodot45Plus()) {
        // 4.5+: cname field removed — offset 0x08 holds CowData<char32_t>._ptr
        // Reading as ASCII gives wrong single-char results, so ONLY try char32
        std::string result = Zoltraak::ReadChar32String(ptr, 256);
        if (IsPlausibleName(result)) return result;
        return "";
    }

    // 3.x & 4.0-4.4: cname is const char* (ASCII pointer into .rdata)
    std::string result = Zoltraak::ReadString(ptr, 256);
    if (IsPlausibleName(result)) {
        // Guard: char32_t data like "Object" = {0x4F,0x00,0x00,0x00, 0x62,...}
        // reads as single-char ASCII "O" (0x4F followed by 0x00 null terminator).
        // When version is unknown and ASCII gives ≤2 chars, also try char32
        // and prefer the longer result — this catches undetected 4.5+ builds.
        if (result.size() <= 2 && GodotVer::Major == 0) {
            std::string c32 = Zoltraak::ReadChar32String(ptr, 256);
            if (IsPlausibleName(c32) && c32.size() > result.size())
                return c32;
        }
        return result;
    }

    // Fallback: try char32_t in case layout differs from expectation
    result = Zoltraak::ReadChar32String(ptr, 256);
    if (IsPlausibleName(result)) return result;

    return "";
}

// Try to read a string from _Data using the name field (CowData<char32_t> or CowData<wchar_t>)
static std::string TryName(uint64_t dataPtr) {
    int nameOffset = DynOff::SDATA_NAME;
    if (nameOffset < 0) nameOffset = 0x10;

    // Read CowData._ptr from _Data + nameOffset
    uint64_t cowPtr = Zoltraak::ReadSafe<uint64_t>(dataPtr + nameOffset, 0);
    if (!cowPtr || !Zoltraak::IsReadable(cowPtr)) return "";

    // Try 1: ReadGodotString — CowData<char32_t> (Godot 4.x)
    std::string result = Zoltraak::ReadGodotString(cowPtr);
    if (IsPlausibleName(result)) return result;

    // Try 2: ReadChar32String — null-terminated char32_t* (direct pointer)
    result = Zoltraak::ReadChar32String(cowPtr, 256);
    if (IsPlausibleName(result)) return result;

    // Try 3: ReadGodot3String — CowData<wchar_t> (Godot 3.x on Windows)
    result = ReadGodot3String(cowPtr);
    if (IsPlausibleName(result)) return result;

    return "";
}

std::string Resolve(uint64_t stringNameAddr) {
    if (!stringNameAddr) return "";

    // Read _Data* from StringName at offset 0x00
    uint64_t dataPtr = Zoltraak::ReadSafe<uint64_t>(
        stringNameAddr + DynOff::STRINGNAME_DATA_PTR, 0);

    if (!dataPtr) return "";  // Null _Data = empty StringName (valid)

    return ResolveAt(dataPtr);
}

std::string ResolveAt(uint64_t dataPtr) {
    if (!dataPtr || !Zoltraak::IsReadable(dataPtr)) return "";

    // Strategy 1: Try cname offset — handles ASCII (3.x/4.0-4.4) and char32 (4.5+)
    std::string result = TryCname(dataPtr);
    if (!result.empty()) return result;

    // Strategy 2: Try name offset — CowData<char32_t> (4.x) or CowData<wchar_t> (3.x)
    result = TryName(dataPtr);
    if (IsPlausibleName(result)) return result;

    return "";
}

void ProbeLayout(uint64_t sampleDataPtr) {
    // Prevent double-probing
    if (DynOff::bStringNameProbed.load(std::memory_order_acquire)) return;

    if (!sampleDataPtr || !Zoltraak::IsReadable(sampleDataPtr)) {
        Solitaer::Debug("FLAMME", "ProbeLayout: no valid sample _Data pointer, using defaults");
        DynOff::bStringNameProbed.store(true, std::memory_order_release);
        return;
    }

    Solitaer::Info("FLAMME", "ProbeLayout: probing _Data at %p (Godot %u.%u)",
        reinterpret_cast<void*>(sampleDataPtr), GodotVer::Major, GodotVer::Minor);

    // If version is known, set expected offsets
    if (GodotVer::IsGodot45Plus()) {
        // 4.5+: no cname, name at +0x08
        DynOff::SDATA_CNAME = 0x08; // Will read String name here (char32 path in TryCname)
        DynOff::SDATA_NAME  = 0x08; // Same offset, String name
        Solitaer::Info("FLAMME", "ProbeLayout: Godot 4.5+ detected — name at +0x08, no cname");
    }

    // Try resolving with current offsets
    // Require ≥2 chars — single-char results are likely misreads of char32_t data
    std::string resolved = TryCname(sampleDataPtr);
    if (!resolved.empty() && resolved.size() >= 2) {
        Solitaer::Info("FLAMME", "ProbeLayout: resolved via cname at +0x%X: '%s'",
            DynOff::SDATA_CNAME, resolved.c_str());
        DynOff::bStringNameProbed.store(true, std::memory_order_release);
        return;
    }
    if (!resolved.empty()) {
        Solitaer::Debug("FLAMME", "ProbeLayout: cname at +0x%X gave short result '%s', continuing probe",
            DynOff::SDATA_CNAME, resolved.c_str());
    }

    resolved = TryName(sampleDataPtr);
    if (IsPlausibleName(resolved) && resolved.size() >= 2) {
        Solitaer::Info("FLAMME", "ProbeLayout: resolved via name at +0x%X: '%s'",
            DynOff::SDATA_NAME, resolved.c_str());
        DynOff::bStringNameProbed.store(true, std::memory_order_release);
        return;
    }

    // Fallback: scan offsets 0x04..0x28 trying all reading methods
    static const int altOffsets[] = { 0x04, 0x08, 0x10, 0x18, 0x20, 0x28 };
    for (int offset : altOffsets) {
        uint64_t ptr = Zoltraak::ReadSafe<uint64_t>(sampleDataPtr + offset, 0);
        if (!ptr || !Zoltraak::IsReadable(ptr)) continue;

        // Try as ASCII const char* (cname in 3.x/4.0-4.4)
        std::string s = Zoltraak::ReadString(ptr, 256);
        if (IsPlausibleName(s) && s.size() >= 2) {
            DynOff::SDATA_CNAME = offset;
            Solitaer::Info("FLAMME", "ProbeLayout: found ASCII cname at +0x%X: '%s'",
                offset, s.c_str());
            DynOff::bStringNameProbed.store(true, std::memory_order_release);
            return;
        }

        // Try as null-terminated char32_t* (CowData._ptr for 4.5+, or raw char32 pointer)
        s = Zoltraak::ReadChar32String(ptr, 256);
        if (IsPlausibleName(s) && s.size() >= 2) {
            DynOff::SDATA_CNAME = offset;
            Solitaer::Info("FLAMME", "ProbeLayout: found char32 string at +0x%X: '%s'",
                offset, s.c_str());
            // char32 at +0x08 strongly indicates Godot 4.5+ (cname removed, name at +0x08)
            if (offset == 0x08 && GodotVer::Major == 0) {
                GodotVer::Major = 4;
                GodotVer::Minor = 5;
                DynOff::SDATA_NAME = 0x08;
                Solitaer::Info("FLAMME", "ProbeLayout: auto-detected Godot 4.5+ (char32 name at +0x08)");
            }
            DynOff::bStringNameProbed.store(true, std::memory_order_release);
            return;
        }

        // Try as CowData<char32_t> (name field in 4.x)
        s = Zoltraak::ReadGodotString(ptr);
        if (IsPlausibleName(s) && s.size() >= 2) {
            DynOff::SDATA_NAME = offset;
            Solitaer::Info("FLAMME", "ProbeLayout: found CowData name at +0x%X: '%s'",
                offset, s.c_str());
            DynOff::bStringNameProbed.store(true, std::memory_order_release);
            return;
        }

        // Try as CowData<wchar_t> (name field in 3.x on Windows)
        s = ReadGodot3String(ptr);
        if (IsPlausibleName(s) && s.size() >= 2) {
            DynOff::SDATA_NAME = offset;
            Solitaer::Info("FLAMME", "ProbeLayout: found wchar_t name at +0x%X: '%s'",
                offset, s.c_str());
            DynOff::bStringNameProbed.store(true, std::memory_order_release);
            return;
        }
    }

    Solitaer::Warn("FLAMME", "ProbeLayout: no valid string found in sample _Data at %p",
        reinterpret_cast<void*>(sampleDataPtr));

    // Hex dump for debugging
    uint8_t buf[48] = {};
    Zoltraak::ReadSafe(sampleDataPtr, buf, sizeof(buf));
    for (int i = 0; i < 48; i += 16) {
        char line[200];
        snprintf(line, sizeof(line),
            "  +0x%02X: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X",
            i,
            buf[i+0], buf[i+1], buf[i+2], buf[i+3],
            buf[i+4], buf[i+5], buf[i+6], buf[i+7],
            buf[i+8], buf[i+9], buf[i+10], buf[i+11],
            buf[i+12], buf[i+13], buf[i+14], buf[i+15]);
        Solitaer::Warn("FLAMME", "%s", line);
    }

    DynOff::bStringNameProbed.store(true, std::memory_order_release);
}

void LogDiagnostics(uint64_t sampleAddr) {
    static std::atomic<bool> s_done{false};
    if (s_done.exchange(true)) return;  // One-shot

    Solitaer::Info("FLAMME", "=== Flamme Diagnostics ===");
    Solitaer::Info("FLAMME", "Godot version: %u.%u.%u", GodotVer::Major, GodotVer::Minor, GodotVer::Patch);
    Solitaer::Info("FLAMME", "SDATA_CNAME offset: 0x%X", DynOff::SDATA_CNAME);
    Solitaer::Info("FLAMME", "SDATA_NAME  offset: 0x%X", DynOff::SDATA_NAME);
    Solitaer::Info("FLAMME", "Probed: %s", DynOff::bStringNameProbed.load() ? "yes" : "no");

    if (!sampleAddr) {
        Solitaer::Info("FLAMME", "No sample address provided for diagnostics");
        return;
    }

    // Try to resolve as a StringName
    std::string resolved = Resolve(sampleAddr);
    Solitaer::Info("FLAMME", "Sample StringName at %p: '%s'",
        reinterpret_cast<void*>(sampleAddr), resolved.c_str());

    // Read _Data pointer and hex dump
    uint64_t dataPtr = Zoltraak::ReadSafe<uint64_t>(sampleAddr, 0);
    if (dataPtr && Zoltraak::IsReadable(dataPtr)) {
        Solitaer::Info("FLAMME", "  _Data at %p:", reinterpret_cast<void*>(dataPtr));

        // Hex dump first 48 bytes of _Data (covers all known fields)
        uint8_t buf[48] = {};
        Zoltraak::ReadSafe(dataPtr, buf, sizeof(buf));

        for (int i = 0; i < 48; i += 16) {
            char line[200];
            snprintf(line, sizeof(line),
                "  +0x%02X: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X",
                i,
                buf[i+0], buf[i+1], buf[i+2], buf[i+3],
                buf[i+4], buf[i+5], buf[i+6], buf[i+7],
                buf[i+8], buf[i+9], buf[i+10], buf[i+11],
                buf[i+12], buf[i+13], buf[i+14], buf[i+15]);
            Solitaer::Info("FLAMME", "%s", line);
        }

        // Show what we find at each relevant offset
        uint64_t ptr08 = Zoltraak::ReadSafe<uint64_t>(dataPtr + 0x08, 0);
        uint64_t ptr10 = Zoltraak::ReadSafe<uint64_t>(dataPtr + 0x10, 0);

        Solitaer::Info("FLAMME", "  +0x08: ptr=%p readable=%s ascii='%s' char32='%s'",
            reinterpret_cast<void*>(ptr08),
            (ptr08 && Zoltraak::IsReadable(ptr08)) ? "yes" : "no",
            (ptr08 && Zoltraak::IsReadable(ptr08)) ?
                Zoltraak::ReadString(ptr08, 64).c_str() : "",
            (ptr08 && Zoltraak::IsReadable(ptr08)) ?
                Zoltraak::ReadChar32String(ptr08, 64).c_str() : "");

        Solitaer::Info("FLAMME", "  +0x10: ptr=%p readable=%s godotStr='%s' char32='%s'",
            reinterpret_cast<void*>(ptr10),
            (ptr10 && Zoltraak::IsReadable(ptr10)) ? "yes" : "no",
            (ptr10 && Zoltraak::IsReadable(ptr10)) ?
                Zoltraak::ReadGodotString(ptr10).c_str() : "",
            (ptr10 && Zoltraak::IsReadable(ptr10)) ?
                Zoltraak::ReadChar32String(ptr10, 64).c_str() : "");
    }
}

} // namespace Zolt::Flamme
