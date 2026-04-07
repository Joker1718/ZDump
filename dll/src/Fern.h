/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Fern — 費倫 (Apprentice — PipeServer)
// Named pipe IPC server for communication with ZoltDumpUI.
// Pipe name: \\.\pipe\ZoltDumpBfx (from Grimoire)
// Protocol: newline-delimited JSON messages (see docs/pipe-protocol.md).

#include <cstdint>
#include <string>

namespace Zolt::Fern {

// Start the pipe server on a background thread.
// Creates the named pipe and enters the connection/message loop.
void Start();

// Stop the pipe server, cancel I/O, and close all handles.
void Stop();

// Check if a client is currently connected.
bool IsConnected();

// Send an async push event to the connected client (e.g. watch notifications).
// Returns false if no client is connected or write fails.
bool SendEvent(const std::string& json);

} // namespace Zolt::Fern
