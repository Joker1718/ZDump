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
}
