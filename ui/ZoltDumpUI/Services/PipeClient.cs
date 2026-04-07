/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
using System.Collections.Concurrent;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using ZoltDumpUI.Core;

namespace ZoltDumpUI.Services;

/// <summary>
/// Fern — 費倫 (Apprentice Messenger)
/// Named pipe client for ZoltDumper.dll communication.
/// </summary>
public sealed class PipeClient : IPipeClient
{
    private NamedPipeClientStream? _pipe;
    private StreamReader? _reader;
    private StreamWriter? _writer;
    private readonly SemaphoreSlim _writeLock = new(1, 1);
    private readonly ConcurrentDictionary<int, TaskCompletionSource<JsonObject>> _pending = new();
    private int _nextId;
    private CancellationTokenSource? _readCts;

    public bool IsConnected => _pipe?.IsConnected == true;
    public event Action<bool>? ConnectionStateChanged;
    public event Action<JsonObject>? EventReceived;

    public async Task ConnectAsync(CancellationToken ct = default)
    {
        _pipe = new NamedPipeClientStream(".", Grimoire.PipeNameBare, PipeDirection.InOut, PipeOptions.Asynchronous);
        await _pipe.ConnectAsync(5000, ct);

        _reader = new StreamReader(_pipe, Encoding.UTF8, leaveOpen: true);
        _writer = new StreamWriter(_pipe, Encoding.UTF8, leaveOpen: true) { AutoFlush = true };

        _readCts = new CancellationTokenSource();
        _ = ReadLoopAsync(_readCts.Token);

        ConnectionStateChanged?.Invoke(true);
    }

    public async Task DisconnectAsync()
    {
        _readCts?.Cancel();

        foreach (var kvp in _pending)
        {
            kvp.Value.TrySetCanceled();
            _pending.TryRemove(kvp.Key, out _);
        }

        if (_pipe is not null)
        {
            try { _pipe.Close(); } catch { }
            _pipe = null;
        }
        _reader = null;
        _writer = null;

        ConnectionStateChanged?.Invoke(false);
        await Task.CompletedTask;
    }

    public async Task<JsonObject> SendAsync(string command, JsonObject? payload = null, CancellationToken ct = default)
    {
        var id = Interlocked.Increment(ref _nextId);
        var request = new JsonObject
        {
            ["id"] = id,
            ["cmd"] = command
        };
        if (payload is not null)
        {
            foreach (var kvp in payload)
                request[kvp.Key] = kvp.Value?.DeepClone();
        }

        var tcs = new TaskCompletionSource<JsonObject>(TaskCreationOptions.RunContinuationsAsynchronously);
        _pending[id] = tcs;

        await using var reg = ct.Register(() => tcs.TrySetCanceled(ct));

        await _writeLock.WaitAsync(ct);
        try
        {
            var line = request.ToJsonString();
            await _writer!.WriteLineAsync(line);
        }
        finally
        {
            _writeLock.Release();
        }

        return await tcs.Task;
    }

    private async Task ReadLoopAsync(CancellationToken ct)
    {
        try
        {
            while (!ct.IsCancellationRequested && _reader is not null)
            {
                var line = await _reader.ReadLineAsync(ct);
                if (line is null) break;

                var obj = JsonNode.Parse(line)?.AsObject();
                if (obj is null) continue;

                if (obj.TryGetPropertyValue("id", out var idNode) && idNode is not null)
                {
                    var id = idNode.GetValue<int>();
                    if (_pending.TryRemove(id, out var tcs))
                        tcs.TrySetResult(obj);
                }
                else
                {
                    EventReceived?.Invoke(obj);
                }
            }
        }
        catch (OperationCanceledException) { }
        catch
        {
            ConnectionStateChanged?.Invoke(false);
        }
    }

    public void Dispose()
    {
        _readCts?.Cancel();
        _readCts?.Dispose();
        _pipe?.Dispose();
        _writeLock.Dispose();
    }
}
