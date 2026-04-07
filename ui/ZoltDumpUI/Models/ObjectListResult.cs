/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
using System.Diagnostics.CodeAnalysis;

namespace ZoltDumpUI.Models;

/// <summary>Paginated object list from "get_object_list" command.</summary>
public sealed class ObjectListResult
{
    public int Total { get; init; }
    public int Offset { get; init; }
    public int Count { get; init; }
    /// <summary>Number of valid objects scanned from slot 0 to current position.</summary>
    public int Scanned { get; init; }
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
