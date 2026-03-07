#pragma once

// Heiter — 海塔 (Priest — GameThread Dispatch)
// Queues work items to be executed on the game's main thread.
// Required for thread-safe access to Godot internals that are
// not safe to read from arbitrary threads.

#include <functional>

namespace Zolt::Heiter {

// Initialize the game thread dispatcher
// Hooks a suitable callback point on the game thread
void Init();

// Queue a work item to be executed on the game thread
// The callback will be invoked on the next game frame
void QueueWork(const std::function<void()>& callback);

// Shut down the dispatcher, cancel pending work
void Shutdown();

} // namespace Zolt::Heiter
