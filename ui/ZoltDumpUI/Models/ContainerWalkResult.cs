namespace ZoltDumpUI.Models;

/// <summary>Container element data from "walk_container" command.</summary>
public sealed class ContainerWalkResult
{
    public string Address { get; init; } = "";
    public string ContainerType { get; init; } = "";
    public int ElementCount { get; init; }
    public string ElementType { get; init; } = "";
    public string KeyType { get; init; } = "";
    public List<LiveFieldValue> Fields { get; init; } = [];
}
