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

/// <summary>Container element data from "walk_container" command.</summary>
public sealed class ContainerWalkResult
{
    public string Address { get; init; } = "";
    public string ContainerType { get; init; } = "";
    public int ElementCount { get; init; }
    public string ElementType { get; init; } = "";
    public string KeyType { get; init; } = "";
    public List<LiveFieldValue> Fields { get; init; } = [];
}
