// Solitaer — 索莉緹爾 (Mana Concealment — Logger)
// 5-category per-process logging system for Zolt-Dump.
// Ported from UE5CEDumper Logger.cpp with adaptations for Godot targets.
//
// Category file mapping:
//   init-0.log    - DLL load, version info, startup, summary
//   scan-0.log    - AOB scanning, pattern matching, memory scans
//   offsets-0.log - Offset validation, DynOff, version detection
//   pipe-0.log    - Named pipe IPC events
//   walk-0.log    - Scene tree traversal, object enumeration, property walking

#include "Solitaer.h"
#include "Grimoire.h"
#include "Zoltraak.h"

#include <ShlObj.h>       // SHGetKnownFolderPath
#include <cstdio>
#include <cstdarg>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

namespace Zolt::Solitaer {

// ============================================================
// Log file enumeration (5 categories)
// ============================================================
enum LogFile : uint8_t {
    LF_Init = 0,    // init-0.log
    LF_Scan,        // scan-0.log
    LF_Offsets,     // offsets-0.log
    LF_Pipe,        // pipe-0.log
    LF_Walk,        // walk-0.log
    LF_COUNT
};

static const char* s_fileNames[LF_COUNT] = {
    "init", "scan", "offsets", "pipe", "walk"
};

// ============================================================
// Category routing table (longest-prefix-first)
// ============================================================
struct CatMapping {
    const char* prefix;
    int         prefixLen;
    LogFile     file;
};

// Sorted longest-prefix-first for correct matching
static const CatMapping s_catMap[] = {
    { "SCAN:ObjDB",  10, LF_Scan    },
    { "SCAN:ClsDB",  10, LF_Scan    },
    { "SCAN:STree",  10, LF_Scan    },
    { "SCAN:Eng",     8, LF_Scan    },
    { "SCAN:Ver",     8, LF_Scan    },
    { "SCAN:Hint",    9, LF_Scan    },
    { "PIPE:svr",     8, LF_Pipe    },
    { "PIPE:cmd",     8, LF_Pipe    },
    { "WALK:Node",    9, LF_Walk    },
    { "WALK:Prop",    9, LF_Walk    },
    { "WALK:Inst",    9, LF_Walk    },
    { "SUMMARY",      7, LF_Init    },
    { "FLAMME",       6, LF_Init    },
    { "CLSDB",        5, LF_Init    },
    { "OBJDB",        5, LF_Init    },
    { "STREE",        5, LF_Init    },
    { "INIT",         4, LF_Init    },
    { "SCAN",         4, LF_Scan    },
    { "DYNO",         4, LF_Offsets },
    { "OFFS",         4, LF_Offsets },
    { "WALK",         4, LF_Walk    },
    { "PIPE",         4, LF_Pipe    },
    { "MEM",          3, LF_Scan    },
    { "HINT",         4, LF_Scan    },
};

static constexpr int s_catMapCount = sizeof(s_catMap) / sizeof(s_catMap[0]);

// ============================================================
// Per-file state
// ============================================================
struct LogFileState {
    FILE*         handle   = nullptr;
    size_t        written  = 0;       // Bytes written to current file
    fs::path      path;               // Full path to current file
    std::string   baseName;           // e.g. "init", "scan"
};

// ============================================================
// Module state
// ============================================================
static std::mutex      s_mutex;
static LogFileState    s_files[LF_COUNT];
static fs::path        s_logDir;            // Per-process log directory
static bool            s_initialized = false;
static bool            s_filesOpen   = false;

// Early buffer: lines logged before InitProcessMirror opens files
struct BufferedLine {
    LogFile     file;
    std::string text;
};
static std::vector<BufferedLine> s_earlyBuffer;
static bool s_earlyBuffering = false;

// ============================================================
// Internal helpers
// ============================================================

static fs::path GetBaseLogDirectory() {
    wchar_t* appdata = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appdata))) {
        fs::path dir = fs::path(appdata) / Grimoire::LOG_DIR_NAME / Grimoire::LOG_SUBFOLDER;
        CoTaskMemFree(appdata);
        return dir;
    }
    return fs::path(L".");
}

static LogFile ResolveFile(const char* cat) {
    if (!cat || !cat[0]) return LF_Init;

    for (int i = 0; i < s_catMapCount; ++i) {
        if (_strnicmp(cat, s_catMap[i].prefix, s_catMap[i].prefixLen) == 0) {
            return s_catMap[i].file;
        }
    }
    return LF_Init;  // Default: init.log
}

static std::string FormatTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    auto time_t_now = std::chrono::system_clock::to_time_t(now);

    struct tm tm_now {};
    localtime_s(&tm_now, &time_t_now);

    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
        tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
        tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec,
        static_cast<int>(ms.count()));
    return buf;
}

