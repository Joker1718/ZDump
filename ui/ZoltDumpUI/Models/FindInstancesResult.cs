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

/// <summary>Result of "find_instances" command.</summary>
public sealed class FindInstancesResult
{
    public string ClassName { get; init; } = "";
    public int Count { get; init; }
    public List<ObjectEntry> Instances { get; init; } = [];
}
