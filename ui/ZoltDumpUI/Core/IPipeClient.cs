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