static void RotateFile(LogFile lf) {
    auto& f = s_files[lf];
    if (f.handle) {
        fclose(f.handle);
        f.handle = nullptr;
    }

    // Shift files: 2->3, 1->2, 0->1
    for (int i = Grimoire::LOG_ROTATE_MAX - 1; i > 0; --i) {
        fs::path oldPath = s_logDir / (f.baseName + "-" + std::to_string(i - 1) + ".log");
        fs::path newPath = s_logDir / (f.baseName + "-" + std::to_string(i) + ".log");
        std::error_code ec;
        fs::rename(oldPath, newPath, ec);  // Ignore errors (file may not exist)
    }

    // Reopen -0.log
    f.path = s_logDir / (f.baseName + "-0.log");
    f.handle = _wfopen(f.path.c_str(), L"wt");
    f.written = 0;
}

static void WriteToFile(LogFile lf, const char* text, size_t len) {
    auto& f = s_files[lf];
    if (!f.handle) return;

    // Check rotation
    if (f.written + len > Grimoire::LOG_MAX_SIZE) {
        RotateFile(lf);
        if (!f.handle) return;
    }

    fwrite(text, 1, len, f.handle);
    fflush(f.handle);
    f.written += len;
}

static void OpenFiles() {
    std::error_code ec;
    fs::create_directories(s_logDir, ec);
    if (ec) return;

    // Rotate ALL log files on each DLL start (fresh logs per session)
    for (int i = 0; i < LF_COUNT; ++i) {
        auto& f = s_files[i];
        f.baseName = s_fileNames[i];
        f.path = s_logDir / (f.baseName + "-0.log");

        // If the file exists, rotate it so each DLL injection gets a fresh log
        if (fs::exists(f.path, ec)) {
            RotateFile(static_cast<LogFile>(i));
        } else {
            f.handle = _wfopen(f.path.c_str(), L"wt");
            f.written = 0;
        }
    }
    s_filesOpen = true;
}

// Prune old subfolders if count exceeds max
static void PruneSubfolders(const fs::path& baseDir, int maxSubfolders) {
    std::error_code ec;
    std::vector<fs::directory_entry> dirs;
    for (auto& entry : fs::directory_iterator(baseDir, ec)) {
        if (entry.is_directory()) {
            dirs.push_back(entry);
        }
    }

    if (static_cast<int>(dirs.size()) <= maxSubfolders) return;

    // Sort by last write time (oldest first)
    std::sort(dirs.begin(), dirs.end(), [](const auto& a, const auto& b) {
        std::error_code ec2;
        return fs::last_write_time(a, ec2) < fs::last_write_time(b, ec2);
    });

    // Remove oldest until under limit
    int toRemove = static_cast<int>(dirs.size()) - maxSubfolders;
    for (int i = 0; i < toRemove; ++i) {
        fs::remove_all(dirs[i].path(), ec);
    }
}

static void WriteLogLine(const char* level, const char* cat, const char* message) {
    std::string timestamp = FormatTimestamp();
    LogFile lf = ResolveFile(cat);

    // Format line
    char line[4096];
    int len;
    if (cat && cat[0]) {
        len = snprintf(line, sizeof(line), "[%s] [%s] [%s] %s\n",
            timestamp.c_str(), level, cat, message);
    } else {
        len = snprintf(line, sizeof(line), "[%s] [%s] %s\n",
            timestamp.c_str(), level, message);
    }
    if (len < 0) return;
    if (len >= static_cast<int>(sizeof(line))) len = sizeof(line) - 1;

    std::lock_guard<std::mutex> lock(s_mutex);

    // Always output to debug console
    OutputDebugStringA(line);

    if (s_filesOpen) {
        WriteToFile(lf, line, static_cast<size_t>(len));
    } else if (s_earlyBuffering) {
        if (static_cast<int>(s_earlyBuffer.size()) < Grimoire::LOG_EARLY_BUF) {
            s_earlyBuffer.push_back({ lf, std::string(line, len) });
        }
    }
}

static void FormatAndWrite(const char* level, const char* cat, const char* fmt, va_list args) {
    char message[3072];
    vsnprintf(message, sizeof(message), fmt, args);
    message[sizeof(message) - 1] = '\0';
    WriteLogLine(level, cat, message);
}

// ============================================================
// Public API
// ============================================================

bool Init() {
    // Allow re-initialization (supports DLL re-injection into same process)
    if (s_initialized && s_filesOpen) {
        // Already fully initialized — just log a restart marker
        Info("INIT", "=== DLL re-injected (logger already initialized) ===");
        return true;
    }
    s_earlyBuffering = true;
    s_initialized = true;
    return true;
}

