namespace ZoltDumpUI.Models;

/// <summary>Result of "find_instances" command.</summary>
public sealed class FindInstancesResult
{
    public string ClassName { get; init; } = "";
    public int Count { get; init; }
    public List<ObjectEntry> Instances { get; init; } = [];
}
