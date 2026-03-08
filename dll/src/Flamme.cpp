// Flamme — 芙拉梅 (Great Mage Master — StringName Resolver)
// Resolves Godot StringName pointers to readable UTF-8 strings.
//
// StringName layout (Godot 4.0-4.4, stable across versions):
//   StringName { _Data* _data; }            // 8 bytes
//   _Data {
//     SafeRefCount refcount;                // +0x00 (4 bytes, atomic u32)
//     SafeNumeric<u32> static_count;        // +0x04 (4 bytes)
//     const char* cname;                    // +0x08 (8 bytes — raw ASCII ptr to .rdata, for static names)
//     String name;                          // +0x10 (8 bytes — CowData<char32_t>._ptr, for dynamic names)
//     int idx;                              // +0x18 (4 bytes)
//     uint32_t hash;                        // +0x1C (4 bytes)
//     _Data* prev, *next;                   // +0x20, +0x28 (hash bucket chain)
//   }
//
// Resolution strategy:
//   1. Try cname at +0x08 — if non-null, read as ASCII string (fast path)
//   2. Try name at +0x10 — if non-null, read CowData<char32_t> via ReadGodotString

#include "Flamme.h"
#include "Zoltraak.h"
#include "Solliter.h"
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

// Try to read a string from _Data using the cname field (const char32_t* in .rdata)
static std::string TryCname(uint64_t dataPtr) {
    uint64_t cnamePtr = Zoltraak::ReadSafe<uint64_t>(dataPtr + DynOff::SDATA_CNAME, 0);
    if (!cnamePtr || !Zoltraak::IsReadable(cnamePtr)) return "";

    // cname is const char32_t* (NOT const char*!) pointing to null-terminated
    // UTF-32 data in .rdata section
    std::string result = Zoltraak::ReadChar32String(cnamePtr, 256);
    if (IsPlausibleName(result)) return result;
    return "";
}

// Try to read a string from _Data using the name field (CowData<char32_t>)
static std::string TryName(uint64_t dataPtr) {
    int nameOffset = DynOff::SDATA_NAME;
    if (nameOffset < 0) nameOffset = 0x10;

    // Read CowData<char32_t>._ptr from _Data + nameOffset
    uint64_t cowPtr = Zoltraak::ReadSafe<uint64_t>(dataPtr + nameOffset, 0);
    if (!cowPtr || !Zoltraak::IsReadable(cowPtr)) return "";

    // ReadGodotString handles both old and new CowData layouts
    return Zoltraak::ReadGodotString(cowPtr);
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

    // Strategy 1: Try cname (raw ASCII pointer) — fast path for static names
    std::string result = TryCname(dataPtr);
    if (!result.empty()) return result;

    // Strategy 2: Try name (CowData<char32_t>) — for dynamic names
    result = TryName(dataPtr);
    if (IsPlausibleName(result)) return result;

    return "";
}

