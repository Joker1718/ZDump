/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using ZoltDumpUI.Core;
using ZoltDumpUI.Models;

namespace ZoltDumpUI.Services;

/// <summary>
/// AOBMaker CE Plugin bridge via named pipe.
/// Protocol: 4-byte LE length prefix + UTF-8 JSON (per-request connect).
/// Must match AOBMaker/plugins/CEPlugin/src/pipe_server.cpp wire format.
/// </summary>
public sealed class AobMakerBridgeService : IAobMakerBridge
{
    private const string PipeName = "AOBMakerCEBridge";

    // Protocol type constants (must match protocol.h)
    private const string TypeNavigateHexView = "NavigateHexView";
    private const string TypeNavigateDisassembler = "NavigateDisassembler";
    private const string TypeCreateAAScript = "CreateAAScript";
    private const string TypeCreateSymbolScript = "CreateSymbolScript";
    private const string TypeCreateMemoryRecord = "CreateMemoryRecord";
    private const string TypeGetAttachedProcess = "GetAttachedProcess";

    public async Task<bool> CheckAvailabilityAsync(CancellationToken ct = default)
    {
        // Use a lightweight ping: GetAttachedProcess is fast and harmless.
        // Avoids the old approach of connect+disconnect which the server logged as "failed to read message".
        try
        {
            var response = await SendAndReceiveAsync(
                new AobMakerMessage { Type = TypeGetAttachedProcess }, ct);
            return response is not null;
        }
        catch { return false; }
    }

    public async Task NavigateHexViewAsync(string address, CancellationToken ct = default)
    {
        await SendAndReceiveAsync(new AobMakerMessage
        {
            Type = TypeNavigateHexView,
            Address = address
        }, ct);
    }

    public async Task NavigateDisassemblerAsync(string address, CancellationToken ct = default)
    {
        await SendAndReceiveAsync(new AobMakerMessage
        {
            Type = TypeNavigateDisassembler,
            Address = address
        }, ct);
    }

    public async Task CreateAAScriptAsync(string label, string script, CancellationToken ct = default)
    {
        await SendAndReceiveAsync(new AobMakerMessage
        {
            Type = TypeCreateAAScript,
            Description = label,
            Script = script,
            AutoActivate = false
        }, ct);
    }

    public async Task CreateSymbolScriptAsync(string label, string address, CancellationToken ct = default)
    {
        await SendAndReceiveAsync(new AobMakerMessage
        {
            Type = TypeCreateSymbolScript,
            Name = label,
            Symbol = label,
            Address = address
        }, ct);
    }

    public async Task CreateMemoryRecordAsync(string description, string address,
        CeValueType valueType, bool isSigned = false, CancellationToken ct = default)
    {
        await SendAndReceiveAsync(new AobMakerMessage
        {
            Type = TypeCreateMemoryRecord,
            Description = description,
            Address = address,
            ValueType = (int)valueType,
            IsSigned = isSigned
        }, ct);
    }

    /// <summary>
    /// Send a length-prefixed JSON message and read the response.
    /// Each call creates a fresh pipe connection (per-request, as AOBMaker expects).
    /// </summary>
    private static async Task<string?> SendAndReceiveAsync(AobMakerMessage msg, CancellationToken ct)
    {
        using var pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut, PipeOptions.Asynchronous);
        await pipe.ConnectAsync(2000, ct);

        // Write: 4-byte LE length prefix + JSON payload
        var json = JsonSerializer.Serialize(msg, AobMakerJsonContext.Default.AobMakerMessage);
        var payload = Encoding.UTF8.GetBytes(json);
        var lengthBytes = BitConverter.GetBytes((uint)payload.Length);

        await pipe.WriteAsync(lengthBytes, ct);
        await pipe.WriteAsync(payload, ct);
        await pipe.FlushAsync(ct);

        // Read response: 4-byte LE length prefix + JSON
        var respLenBuf = new byte[4];
        int totalRead = 0;
        while (totalRead < 4)
        {
            int read = await pipe.ReadAsync(respLenBuf.AsMemory(totalRead, 4 - totalRead), ct);
            if (read == 0) return null; // pipe closed
            totalRead += read;
        }

        uint respLen = BitConverter.ToUInt32(respLenBuf, 0);
        if (respLen == 0 || respLen > 10 * 1024 * 1024) return null; // sanity check

        var respBuf = new byte[respLen];
        totalRead = 0;
        while (totalRead < (int)respLen)
        {
            int read = await pipe.ReadAsync(respBuf.AsMemory(totalRead, (int)respLen - totalRead), ct);
            if (read == 0) return null;
            totalRead += read;
        }

        return Encoding.UTF8.GetString(respBuf);
    }
}
