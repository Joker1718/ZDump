namespace ZoltDumpUI.Models;

/// <summary>Live instance field values from "walk_instance" command.</summary>
public sealed class InstanceWalkResult
{
    public string Address { get; init; } = "";
    public string Name { get; init; } = "";
    public string ClassName { get; init; } = "";
    public string ParentAddr { get; init; } = "";
    public List<LiveFieldValue> Fields { get; init; } = [];
}
