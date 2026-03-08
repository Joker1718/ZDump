// dllmain.cpp — DLL entry point for ZoltDumper
// Zolt-Dump: Godot Engine real-time memory dumper
//
// On DLL_PROCESS_ATTACH:
//   1. Store module handle for DLL path resolution
//   2. Initialize Solliter (logging)
//   3. Start Qual (mailbox) polling thread for CE Lua
//   4. Spawn auto-start thread (scan + pipe server)

#include <Windows.h>

// Forward declarations
// Stark — 修塔爾克 — Export API
namespace Zolt::Stark {
    int AutoStart();
}

// Solliter — 索莉緹爾 — Logger
namespace Zolt::Solliter {
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

static DWORD WINAPI AutoStartThreadProc(LPVOID /*param*/) {
    // Prevent double invocation (e.g. if DllMain is called twice)
    if (InterlockedCompareExchange(&g_autoStartOnce, 1, 0) != 0) {
        return 0;
    }

#ifdef ZOLT_PROXY_BUILD
    // Eisen — 艾乍 — Proxy mode: wait for engine to initialize singletons
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
        Zolt::Solliter::Init();

        // Start mailbox polling for CE Lua communication
        Zolt::Qual::StartPolling();

        // Spawn auto-start thread
        CreateThread(nullptr, 0, AutoStartThreadProc, nullptr, 0, nullptr);
    }
    return TRUE;
}
