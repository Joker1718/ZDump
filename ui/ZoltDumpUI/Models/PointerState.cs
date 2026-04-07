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

/// <summary>Global pointer addresses returned by "get_pointers" command.</summary>
public sealed class PointerState
{
    public string ObjectDb { get; init; } = "";
    public string ClassDb { get; init; } = "";
    public string SceneTree { get; init; } = "";
    public string Engine { get; init; } = "";
    public int SlotCount { get; init; }
    public int SlotMax { get; init; }
    public int ObjectCount { get; init; }

    // Scan metadata
    public string ObjectDbMethod { get; init; } = "";
    public string ClassDbMethod { get; init; } = "";
    public string SceneTreeMethod { get; init; } = "";
    public string ObjectDbPatternId { get; init; } = "";
    public string ClassDbPatternId { get; init; } = "";
    public string SceneTreePatternId { get; init; } = "";
    public string ObjectDbScanAddr { get; init; } = "";
    public string ClassDbScanAddr { get; init; } = "";
    public string SceneTreeScanAddr { get; init; } = "";
}
