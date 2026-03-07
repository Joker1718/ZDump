#pragma once

// Himmel — 欣梅爾 (Hero — HintCache)
// Persists AOB pattern match results keyed by PE hash.
// Allows fast re-scan by remembering which pattern matched
// for a given game executable, avoiding full AOB scans on reload.

#include <cstdint>
#include <string>

namespace Zolt::Himmel {

// Load hint cache from disk (%LOCALAPPDATA%\ZoltDump\hints.dat)
void Load();

// Save hint cache to disk
void Save();

// Get the stored pattern ID for a given PE hash
// Returns empty string if no hint is stored
std::string GetHint(uint64_t peHash);

// Set/update the pattern ID for a given PE hash
void SetHint(uint64_t peHash, const char* patternId);

} // namespace Zolt::Himmel
