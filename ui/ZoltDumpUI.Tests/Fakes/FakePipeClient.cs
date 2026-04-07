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
using ZoltDumpUI.Core;

namespace ZoltDumpUI.Tests.Fakes;

/// <summary>
/// Fake IPipeClient for unit tests.
/// Queues JSON responses that are returned by SendAsync in FIFO order.
/// </summary>
public sealed class FakePipeClient : IPipeClient
{
    private readonly Queue<JsonObject> _responses = new();
    private readonly List<(string Command, JsonObject? Payload)> _sent = [];

    public bool IsConnected => true;
    public event Action<bool>? ConnectionStateChanged;
    public event Action<JsonObject>? EventReceived;

    /// <summary>All commands sent through this client.</summary>
    public IReadOnlyList<(string Command, JsonObject? Payload)> Sent => _sent;

    /// <summary>Enqueue a JSON response to be returned by the next SendAsync call.</summary>
    public void Enqueue(JsonObject response) => _responses.Enqueue(response);

    /// <summary>Enqueue a JSON response from a raw JSON string.</summary>
    public void Enqueue(string json) => _responses.Enqueue(JsonNode.Parse(json)!.AsObject());

    public Task ConnectAsync(CancellationToken ct = default) => Task.CompletedTask;
    public Task DisconnectAsync() => Task.CompletedTask;

    public Task<JsonObject> SendAsync(string command, JsonObject? payload = null, CancellationToken ct = default)
    {
        _sent.Add((command, payload));
        if (_responses.Count == 0)
            throw new InvalidOperationException($"FakePipeClient: no response queued for command '{command}'");
        return Task.FromResult(_responses.Dequeue());
    }

    public void Dispose() { }

    // Suppress unused event warnings
    internal void RaiseConnectionState(bool connected) => ConnectionStateChanged?.Invoke(connected);
    internal void RaiseEvent(JsonObject obj) => EventReceived?.Invoke(obj);
}
