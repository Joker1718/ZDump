namespace ZoltDumpUI.Models;

/// <summary>CE XML pointer chain data from "get_ce_pointer_info" command.</summary>
public sealed class CePointerInfo
{
    public List<BreadcrumbEntry> Breadcrumbs { get; init; } = [];
    public List<LiveFieldValue> Fields { get; init; } = [];
}

/// <summary>Navigation breadcrumb for CE XML chain building.</summary>
public sealed class BreadcrumbEntry
{
    public string Address { get; init; } = "";
    public string Name { get; init; } = "";
    public string ClassName { get; init; } = "";
    public int Offset { get; init; }
    public bool IsPointer { get; init; }
    public string ModuleRva { get; init; } = "";
}
