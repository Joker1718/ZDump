#pragma once

// Qual — 全 (Wholeness — Mailbox)
// Polling-based communication channel for CE Lua scripts.
// Uses a separate named pipe (ZoltDumpMailbox) for simple
// command/response exchange with CE Lua callFunction.

namespace Zolt::Qual {

// Start the mailbox polling thread
// Creates the mailbox pipe and begins listening for CE Lua commands
void StartPolling();

// Stop the mailbox polling thread and close the pipe
void StopPolling();

} // namespace Zolt::Qual
