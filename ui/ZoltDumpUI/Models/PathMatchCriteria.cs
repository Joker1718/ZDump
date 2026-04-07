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

/// <summary>
/// Criteria for CMD_FIND_INSTANCE_BY_PATH — find an instance by matching
/// a nested property path against a string value.
/// </summary>
public sealed class PathMatchCriteria
{
    /// <summary>Class name filter (e.g. "Button"), or empty for any class.</summary>
    public string ClassName { get; init; } = "";

    /// <summary>Dot-separated property path to match (e.g. "item.my_id").</summary>
    public string MatchPath { get; init; } = "";

    /// <summary>Expected string value at end of path (e.g. "item_tree").</summary>
    public string MatchValue { get; init; } = "";
}
