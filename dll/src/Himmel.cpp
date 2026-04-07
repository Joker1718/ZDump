/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
// Himmel — 欣梅爾 (Hero — HintCache)
// Persists AOB pattern match results keyed by PE hash.
// Ported from UE5CEDumper HintCache.cpp with adaptations for Godot targets.
//
// JSON cache file: %LOCALAPPDATA%\ZoltDump\ZoltDump.{COMPUTERNAME}.json
// Key: PE image hash (TimeDateStamp + SizeOfImage = 16 hex chars)
// Values: per-target pattern IDs, Godot version, scan statistics

#define LOG_CAT "HINT"
#include "Himmel.h"
#include "Frieren.h"
#include "Metode.h"
#include "Diagoldze.h"
#include "Zoltraak.h"
#include "Solitaer.h"
#include "Grimoire.h"

#include <Windows.h>
#include <ShlObj.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <chrono>
#include <mutex>
#include <ctime>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Zolt::Himmel {

// ============================================================
// Session-scoped hit/miss counters (reset each DLL load)
// ============================================================
static std::atomic<int> s_hintHits{0};
static std::atomic<int> s_hintMisses{0};

// ============================================================
// Internal helpers
// ============================================================

static fs::path GetCacheFilePath() {
    wchar_t* appdata = nullptr;
    if (!SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appdata))) {
        return {};
    }
    fs::path dir = fs::path(appdata) / Grimoire::LOG_DIR_NAME;
    CoTaskMemFree(appdata);

    // Ensure directory exists
    std::error_code ec;
    fs::create_directories(dir, ec);

    // Build filename: ZoltDump.{COMPUTERNAME}.json
    wchar_t compName[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD compLen = MAX_COMPUTERNAME_LENGTH + 1;
    if (!GetComputerNameW(compName, &compLen)) {
        wcscpy_s(compName, L"UNKNOWN");
    }

    std::wstring filename = std::wstring(L"ZoltDump.") + compName + L".json";
    return dir / filename;
}

static std::string GetUtcTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    struct tm tm_utc {};
    gmtime_s(&tm_utc, &time_t_now);

    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
        tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
        tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
    return buf;
}

static json ReadCacheFile(const fs::path& path) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return json::object();
    }

    try {
        std::ifstream ifs(path);
        if (!ifs.is_open()) return json::object();
        return json::parse(ifs, nullptr, false);
    } catch (...) {
        LOG_WARN("Failed to parse hint cache, starting fresh");
        return json::object();
    }
}

static bool WriteCacheFile(const fs::path& path, const json& data) {
    // Atomic write: temp file + rename
    fs::path tmpPath = path;
    tmpPath += L".tmp";

    try {
        std::ofstream ofs(tmpPath);
        if (!ofs.is_open()) {
            LOG_ERROR("Cannot open temp file for hint cache write");
            return false;
        }
        ofs << data.dump(2);
        ofs.close();

        std::error_code ec;
        fs::rename(tmpPath, path, ec);
        if (ec) {
            // Fallback: copy + delete (cross-volume rename)
            fs::copy_file(tmpPath, path, fs::copy_options::overwrite_existing, ec);
            fs::remove(tmpPath, ec);
        }
        return true;
    } catch (...) {
        LOG_ERROR("Exception writing hint cache");
        std::error_code ec;
        fs::remove(tmpPath, ec);
        return false;
    }
}

// ============================================================
// Public API
// ============================================================

bool ComputePEHash(char* out, size_t bufSize) {
    if (!out || bufSize < 17) return false;
    out[0] = '\0';

    uintptr_t base = static_cast<uintptr_t>(Zoltraak::GetModuleBase(nullptr));
    if (!base) return false;

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(
        base + static_cast<uint32_t>(dos->e_lfanew));
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    snprintf(out, bufSize, "%08X%08X",
        nt->FileHeader.TimeDateStamp,
        nt->OptionalHeader.SizeOfImage);
    return true;
}

