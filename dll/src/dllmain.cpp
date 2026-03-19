// dllmain.cpp — DLL entry point for ZoltDumper
// Zolt-Dump: Godot Engine real-time memory dumper
//
// On DLL_PROCESS_ATTACH:
//   1. Store module handle for DLL path resolution
//   2. Initialize Solitaer (logging)
//   3. Start Qual (mailbox) polling thread for CE Lua
//   4. Spawn auto-start thread (scan + pipe server)

#include <Windows.h>
#include <algorithm>
#include <string>

// Forward declarations
// Stark — 修塔爾克 — Export API
namespace Zolt::Stark {
    int AutoStart();
}

// Solitaer — 索莉緹爾 — Logger
namespace Zolt::Solitaer {
    bool Init();
}

// Qual — 全 — Mailbox
namespace Zolt::Qual {
    void StartPolling();
}

// Macht — 馬赫特 — CE Plugin
namespace Zolt::Macht {
    extern bool g_isCEPlugin;
}

static HMODULE g_hModule = nullptr;
static volatile LONG g_autoStartOnce = 0;  // InterlockedExchange guard

// Accessor for DLL module handle (used by Heiter for SetWindowsHookEx)
extern "C" HMODULE Zolt_GetDllModule() { return g_hModule; }

// Check if current process is a known non-game helper (proxy mode only).
// These executables load winmm.dll from the game directory but are not Godot processes.
static bool IsBlockedProcess() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    // Extract filename and lowercase it
    std::wstring exe(path);
    auto slash = exe.find_last_of(L"\\/");
    if (slash != std::wstring::npos) exe = exe.substr(slash + 1);
    std::transform(exe.begin(), exe.end(), exe.begin(), ::towlower);

    // Blocklist: helper/utility processes commonly found in game directories
    static const wchar_t* blocked[] = {
        L"crashpad_handler.exe",        // Chromium/CEF crash reporter
        L"crashpad_handler_console.exe", // Chromium variant
        L"unitycrashdump64.exe",        // Unity crash handler
        L"unitycrashhandler64.exe",     // Unity crash handler
        L"unitycrashhandler32.exe",     // Unity crash handler
        L"cef_subprocess.exe",          // Chromium Embedded Framework
        L"notification_helper.exe",     // Windows notification helper
        L"dxwebsetup.exe",             // DirectX installer
        L"vcredist_x64.exe",           // Visual C++ redistributable
        L"ue4prereqsetup_x64.exe",     // Unreal prereq installer
    };
    for (auto* b : blocked) {
        if (exe == b) return true;
    }
    return false;
}

static DWORD WINAPI AutoStartThreadProc(LPVOID /*param*/) {
    // Prevent double invocation (e.g. if DllMain is called twice)
    if (InterlockedCompareExchange(&g_autoStartOnce, 1, 0) != 0) {
        return 0;
    }

#ifdef ZOLT_PROXY_BUILD
    // Eisen — 艾乍 — Proxy mode: skip known non-game helper processes
    // (e.g., crashpad_handler.exe loads winmm.dll from the same directory)
    if (IsBlockedProcess()) {
        return 0;
    }

    // Wait for engine to initialize singletons
    // 1500ms gives Godot time to create Engine, SceneTree etc. before we scan
    Sleep(1500);
    Zolt::Stark::AutoStart();
#else
    // CE/Inject mode: wait for CE plugin init
    Sleep(1000);

    // If we're loaded into CE.exe itself, skip auto-start
    if (Zolt::Macht::g_isCEPlugin) {
        return 0;
    }

    // Check for existing pipe — if another instance is running, skip
    HANDLE hPipe = CreateFileA(
        "\\\\.\\pipe\\ZoltDumpBfx",
        GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(hPipe);
        return 0;  // Another instance already running
    }

    Zolt::Stark::AutoStart();
#endif
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);

        // Initialize logging (early buffering until InitProcessMirror)
        Zolt::Solitaer::Init();

        // Start mailbox polling for CE Lua communication
        Zolt::Qual::StartPolling();

        // Spawn auto-start thread
        CreateThread(nullptr, 0, AutoStartThreadProc, nullptr, 0, nullptr);
    }
    return TRUE;
}
