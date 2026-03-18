// Heiter — 海塔 (Priest — GameThread Dispatch)
// Dispatches work items to the game's main thread via a WH_GETMESSAGE hook.
//
// Godot calls PeekMessageW every frame (DisplayServerWindows::process_events),
// so our hook fires reliably on the game thread each frame. This allows safe
// invocation of MethodBind::ptrcall for property reading.

#include "Heiter.h"
#include "Solitaer.h"
#include "Grimoire.h"

#include <atomic>
#include <deque>
#include <mutex>

namespace Zolt::Heiter {

// ============================================================
// WorkItem — queued task with optional sync signaling
// ============================================================
struct WorkItem {
    std::function<void()> fn;
    HANDLE                doneEvent;   // nullptr = fire-and-forget
    bool                  succeeded;   // set by executor
};

// ============================================================
// Static state
// ============================================================
static HMODULE           s_hModule       = nullptr;
static HHOOK             s_hHook         = nullptr;
static HWND              s_hGameWnd      = nullptr;
static DWORD             s_gameThreadId  = 0;
static HANDLE            s_retryThread   = nullptr;
static std::atomic<bool> s_initialized   {false};
static std::atomic<bool> s_hookActive    {false};
static std::atomic<bool> s_draining      {false};   // re-entrancy guard
static std::atomic<bool> s_shutdownFlag  {false};
static std::atomic<int>  s_hookCallCount {0};        // Diagnostic: how many times hook proc fired
static std::atomic<int>  s_consecTimeouts{0};        // Consecutive sync dispatch timeouts
static std::atomic<bool> s_ptrcallDisabled{false};   // Auto-disabled after too many timeouts
static std::mutex        s_mutex;
static std::deque<WorkItem> s_workQueue;

// ============================================================
// SEH-safe work item execution
// Separated into its own function because MSVC does not allow
// __try/__except in functions with C++ objects that have destructors.
// ============================================================
static bool ExecuteWithSEH(void (*rawFn)(void*), void* ctx) {
    __try {
        rawFn(ctx);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Solitaer::Error("HEITER", "SEH exception in work item (code 0x%08X)",
                        GetExceptionCode());
        return false;
    }
}

// Thunk: call std::function via raw pointer
static void CallStdFunction(void* ctx) {
    auto* fn = static_cast<std::function<void()>*>(ctx);
    (*fn)();
}

// ============================================================
// DrainWorkQueue — execute pending items on the game thread
// ============================================================
static void DrainWorkQueue() {
    // Re-entrancy guard: if we're already draining, skip
    if (s_draining.exchange(true)) return;

    ULONGLONG startTick = GetTickCount64();

    std::unique_lock<std::mutex> lock(s_mutex);
    while (!s_workQueue.empty()) {
        // Time budget check
        if (GetTickCount64() - startTick > Zolt::Heiter::DRAIN_BUDGET_MS) break;

        WorkItem item = std::move(s_workQueue.front());
        s_workQueue.pop_front();
        lock.unlock();

        // Execute with SEH protection
        bool ok = ExecuteWithSEH(CallStdFunction, &item.fn);

        // Signal completion for sync callers
        if (item.doneEvent) {
            item.succeeded = ok;
            SetEvent(item.doneEvent);
        }

        lock.lock();
    }

    s_draining.store(false);
}

// ============================================================
// Hook callback — fires on the game thread during PeekMessage
// ============================================================
static LRESULT CALLBACK HeiterHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    s_hookCallCount.fetch_add(1, std::memory_order_relaxed);
    if (nCode >= 0 && !s_workQueue.empty()) {
        DrainWorkQueue();
    }
    return CallNextHookEx(s_hHook, nCode, wParam, lParam);
}

// ============================================================
// FindGameWindow — locate the game's main window
// ============================================================
struct EnumCtx {
    DWORD pid;
    HWND  bestWnd;
    int   bestArea;
};