ScanHints LoadHints(const char* peHash) {
    ScanHints hints;
    if (!peHash || !peHash[0]) return hints;

    fs::path cachePath = GetCacheFilePath();
    if (cachePath.empty()) return hints;

    json root = ReadCacheFile(cachePath);
    if (root.is_discarded() || !root.contains("games")) return hints;

    auto& games = root["games"];
    if (!games.contains(peHash)) {
        LOG_INFO("No cached hints for PE hash %s", peHash);
        return hints;
    }

    auto& entry = games[peHash];

    // Extract per-target pattern IDs
    auto extractPatternId = [](const json& target) -> std::string {
        if (target.is_object() && target.contains("patternId")) {
            return target["patternId"].get<std::string>();
        }
        return "";
    };

    if (entry.contains("objectDB"))  hints.objectDBPatternId  = extractPatternId(entry["objectDB"]);
    if (entry.contains("classDB"))   hints.classDBPatternId   = extractPatternId(entry["classDB"]);
    if (entry.contains("sceneTree")) hints.sceneTreePatternId = extractPatternId(entry["sceneTree"]);
    if (entry.contains("engine"))    hints.enginePatternId    = extractPatternId(entry["engine"]);

    // Extract cached Godot version
    if (entry.contains("godotVersion") && entry["godotVersion"].is_string()) {
        hints.godotVersion = entry["godotVersion"].get<std::string>();
        hints.hasVersionHint = !hints.godotVersion.empty();
    }

    LOG_INFO("Loaded hints for PE hash %s: ObjDB=%s ClassDB=%s STree=%s Engine=%s ver=%s",
        peHash,
        hints.objectDBPatternId.c_str(),
        hints.classDBPatternId.c_str(),
        hints.sceneTreePatternId.c_str(),
        hints.enginePatternId.c_str(),
        hints.godotVersion.c_str());

    return hints;
}

void SaveResults(const char* peHash,
                 const Zolt::Frieren::EnginePointers& ptrs,
                 const char* processName) {
    if (!peHash || !peHash[0]) return;

    fs::path cachePath = GetCacheFilePath();
    if (cachePath.empty()) return;

    // Read existing cache
    json root = ReadCacheFile(cachePath);
    if (root.is_discarded()) root = json::object();

    // Ensure structure
    if (!root.contains("version")) root["version"] = 1;

    wchar_t compName[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD compLen = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(compName, &compLen)) {
        char compNameA[MAX_COMPUTERNAME_LENGTH + 1] = {};
        WideCharToMultiByte(CP_UTF8, 0, compName, -1, compNameA, sizeof(compNameA), nullptr, nullptr);
        root["machineName"] = compNameA;
    }

    if (!root.contains("games")) root["games"] = json::object();

    // Build per-target result objects
    auto makeTarget = [](uintptr_t addr, const char* method, const char* patternId,
                         int tried) -> json {
        json t;
        t["method"] = method ? method : "not_found";
        t["patternId"] = patternId ? patternId : "";
        t["address"] = addr;
        t["patternsTried"] = tried;
        return t;
    };

    // Upsert the game entry
    json entry;
    if (root["games"].contains(peHash)) {
        entry = root["games"][peHash];
    }

    entry["peHash"] = peHash;
    entry["gameName"] = processName ? processName : "";
    entry["godotVersion"] = ptrs.godotVersion;

    entry["objectDB"]  = makeTarget(ptrs.ObjectDB, ptrs.objectDBMethod, ptrs.objectDBPatternId,
                                     ptrs.objectDBPatternsTried);
    entry["classDB"]   = makeTarget(ptrs.ClassDB, ptrs.classDBMethod, ptrs.classDBPatternId,
                                     ptrs.classDBPatternsTried);
    entry["sceneTree"] = makeTarget(ptrs.SceneTree, ptrs.sceneTreeMethod, ptrs.sceneTreePatternId,
                                     ptrs.sceneTreePatternsTried);
    entry["engine"]    = makeTarget(ptrs.Engine, ptrs.engineMethod, ptrs.enginePatternId,
                                     ptrs.enginePatternsTried);

    entry["lastScanUtc"] = GetUtcTimestamp();

    // Increment scan count
    int scanCount = 1;
    if (entry.contains("scanCount") && entry["scanCount"].is_number_integer()) {
        scanCount = entry["scanCount"].get<int>() + 1;
    }
    entry["scanCount"] = scanCount;

    root["games"][peHash] = entry;

    // Write atomically
    if (WriteCacheFile(cachePath, root)) {
        LOG_INFO("Saved hint cache for PE hash %s (scan #%d)", peHash, scanCount);
    } else {
        LOG_ERROR("Failed to save hint cache for PE hash %s", peHash);
    }
}

// ============================================================
// Hit/miss tracking
// ============================================================

