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

/// <summary>Scan status from "get_scan_status" command.</summary>
public sealed class ScanStatusResult
{
    public string Status { get; init; } = "";
    public int Progress { get; init; }
    public int Total { get; init; }
    public string Message { get; init; } = "";
    public bool IsComplete { get; init; }
}