static BOOL CALLBACK EnumWndProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lParam);

    DWORD wndPid = 0;
    GetWindowThreadProcessId(hwnd, &wndPid);
    if (wndPid != ctx->pid) return TRUE;

    // Must be a visible, titled, top-level window
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindowTextLengthW(hwnd) == 0) return TRUE;

    // Prefer windows with WS_CAPTION (game window, not console/overlay)
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    if (!(style & WS_CAPTION)) return TRUE;

    // Pick the largest client area (heuristic: game window is biggest)
    RECT rc = {};
    GetClientRect(hwnd, &rc);
    int area = (rc.right - rc.left) * (rc.bottom - rc.top);
    if (area > ctx->bestArea) {
        ctx->bestArea = area;
        ctx->bestWnd  = hwnd;
    }

    return TRUE; // continue enumeration
}

static HWND FindGameWindow() {
    EnumCtx ctx = { GetCurrentProcessId(), nullptr, 0 };
    EnumWindows(EnumWndProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.bestWnd;
}

// ============================================================
// InstallHook — install WH_GETMESSAGE on the game thread
// ============================================================
static bool InstallHook() {
    s_hGameWnd = FindGameWindow();
    if (!s_hGameWnd) return false;

    s_gameThreadId = GetWindowThreadProcessId(s_hGameWnd, nullptr);
    if (!s_gameThreadId) return false;

    // Get window title for logging
    wchar_t title[128] = {};
    GetWindowTextW(s_hGameWnd, title, 128);
    char titleA[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, title, -1, titleA, sizeof(titleA), nullptr, nullptr);

    Solitaer::Info("HEITER", "Game window found: hwnd=%p tid=%lu title='%s'",
                   s_hGameWnd, s_gameThreadId, titleA);

    // Install thread-specific message hook
    // MSDN: hMod should be NULL when hooking a thread in the current process
    // and the hook procedure is within the process's code (our injected DLL).
    // First try with NULL (correct per MSDN), fallback to DLL module handle.
    s_hHook = SetWindowsHookExW(WH_GETMESSAGE, HeiterHookProc, nullptr, s_gameThreadId);
    if (!s_hHook) {
        Solitaer::Debug("HEITER", "SetWindowsHookExW(hMod=NULL) failed (%lu), retrying with DLL module",
                        GetLastError());
        s_hHook = SetWindowsHookExW(WH_GETMESSAGE, HeiterHookProc, s_hModule, s_gameThreadId);
    }
    if (!s_hHook) {
        Solitaer::Error("HEITER", "SetWindowsHookExW failed (error %lu)", GetLastError());
        return false;
    }

    s_hookActive.store(true);
    Solitaer::Info("HEITER", "WH_GETMESSAGE hook installed on thread %lu", s_gameThreadId);
    return true;
}

// ============================================================
// Retry thread — waits for game window to appear
// ============================================================
static DWORD WINAPI RetryThreadProc(LPVOID /*param*/) {
    Solitaer::Info("HEITER", "Window discovery retry thread started");

    for (int i = 0; i < Zolt::Heiter::RETRY_ATTEMPTS; ++i) {
        if (s_shutdownFlag.load()) break;

        Sleep(Zolt::Heiter::RETRY_INTERVAL_MS);

        if (s_shutdownFlag.load()) break;

        if (InstallHook()) {
            Solitaer::Info("HEITER", "Hook installed on retry attempt %d", i + 1);
            return 0;
        }
    }

    Solitaer::Warn("HEITER", "Window discovery gave up after %d attempts",
                   Zolt::Heiter::RETRY_ATTEMPTS);
    return 1;
}

// ============================================================
// Public API
// ============================================================

bool Init(HMODULE hModule) {
    if (s_initialized.exchange(true)) return s_hookActive.load();

    s_hModule = hModule;
    s_shutdownFlag.store(false);

    Solitaer::LogInit("Heiter::Init: installing game thread hook");

    // Try immediate install
    if (InstallHook()) {
        return true;
    }

    // Deferred: spawn retry thread
    Solitaer::Info("HEITER", "No game window yet, starting retry thread");
    s_retryThread = CreateThread(nullptr, 0, RetryThreadProc, nullptr, 0, nullptr);
    return false;
}

void QueueWork(std::function<void()> callback) {
    if (!callback) return;

    if (!s_hookActive.load()) {
        // Hook not ready — execute inline as fallback (caller accepts risk)
        Solitaer::Debug("HEITER", "QueueWork: hook not active, executing inline");
        callback();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_workQueue.size() >= Zolt::Heiter::MAX_QUEUE_SIZE) {
            Solitaer::Warn("HEITER", "Work queue overflow (%zu items), dropping",
                           s_workQueue.size());
            return;
        }
        s_workQueue.push_back({std::move(callback), nullptr, false});
    }

    // Wake game thread if idle
    PostThreadMessageW(s_gameThreadId, WM_NULL, 0, 0);
}