void RecordHintOutcome(bool hit) {
    if (hit)
        s_hintHits.fetch_add(1, std::memory_order_relaxed);
    else
        s_hintMisses.fetch_add(1, std::memory_order_relaxed);
}

// ============================================================
// Cache pruning — remove entries older than ttlDays
// ============================================================

int PruneStaleEntries(int ttlDays) {
    fs::path cachePath = GetCacheFilePath();
    if (cachePath.empty()) return 0;

    json root = ReadCacheFile(cachePath);
    if (root.is_discarded() || !root.contains("games")) return 0;

    auto& games = root["games"];
    if (!games.is_object()) return 0;

    // Compute cutoff timestamp
    auto now = std::chrono::system_clock::now();
    auto cutoff = now - std::chrono::hours(24 * ttlDays);
    auto cutoff_t = std::chrono::system_clock::to_time_t(cutoff);

    // Collect stale keys
    std::vector<std::string> staleKeys;
    for (auto it = games.begin(); it != games.end(); ++it) {
        if (!it->contains("lastScanUtc")) {
            staleKeys.push_back(it.key());
            continue;
        }

        auto& ts = (*it)["lastScanUtc"];
        if (!ts.is_string()) {
            staleKeys.push_back(it.key());
            continue;
        }

        // Parse ISO 8601 timestamp: YYYY-MM-DDTHH:MM:SSZ
        std::string tsStr = ts.get<std::string>();
        struct tm tm_entry {};
        if (sscanf(tsStr.c_str(), "%d-%d-%dT%d:%d:%dZ",
                   &tm_entry.tm_year, &tm_entry.tm_mon, &tm_entry.tm_mday,
                   &tm_entry.tm_hour, &tm_entry.tm_min, &tm_entry.tm_sec) == 6) {
            tm_entry.tm_year -= 1900;
            tm_entry.tm_mon  -= 1;
            time_t entry_t = _mkgmtime(&tm_entry);
            if (entry_t != -1 && entry_t < cutoff_t) {
                staleKeys.push_back(it.key());
            }
        } else {
            // Unparseable timestamp — consider stale
            staleKeys.push_back(it.key());
        }
    }

    if (staleKeys.empty()) return 0;

    for (const auto& key : staleKeys) {
        LOG_INFO("Pruning stale cache entry: %s (game=%s)",
                 key.c_str(),
                 games[key].value("gameName", "unknown").c_str());
        games.erase(key);
    }

    int pruned = static_cast<int>(staleKeys.size());

    if (WriteCacheFile(cachePath, root)) {
        LOG_INFO("Pruned %d stale hint cache entries (TTL=%d days)", pruned, ttlDays);
    } else {
        LOG_ERROR("Failed to write cache after pruning");
    }

    return pruned;
}

// ============================================================
// Clear specific entry
// ============================================================

bool ClearEntry(const char* peHash) {
    if (!peHash || !peHash[0]) return false;

    fs::path cachePath = GetCacheFilePath();
    if (cachePath.empty()) return false;

    json root = ReadCacheFile(cachePath);
    if (root.is_discarded() || !root.contains("games")) return false;

    auto& games = root["games"];
    if (!games.contains(peHash)) return false;

    games.erase(peHash);
    WriteCacheFile(cachePath, root);
    LOG_INFO("Cleared cache entry for PE hash %s", peHash);
    return true;
}

// ============================================================
// Diagnostics
// ============================================================

CacheStats GetStats() {
    CacheStats stats;
    stats.hintHits   = s_hintHits.load(std::memory_order_relaxed);
    stats.hintMisses = s_hintMisses.load(std::memory_order_relaxed);

    fs::path cachePath = GetCacheFilePath();
    if (!cachePath.empty()) {
        stats.cacheFilePath = cachePath.string();

        json root = ReadCacheFile(cachePath);
        if (!root.is_discarded() && root.contains("games") && root["games"].is_object()) {
            stats.totalEntries = static_cast<int>(root["games"].size());
        }
    }

    return stats;
}

// ============================================================
// Runtime CLR TypeLayout cache (session-scoped, in-memory only)
// Avoids repeated Metode JSON round-trips for the same type.
// ============================================================
static std::mutex s_clrCacheMutex;
static std::unordered_map<std::string, Zolt::Metode::TypeLayout> s_typeLayoutCache;
static std::unordered_map<uint64_t, std::string> s_mtToTypeName;
static bool s_mtCacheBuilt = false;

