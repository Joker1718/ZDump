namespace ZoltDumpUI.Models;

/// <summary>Single class entry from "list_classes" command.</summary>
public sealed class ClassListItem
{
    public string ClassName { get; init; } = "";
    public string SuperName { get; init; } = "";
    public int PropertyCount { get; init; }
    public int MethodCount { get; init; }
    public bool IsGameClass { get; init; }
    public int Score { get; init; }
}
