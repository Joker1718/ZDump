namespace ZoltDumpUI.Models;

/// <summary>Result of reverse address lookup via "find_by_address" command.</summary>
public sealed class AddressLookupResult
{
    public bool Found { get; init; }
    public string MatchType { get; init; } = "";
    public string Address { get; init; } = "";
    public int SlotIndex { get; init; }
    public string Name { get; init; } = "";
    public string ClassName { get; init; } = "";
    public string ParentAddr { get; init; } = "";
    public int OffsetFromBase { get; init; }
    public string QueryAddress { get; init; } = "";

    /// <summary>
    /// Alternative parent objects that also reference the same .NET managed child.
    /// Only populated for managed_instance L2 matches where multiple Godot objects
    /// share the same nested .NET data object (e.g. Creature + TypeLabel + HealthBar).
    /// </summary>
    public List<AlternativeEntry> Alternatives { get; init; } = [];
}

/// <summary>Alternative parent object for a managed_instance match.</summary>
public sealed class AlternativeEntry
{
    public string Address { get; init; } = "";
    public string Name { get; init; } = "";
    public string ClassName { get; init; } = "";
    public int SlotIndex { get; init; }
}