bool QueueWorkSync(std::function<void()> work, DWORD timeoutMs) {
    if (!work) return false;

    // If ptrcall is auto-disabled due to repeated timeouts, skip entirely
    if (s_ptrcallDisabled.load()) {
        return false;
    }

    if (!s_hookActive.load()) {
        Solitaer::Debug("HEITER", "QueueWorkSync: hook not active, executing inline");
        work();
        return true;
    }

    // Same-thread check: avoid deadlock
    if (GetCurrentThreadId() == s_gameThreadId) {
        work();
        return true;
    }

    HANDLE evt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!evt) return false;

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_workQueue.size() >= Zolt::Heiter::MAX_QUEUE_SIZE) {
            Solitaer::Warn("HEITER", "Work queue overflow in sync dispatch");
            CloseHandle(evt);
            return false;
        }
        s_workQueue.push_back({std::move(work), evt, false});
    }

    // Wake game thread — try both PostThreadMessage and PostMessage to window
    PostThreadMessageW(s_gameThreadId, WM_NULL, 0, 0);
    if (s_hGameWnd) {
        PostMessageW(s_hGameWnd, WM_NULL, 0, 0);
    }

    DWORD waitResult = WaitForSingleObject(evt, timeoutMs);
    CloseHandle(evt);

    if (waitResult == WAIT_TIMEOUT) {
        int n = s_consecTimeouts.fetch_add(1) + 1;
        Solitaer::Warn("HEITER", "QueueWorkSync timed out after %lu ms (hookCalls=%d, consecutive=%d)",
                       timeoutMs, s_hookCallCount.load(), n);
        if (n >= Zolt::Heiter::MAX_CONSEC_TIMEOUTS) {
            s_ptrcallDisabled.store(true);
            Solitaer::Warn("HEITER", "Ptrcall auto-disabled after %d consecutive timeouts. "
                           "Hook proc fired %d times total. Direct memory reads only.",
                           n, s_hookCallCount.load());
        }
        return false;
    }

    // Success — reset timeout counter
    s_consecTimeouts.store(0);
    return (waitResult == WAIT_OBJECT_0);
}

bool IsPtrcallDisabled() {
    return s_ptrcallDisabled.load();
}

void ResetTimeoutState() {
    s_consecTimeouts.store(0);
    s_ptrcallDisabled.store(false);
}

bool IsReady() {
    return s_hookActive.load();
}

void Shutdown() {
    if (!s_initialized.load()) return;

    Solitaer::LogInit("Heiter::Shutdown: removing hook");
    s_shutdownFlag.store(true);

    // Wait for retry thread to finish
    if (s_retryThread) {
        WaitForSingleObject(s_retryThread, 5000);
        CloseHandle(s_retryThread);
        s_retryThread = nullptr;
    }

    // Remove the hook
    if (s_hHook) {
        UnhookWindowsHookEx(s_hHook);
        s_hHook = nullptr;
    }
    s_hookActive.store(false);

    // Signal all pending sync callers to unblock them
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        while (!s_workQueue.empty()) {
            auto& item = s_workQueue.front();
            if (item.doneEvent) {
                item.succeeded = false;
                SetEvent(item.doneEvent);
            }
            s_workQueue.pop_front();
        }
    }

    s_hGameWnd     = nullptr;
    s_gameThreadId = 0;
    s_initialized.store(false);
    Solitaer::LogInit("Heiter::Shutdown complete");
}

} // namespace Zolt::Heiter
