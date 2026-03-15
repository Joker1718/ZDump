#pragma once

// Heiter — 海塔 (Priest — GameThread Dispatch)
// Dispatches work items to execute on the game's main thread via
// a WH_GETMESSAGE hook on the game window's thread.
// Required for thread-safe MethodBind::ptrcall invocations.

#include <functional>
#include <Windows.h>

namespace Zolt::Heiter {

// Initialize the game thread dispatcher.
// Installs a WH_GETMESSAGE hook on the game window's thread.
// hModule: DLL module handle (needed by SetWindowsHookExW)
// Returns true if hook installed immediately, false if deferred (no window yet).
bool Init(HMODULE hModule);

// Queue a fire-and-forget work item to execute on the game thread.
void QueueWork(std::function<void()> callback);

// Queue a work item and BLOCK until it completes on the game thread.
// Returns true if executed successfully, false on timeout or error.
bool QueueWorkSync(std::function<void()> work, DWORD timeoutMs = 5000);

// Is the hook active and ready to drain work?
bool IsReady();

// Shut down the dispatcher: unhook, signal pending sync callers, discard queue.
void Shutdown();

} // namespace Zolt::Heiter
