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