const Zolt::Metode::TypeLayout* LookupTypeLayout(const std::string& typeName) {
    std::lock_guard<std::mutex> lock(s_clrCacheMutex);
    auto it = s_typeLayoutCache.find(typeName);
    if (it != s_typeLayoutCache.end()) return &it->second;
    return nullptr;
}

void StoreTypeLayout(const std::string& typeName, const Zolt::Metode::TypeLayout& layout) {
    std::lock_guard<std::mutex> lock(s_clrCacheMutex);
    s_typeLayoutCache[typeName] = layout;
}

Zolt::Metode::TypeLayout GetTypeLayoutCached(const std::string& typeName) {
    // Fast path: check cache
    {
        std::lock_guard<std::mutex> lock(s_clrCacheMutex);
        auto it = s_typeLayoutCache.find(typeName);
        if (it != s_typeLayoutCache.end()) {
            LOG_INFO("CLR cache HIT: '%s' (%zu fields)", typeName.c_str(), it->second.fields.size());
            return it->second;
        }
    }

    // Slow path: call Metode (outside lock to avoid blocking other cache reads)
    if (!Zolt::Metode::IsReady()) return {};

    auto layout = Zolt::Metode::GetTypeLayout(typeName);

    // Cache even empty results to avoid re-querying failed types
    {
        std::lock_guard<std::mutex> lock(s_clrCacheMutex);
        s_typeLayoutCache[typeName] = layout;
    }

    if (!layout.fields.empty()) {
        LOG_INFO("CLR cache MISS → stored: '%s' (%zu fields)", typeName.c_str(), layout.fields.size());
    }

    return layout;
}

const std::string& LookupMTTypeName(uint64_t methodTable) {
    static const std::string s_empty;
    std::lock_guard<std::mutex> lock(s_clrCacheMutex);
    auto it = s_mtToTypeName.find(methodTable);
    return (it != s_mtToTypeName.end()) ? it->second : s_empty;
}

void EnsureMTCache() {
    std::lock_guard<std::mutex> lock(s_clrCacheMutex);
    if (s_mtCacheBuilt) return;
    if (!Zolt::Metode::IsReady()) return;

    auto types = Zolt::Metode::EnumerateTypes();
    for (auto& ts : types) {
        if (ts.methodTablePtr != 0)
            s_mtToTypeName[ts.methodTablePtr] = ts.fullName;
    }
    s_mtCacheBuilt = true;
    LOG_INFO("CLR MT cache built: %zu types", s_mtToTypeName.size());
}

void ClearCLRCache() {
    std::lock_guard<std::mutex> lock(s_clrCacheMutex);
    s_typeLayoutCache.clear();
    s_mtToTypeName.clear();
    s_mtCacheBuilt = false;
    LOG_INFO("CLR runtime caches cleared");
}

// ============================================================
// Runtime Godot PropertyList cache (session-scoped, in-memory only)
// Class property lists are fixed once ClassDB is initialized.
// ============================================================
static std::mutex s_propCacheMutex;
static std::unordered_map<std::string, std::vector<Zolt::Diagoldze::PropertyInfo>> s_propertyListCache;
static const std::vector<Zolt::Diagoldze::PropertyInfo> s_emptyPropList;

const std::vector<Zolt::Diagoldze::PropertyInfo>& GetPropertyListCached(const char* className) {
    if (!className || !className[0]) return s_emptyPropList;

    std::string key(className);

    // Fast path: check cache
    {
        std::lock_guard<std::mutex> lock(s_propCacheMutex);
        auto it = s_propertyListCache.find(key);
        if (it != s_propertyListCache.end()) return it->second;
    }

    // Slow path: call Diagoldze (outside lock)
    auto props = Zolt::Diagoldze::GetPropertyList(className);

    // Store result (even if empty, to avoid re-querying)
    std::lock_guard<std::mutex> lock(s_propCacheMutex);
    auto [it, inserted] = s_propertyListCache.emplace(std::move(key), std::move(props));
    if (inserted) {
        LOG_INFO("PropertyList cache MISS → stored: '%s' (%zu props)",
            className, it->second.size());
    }
    return it->second;
}

void ClearPropertyListCache() {
    std::lock_guard<std::mutex> lock(s_propCacheMutex);
    s_propertyListCache.clear();
    LOG_INFO("PropertyList cache cleared");
}

} // namespace Zolt::Himmel
