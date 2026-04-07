/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
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
