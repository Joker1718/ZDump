namespace ZoltDumpUI.Models;

/// <summary>Result of property search across classes.</summary>
public sealed class PropertySearchResult
{
    public int Count { get; init; }
    public List<PropertySearchHit> Hits { get; init; } = [];
}

/// <summary>Single property search hit.</summary>
public sealed class PropertySearchHit
{
    public string ClassName { get; init; } = "";
    public string SuperName { get; init; } = "";
    public string PropertyName { get; init; } = "";
    public string PropertyType { get; init; } = "";
    public int Offset { get; init; }
    public int Size { get; init; }
    public string Preview { get; init; } = "";
}
