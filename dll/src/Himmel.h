#pragma once

// Himmel — 欣梅爾 (Hero — HintCache)
// Persists AOB pattern match results keyed by PE hash.
// File: %LOCALAPPDATA%\ZoltDump\ZoltDump.{ComputerName}.json
// Format: JSON (nlohmann) - shared between DLL and UI.
// Ported from UE5CEDumper HintCache with adaptations for Godot targets.

#include <cstdint>
#include <string>

// Forward declare to avoid pulling Frieren.h
namespace Zolt::Frieren { struct EnginePointers; }

namespace Zolt::Himmel {

// Per-target hint: pattern ID to try first (empty = no hint).
struct ScanHints {
    std::string objectDBPatternId;
    std::string classDBPatternId;
    std::string sceneTreePatternId;
    std::string enginePatternId;

    // Cached Godot version from previous scan (empty = no cached version)
    std::string godotVersion;
    bool        hasVersionHint = false;
};

// Compute PE hash for the main module: TimeDateStamp(8hex) + SizeOfImage(8hex)
// Output: 16-char hex string + null terminator (bufSize must be >= 17)
// Returns false if PE headers are unreadable
bool ComputePEHash(char* out, size_t bufSize);

// Load hints for a given PE hash from the cache file.
// Returns empty strings if file doesn't exist or PE hash not found.
ScanHints LoadHints(const char* peHash);

// Save scan results to the cache file.
// Reads existing file, upserts the record for peHash, writes atomically.
void SaveResults(const char* peHash,
                 const Zolt::Frieren::EnginePointers& ptrs,
                 const char* processName);

} // namespace Zolt::Himmel
