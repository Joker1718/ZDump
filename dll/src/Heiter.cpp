// Heiter — 海塔 (Priest — GameThread Dispatch)
// Queues work items to be executed on the game's main thread.

#include "Heiter.h"
#include "Solliter.h"
#include <Windows.h>
#include <atomic>
#include <mutex>
#include <queue>

namespace Zolt::Heiter {

static std::mutex                          s_mutex;
static std::queue<std::function<void()>>   s_workQueue;
static std::atomic<bool>                   s_initialized{false};

void Init() {
    if (s_initialized.load()) return;

    // TODO: Hook a suitable game thread callback (e.g., SceneTree::idle or process)
    // TODO: Install a trampoline that drains s_workQueue each frame
    Solliter::LogInit("Heiter::Init: TODO — not yet implemented");
    s_initialized.store(true);
}

void QueueWork(const std::function<void()>& callback) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_workQueue.push(callback);
    // TODO: Signal the game thread to process the queue
}

void Shutdown() {
    if (!s_initialized.load()) return;

    // TODO: Remove game thread hook
    // TODO: Drain and discard pending work
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        while (!s_workQueue.empty()) s_workQueue.pop();
    }
    s_initialized.store(false);
    Solliter::LogInit("Heiter::Shutdown complete");
}

} // namespace Zolt::Heiter
