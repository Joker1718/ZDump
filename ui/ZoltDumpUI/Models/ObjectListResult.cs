using System.Diagnostics.CodeAnalysis;

namespace ZoltDumpUI.Models;

/// <summary>Paginated object list from "get_object_list" command.</summary>
public sealed class ObjectListResult
{
    public int Total { get; init; }
    public int Offset { get; init; }
    public int Count { get; init; }
    public List<ObjectEntry> Objects { get; init; } = [];
}

/// <summary>Single object in the object list.</summary>
[DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.PublicProperties)]
public sealed class ObjectEntry
{
    public string Address { get; init; } = "";
    public string Name { get; init; } = "";
    public string ClassName { get; init; } = "";
    public int SlotIndex { get; init; }
    public long ObjectId { get; init; }
    public string ParentAddr { get; init; } = "";
}
