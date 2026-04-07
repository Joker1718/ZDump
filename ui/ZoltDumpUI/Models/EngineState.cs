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

/// <summary>Engine initialization state returned by "init" command.</summary>
public sealed class EngineState
{
    public bool Ok { get; init; }
    public string GodotVersion { get; init; } = "";
    public string ProcessName { get; init; } = "";
    public int Pid { get; init; }
    public string ModulePath { get; init; } = "";
    public string ScanMethod { get; init; } = "";
    public string Error { get; init; } = "";
}
