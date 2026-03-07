namespace ZoltDumpUI.Models;

/// <summary>Scene tree hierarchy from "walk_tree" command.</summary>
public sealed class TreeWalkResult
{
    public TreeNodeInfo? Root { get; init; }
}

/// <summary>Single node in the scene tree hierarchy.</summary>
public sealed class TreeNodeInfo
{
    public string Address { get; init; } = "";
    public string Name { get; init; } = "";
    public string ClassName { get; init; } = "";
    public string NodePath { get; init; } = "";
    public int ChildCount { get; init; }
    public List<TreeNodeInfo> Children { get; init; } = [];
}
