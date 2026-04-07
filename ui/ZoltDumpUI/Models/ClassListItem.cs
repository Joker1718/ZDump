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

/// <summary>Single class entry from "list_classes" command.</summary>
[DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.PublicProperties)]
public sealed class ClassListItem
{
    public string ClassName { get; init; } = "";
    public string SuperName { get; init; } = "";
    public int PropertyCount { get; init; }
    public int MethodCount { get; init; }
    public bool IsGameClass { get; init; }
    public int Score { get; init; }
}
