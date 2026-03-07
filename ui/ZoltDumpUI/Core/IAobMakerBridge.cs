namespace ZoltDumpUI.Core;

/// <summary>AOBMaker CE Plugin bridge interface.</summary>
public interface IAobMakerBridge
{
    Task<bool> CheckAvailabilityAsync(CancellationToken ct = default);
    Task NavigateHexViewAsync(string address, CancellationToken ct = default);
    Task NavigateDisassemblerAsync(string address, CancellationToken ct = default);
    Task CreateAAScriptAsync(string label, string script, CancellationToken ct = default);
    Task CreateSymbolScriptAsync(string label, string address, CancellationToken ct = default);
}
