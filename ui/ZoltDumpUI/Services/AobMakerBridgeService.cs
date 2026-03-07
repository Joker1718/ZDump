using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using ZoltDumpUI.Core;
using ZoltDumpUI.Models;

namespace ZoltDumpUI.Services;

/// <summary>AOBMaker CE Plugin bridge via named pipe.</summary>
public sealed class AobMakerBridgeService : IAobMakerBridge
{
    public async Task<bool> CheckAvailabilityAsync(CancellationToken ct = default)
    {
        try
        {
            using var pipe = new NamedPipeClientStream(".", "AOBMakerCEBridge", PipeDirection.InOut);
            await pipe.ConnectAsync(500, ct);
            return true;
        }
        catch { return false; }
    }

    public async Task NavigateHexViewAsync(string address, CancellationToken ct = default)
    {
        await SendMessageAsync(new AobMakerMessage { Cmd = "navigate_hex", Address = address }, ct);
    }

    public async Task NavigateDisassemblerAsync(string address, CancellationToken ct = default)
    {
        await SendMessageAsync(new AobMakerMessage { Cmd = "navigate_disasm", Address = address }, ct);
    }

    public async Task CreateAAScriptAsync(string label, string script, CancellationToken ct = default)
    {
        await SendMessageAsync(new AobMakerMessage { Cmd = "create_aa", Label = label, Script = script }, ct);
    }

    public async Task CreateSymbolScriptAsync(string label, string address, CancellationToken ct = default)
    {
        await SendMessageAsync(new AobMakerMessage { Cmd = "create_symbol", Label = label, Address = address }, ct);
    }

    private static async Task SendMessageAsync(AobMakerMessage msg, CancellationToken ct)
    {
        using var pipe = new NamedPipeClientStream(".", "AOBMakerCEBridge", PipeDirection.InOut, PipeOptions.Asynchronous);
        await pipe.ConnectAsync(2000, ct);

        var json = JsonSerializer.Serialize(msg, AobMakerJsonContext.Default.AobMakerMessage);
        var payload = Encoding.UTF8.GetBytes(json);
        var lengthBytes = BitConverter.GetBytes((uint)payload.Length);

        await pipe.WriteAsync(lengthBytes, ct);
        await pipe.WriteAsync(payload, ct);
        await pipe.FlushAsync(ct);
    }
}
