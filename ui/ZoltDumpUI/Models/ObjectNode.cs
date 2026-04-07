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

/// <summary>Detailed single object from "get_object" command.</summary>
public sealed class ObjectNode
{
    public string Address { get; init; } = "";
    public string Name { get; init; } = "";
    public string ClassName { get; init; } = "";
    public string ClassAddr { get; init; } = "";
    public string ParentAddr { get; init; } = "";
    public string ParentName { get; init; } = "";
    public string NodePath { get; init; } = "";
    public long ObjectId { get; init; }
    public int SlotIndex { get; init; }
    public bool IsNode { get; init; }
    public bool IsRefCounted { get; init; }
}