void InitProcessMirror(const std::wstring& processName, int maxSubfolders) {
    std::lock_guard<std::mutex> lock(s_mutex);

    if (s_filesOpen) return;  // Already initialized

    // Build log directory path
    fs::path baseDir = GetBaseLogDirectory();
    s_logDir = baseDir / processName;

    // Prune old process subfolders
    if (fs::exists(baseDir)) {
        PruneSubfolders(baseDir, maxSubfolders);
    }

    // Open all 5 category files
    OpenFiles();

    // Flush early buffer
    s_earlyBuffering = false;
    for (auto& entry : s_earlyBuffer) {
        WriteToFile(entry.file, entry.text.c_str(), entry.text.size());
    }
    s_earlyBuffer.clear();
    s_earlyBuffer.shrink_to_fit();
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(s_mutex);
    for (int i = 0; i < LF_COUNT; ++i) {
        if (s_files[i].handle) {
            fflush(s_files[i].handle);
            fclose(s_files[i].handle);
            s_files[i].handle = nullptr;
        }
    }
    s_filesOpen = false;
    s_initialized = false;
}

void Info(const char* cat, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    FormatAndWrite("INFO", cat, fmt, args);
    va_end(args);
}

void Error(const char* cat, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    FormatAndWrite("ERROR", cat, fmt, args);
    va_end(args);
}

void Warn(const char* cat, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    FormatAndWrite("WARN", cat, fmt, args);
    va_end(args);
}

void Debug(const char* cat, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    FormatAndWrite("DEBUG", cat, fmt, args);
    va_end(args);
}

void Summary(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    FormatAndWrite("INFO", "SUMMARY", fmt, args);
    va_end(args);
}

// ============================================================
// HexDump — dump memory block to log
// ============================================================
void HexDump(const char* cat, uint64_t baseAddr, size_t maxBytes) {
    if (!baseAddr || maxBytes == 0) return;

    constexpr size_t LINE_BYTES = 16;
    uint8_t buf[LINE_BYTES];

    for (size_t off = 0; off < maxBytes; off += LINE_BYTES) {
        size_t chunk = (off + LINE_BYTES <= maxBytes) ? LINE_BYTES : (maxBytes - off);

        // Read memory safely
        bool ok = true;
        for (size_t i = 0; i < chunk; ++i) {
            buf[i] = Zoltraak::ReadSafe<uint8_t>(baseAddr + off + i, 0);
            if (!Zoltraak::IsReadable(baseAddr + off + i)) { ok = false; break; }
        }
        if (!ok) {
            Info(cat, "  +0x%04X: (unreadable)", static_cast<int>(off));
            continue;
        }

        // Format hex part
        char hex[64], ascii[20];
        int hpos = 0;
        for (size_t i = 0; i < LINE_BYTES; ++i) {
            if (i == 8) hex[hpos++] = ' ';
            if (i < chunk) {
                hpos += snprintf(hex + hpos, sizeof(hex) - hpos, "%02X ", buf[i]);
                ascii[i] = (buf[i] >= 0x20 && buf[i] < 0x7F) ? static_cast<char>(buf[i]) : '.';
            } else {
                hpos += snprintf(hex + hpos, sizeof(hex) - hpos, "   ");
                ascii[i] = ' ';
            }
        }
        ascii[LINE_BYTES] = '\0';

        // Check if any 8-byte aligned value looks like a pointer
        const char* annotation = "";
        if (chunk >= 8) {
            uint64_t val = 0;
            memcpy(&val, buf, 8);
            if (val > 0x10000 && Zoltraak::IsReadable(val))
                annotation = " <- ptr";
        }

        Info(cat, "  +0x%04X: %s |%s|%s",
            static_cast<int>(off), hex, ascii, annotation);
    }
}

// ============================================================
// Throttle — rate-limited logging
// ============================================================
static std::unordered_map<std::string, std::chrono::steady_clock::time_point> s_throttleMap;

bool Throttle(const char* key, int intervalMs, const char* cat, const char* fmt, ...) {
    auto now = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = s_throttleMap.find(key);
        if (it != s_throttleMap.end()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->second).count();
            if (elapsed < intervalMs) return false;
        }
        s_throttleMap[key] = now;
    }

    va_list args;
    va_start(args, fmt);
    FormatAndWrite("INFO", cat, fmt, args);
    va_end(args);
    return true;
}

// ============================================================
// ScopedTimer destructor
// ============================================================
ScopedTimer::~ScopedTimer() {
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    if (elapsed >= 1000) {
        Info(cat, "[TIMER] %s: %.1f ms", label, elapsed / 1000.0);
    } else {
        Info(cat, "[TIMER] %s: %lld us", label, static_cast<long long>(elapsed));
    }
}

// Legacy API wrappers
void LogInit(const char* msg)    { Info("INIT", "%s", msg); }
void LogScan(const char* msg)    { Info("SCAN", "%s", msg); }
void LogOffsets(const char* msg) { Info("OFFS", "%s", msg); }
void LogPipe(const char* msg)    { Info("PIPE", "%s", msg); }
void LogWalk(const char* msg)    { Info("WALK", "%s", msg); }

} // namespace Zolt::Solitaer
