namespace ZoltDumpUI.Models;

/// <summary>Scan status from "get_scan_status" command.</summary>
public sealed class ScanStatusResult
{
    public string Status { get; init; } = "";
    public int Progress { get; init; }
    public int Total { get; init; }
    public string Message { get; init; } = "";
    public bool IsComplete { get; init; }
}
