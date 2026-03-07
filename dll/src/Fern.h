#pragma once

// Fern — 費倫 (Apprentice — PipeServer)
// Named pipe IPC server for communication with ZoltDumpUI.
// Pipe name: \\.\pipe\ZoltDumpBfx (from Grimoire)
// Protocol: newline-delimited JSON messages.

#include <cstdint>

namespace Zolt::Fern {

// Start the pipe server on a background thread
// Creates the named pipe and waits for client connections
void Start();

// Stop the pipe server and close all connections
void Stop();

// Check if a client is currently connected
bool IsConnected();

} // namespace Zolt::Fern
