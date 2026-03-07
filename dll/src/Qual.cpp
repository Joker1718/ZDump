// Qual — 全 (Wholeness — Mailbox)
// Polling-based communication channel for CE Lua scripts.

#include "Qual.h"
#include "Solliter.h"
#include "Grimoire.h"
#include <Windows.h>
#include <atomic>

namespace Zolt::Qual {

static HANDLE            s_hThread = nullptr;
static std::atomic<bool> s_running{false};

static DWORD WINAPI MailboxThreadProc(LPVOID /*param*/) {
    Solliter::LogPipe("Qual: Mailbox polling thread started");

    // TODO: Create mailbox named pipe (Grimoire::MAILBOX_NAME)
    // TODO: Poll for incoming CE Lua commands
    // TODO: Dispatch commands to appropriate handlers
    // TODO: Send responses back through the pipe

    while (s_running.load()) {
        // TODO: Implement mailbox polling loop
        Sleep(100);  // Placeholder to avoid busy-wait
    }

    Solliter::LogPipe("Qual: Mailbox polling thread exiting");
    return 0;
}

void StartPolling() {
    if (s_running.load()) return;

    Solliter::LogPipe("Qual: Starting mailbox polling");
    s_running.store(true);
    s_hThread = CreateThread(nullptr, 0, MailboxThreadProc, nullptr, 0, nullptr);
}

void StopPolling() {
    if (!s_running.load()) return;

    Solliter::LogPipe("Qual: Stopping mailbox polling");
    s_running.store(false);

    if (s_hThread) {
        WaitForSingleObject(s_hThread, 3000);
        CloseHandle(s_hThread);
        s_hThread = nullptr;
    }
}

} // namespace Zolt::Qual
