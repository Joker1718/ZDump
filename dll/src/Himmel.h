#pragma once

// Himmel — 欣梅爾 (Hero — HintCache)
// Persists AOB pattern match results keyed by PE hash.
// File: %LOCALAPPDATA%\ZoltDump\ZoltDump.{ComputerName}.json
// Format: JSON (nlohmann) - shared between DLL and UI.
// Ported from UE5CEDumper HintCache with adaptations for Godot targets.

#include <cstdint>
#include <string>
#include <unordered_map>

// Forward declare to avoid pulling Frieren.h
namespace Zolt::Frieren { struct EnginePointers; }

// Forward declare Metode types for TypeLayout cache
namespace Zolt::Metode { struct TypeLayout; struct TypeSummary; }

// Forward declare Diagoldze types for PropertyList cache
namespace Zolt::Diagoldze { struct PropertyInfo; }

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

// Cache statistics for diagnostics
struct CacheStats {
    int totalEntries    = 0;  // Total game entries in cache
    int prunedEntries   = 0;  // Entries removed during last prune
    int hintHits        = 0;  // Hints that led to successful scan (cumulative, this session)
    int hintMisses      = 0;  // Hints that failed, required fallback (cumulative, this session)
    std::string cacheFilePath;  // Resolved cache file path
};

// Default TTL for cache entries (90 days in seconds)
constexpr int CACHE_TTL_DAYS = 90;

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

// Record whether a hint led to a successful scan (hit) or required fallback (miss).
// Called by Frieren after each target scan completes.
void RecordHintOutcome(bool hit);

// Remove cache entries older than ttlDays.
// Returns number of pruned entries.
int PruneStaleEntries(int ttlDays = CACHE_TTL_DAYS);

// Clear a specific entry by PE hash.
// Returns true if the entry existed and was removed.
bool ClearEntry(const char* peHash);

// Get current session cache statistics.
CacheStats GetStats();

// ============================================================
// Runtime CLR TypeLayout cache (session-scoped, in-memory only)
// Caches Metode::GetTypeLayout results to avoid repeated JSON
// round-trips to the managed ZoltReflector bridge.
// ============================================================

// Look up a cached TypeLayout by fully qualified type name.
// Returns pointer to cached layout, or nullptr on miss.
const Zolt::Metode::TypeLayout* LookupTypeLayout(const std::string& typeName);

// Store a TypeLayout in the cache.
void StoreTypeLayout(const std::string& typeName, const Zolt::Metode::TypeLayout& layout);

// Get TypeLayout with automatic cache: returns cached result or
// calls Metode::GetTypeLayout, caches, and returns.
// Returns empty TypeLayout (fields.empty()) if Metode is not ready or type not found.
Zolt::Metode::TypeLayout GetTypeLayoutCached(const std::string& typeName);

// Look up a cached MethodTable → TypeName mapping.
// Returns empty string on miss.
const std::string& LookupMTTypeName(uint64_t methodTable);

// Build/rebuild the MethodTable → TypeName cache from Metode::EnumerateTypes.
// Called once lazily; subsequent calls are no-ops.
void EnsureMTCache();

// Clear all runtime CLR caches (TypeLayout + MT map).
void ClearCLRCache();

// ============================================================
// Runtime Godot PropertyList cache (session-scoped, in-memory only)
// Caches Diagoldze::GetPropertyList results. Class property lists
// are determined by the ClassDB registry and never change at runtime.
// ============================================================

// Get PropertyList with automatic cache: returns cached result or
// calls Diagoldze::GetPropertyList, caches, and returns.
const std::vector<Zolt::Diagoldze::PropertyInfo>& GetPropertyListCached(const char* className);

// Clear Godot property list cache.
void ClearPropertyListCache();

} // namespace Zolt::Himmel