void ProbeLayout(uint64_t sampleDataPtr) {
    // Prevent double-probing
    if (DynOff::bStringNameProbed.load(std::memory_order_acquire)) return;

    if (!sampleDataPtr || !Zoltraak::IsReadable(sampleDataPtr)) {
        Solliter::Debug("FLAMME", "ProbeLayout: no valid sample _Data pointer, using defaults");
        DynOff::bStringNameProbed.store(true, std::memory_order_release);
        return;
    }

    // The _Data layout is stable across Godot 4.0-4.4:
    //   +0x08 = cname (const char*)
    //   +0x10 = name (String/CowData<char32_t>)
    // Try cname first, then name. These are the known offsets.

    // Try cname at default offset (0x08)
    std::string cname = TryCname(sampleDataPtr);
    if (!cname.empty()) {
        Solliter::Info("FLAMME", "ProbeLayout: resolved via cname at +0x%X: '%s'",
            DynOff::SDATA_CNAME, cname.c_str());
        DynOff::bStringNameProbed.store(true, std::memory_order_release);
        return;
    }

    // Try name at default offset (0x10)
    std::string name = TryName(sampleDataPtr);
    if (IsPlausibleName(name)) {
        Solliter::Info("FLAMME", "ProbeLayout: resolved via name at +0x%X: '%s'",
            DynOff::SDATA_NAME, name.c_str());
        DynOff::bStringNameProbed.store(true, std::memory_order_release);
        return;
    }

    // Fallback: try alternate offsets for both cname (char32_t*) and name (CowData)
    static const int altOffsets[] = { 0x08, 0x10, 0x18, 0x20 };
    for (int offset : altOffsets) {
        uint64_t ptr = Zoltraak::ReadSafe<uint64_t>(sampleDataPtr + offset, 0);
        if (!ptr || !Zoltraak::IsReadable(ptr)) continue;

        // Try as raw char32_t* (null-terminated)
        std::string s = Zoltraak::ReadChar32String(ptr, 256);
        if (IsPlausibleName(s) && s.size() >= 2) {
            DynOff::SDATA_CNAME = offset;
            Solliter::Info("FLAMME", "ProbeLayout: found cname at alternate +0x%X: '%s'",
                offset, s.c_str());
            DynOff::bStringNameProbed.store(true, std::memory_order_release);
            return;
        }

        // Try as CowData<char32_t>
        s = Zoltraak::ReadGodotString(ptr);
        if (IsPlausibleName(s) && s.size() >= 2) {
            DynOff::SDATA_NAME = offset;
            Solliter::Info("FLAMME", "ProbeLayout: found name at alternate +0x%X: '%s'",
                offset, s.c_str());
            DynOff::bStringNameProbed.store(true, std::memory_order_release);
            return;
        }
    }

    Solliter::Warn("FLAMME", "ProbeLayout: no valid string found in sample _Data at %p",
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
        Solliter::Warn("FLAMME", "%s", line);
    }

    DynOff::bStringNameProbed.store(true, std::memory_order_release);
}

void LogDiagnostics(uint64_t sampleAddr) {
    static std::atomic<bool> s_done{false};
    if (s_done.exchange(true)) return;  // One-shot

    Solliter::Info("FLAMME", "=== Flamme Diagnostics ===");
    Solliter::Info("FLAMME", "SDATA_CNAME offset: 0x%X", DynOff::SDATA_CNAME);
    Solliter::Info("FLAMME", "SDATA_NAME  offset: 0x%X", DynOff::SDATA_NAME);
    Solliter::Info("FLAMME", "Probed: %s", DynOff::bStringNameProbed.load() ? "yes" : "no");

    if (!sampleAddr) {
        Solliter::Info("FLAMME", "No sample address provided for diagnostics");
        return;
    }

    // Try to resolve as a StringName
    std::string resolved = Resolve(sampleAddr);
    Solliter::Info("FLAMME", "Sample StringName at %p: '%s'",
        reinterpret_cast<void*>(sampleAddr), resolved.c_str());

    // Read _Data pointer and hex dump
    uint64_t dataPtr = Zoltraak::ReadSafe<uint64_t>(sampleAddr, 0);
    if (dataPtr && Zoltraak::IsReadable(dataPtr)) {
        Solliter::Info("FLAMME", "  _Data at %p:", reinterpret_cast<void*>(dataPtr));

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
            Solliter::Info("FLAMME", "%s", line);
        }

        // Show what we find at each relevant offset
        uint64_t cnamePtr = Zoltraak::ReadSafe<uint64_t>(dataPtr + 0x08, 0);
        uint64_t namePtr  = Zoltraak::ReadSafe<uint64_t>(dataPtr + 0x10, 0);

        Solliter::Info("FLAMME", "  +0x08 cname: ptr=%p readable=%s str='%s'",
            reinterpret_cast<void*>(cnamePtr),
            (cnamePtr && Zoltraak::IsReadable(cnamePtr)) ? "yes" : "no",
            (cnamePtr && Zoltraak::IsReadable(cnamePtr)) ?
                Zoltraak::ReadChar32String(cnamePtr, 64).c_str() : "");

        Solliter::Info("FLAMME", "  +0x10 name:  ptr=%p readable=%s str='%s'",
            reinterpret_cast<void*>(namePtr),
            (namePtr && Zoltraak::IsReadable(namePtr)) ? "yes" : "no",
            (namePtr && Zoltraak::IsReadable(namePtr)) ?
                Zoltraak::ReadGodotString(namePtr).c_str() : "");
    }
}

} // namespace Zolt::Flamme
