// Fern — 費倫 (Apprentice — PipeServer)
// Named pipe IPC server for communication with ZoltDumpUI.

#include "Fern.h"
#include "Solliter.h"
#include "Grimoire.h"
#include <Windows.h>
#include <atomic>

namespace Zolt::Fern {

static HANDLE              s_hPipe      = INVALID_HANDLE_VALUE;
static HANDLE              s_hThread    = nullptr;
static std::atomic<bool>   s_running{false};
static std::atomic<bool>   s_connected{false};

// TODO: Pipe server thread procedure
static DWORD WINAPI PipeThreadProc(LPVOID /*param*/) {
    Solliter::LogPipe("Fern: Pipe server thread started");

    // TODO: Create named pipe with Grimoire::PIPE_NAME
    // TODO: Loop: ConnectNamedPipe, read JSON commands, dispatch, write responses
    // TODO: Handle client disconnect and reconnect
    // TODO: Parse JSON commands per pipe-protocol.md spec

    while (s_running.load()) {
        // TODO: Implement pipe message loop
        Sleep(100);  // Placeholder to avoid busy-wait
    }

    Solliter::LogPipe("Fern: Pipe server thread exiting");
    return 0;
}

void Start() {
    if (s_running.load()) return;

    Solliter::LogPipe("Fern: Starting pipe server");
    s_running.store(true);

    // TODO: Create the named pipe handle
    // TODO: Spawn pipe server thread
    s_hThread = CreateThread(nullptr, 0, PipeThreadProc, nullptr, 0, nullptr);
}

void Stop() {
    if (!s_running.load()) return;

    Solliter::LogPipe("Fern: Stopping pipe server");
    s_running.store(false);

    // TODO: Cancel pending I/O, close pipe handle
    if (s_hPipe != INVALID_HANDLE_VALUE) {
        DisconnectNamedPipe(s_hPipe);
        CloseHandle(s_hPipe);
        s_hPipe = INVALID_HANDLE_VALUE;
    }

    if (s_hThread) {
        WaitForSingleObject(s_hThread, 3000);
        CloseHandle(s_hThread);
        s_hThread = nullptr;
    }

    s_connected.store(false);
}

bool IsConnected() {
    return s_connected.load();
}

} // namespace Zolt::Fern
