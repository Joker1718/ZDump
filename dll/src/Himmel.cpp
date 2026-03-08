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
#include "Zoltraak.h"
#include "Solliter.h"
#include "Grimoire.h"

#include <Windows.h>
#include <ShlObj.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <ctime>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Zolt::Himmel {

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

} // namespace Zolt::Himmel
