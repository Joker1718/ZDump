/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
using System.Text.Json.Nodes;

namespace ZoltDumpUI.Core;

/// <summary>Named pipe client for DLL communication.</summary>
public interface IPipeClient : IDisposable
{
    bool IsConnected { get; }
    event Action<bool>? ConnectionStateChanged;
    event Action<JsonObject>? EventReceived;

    Task ConnectAsync(CancellationToken ct = default);
    Task DisconnectAsync();
    Task<JsonObject> SendAsync(string command, JsonObject? payload = null, CancellationToken ct = default);
}
