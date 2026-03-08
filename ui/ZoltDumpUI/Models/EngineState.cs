namespace ZoltDumpUI.Models;

/// <summary>Engine initialization state returned by "init" command.</summary>
public sealed class EngineState
{
    public bool Ok { get; init; }
    public string GodotVersion { get; init; } = "";
    public string ProcessName { get; init; } = "";
    public int Pid { get; init; }
    public string ModulePath { get; init; } = "";
    public string ScanMethod { get; init; } = "";
    public string Error { get; init; } = "";
}
