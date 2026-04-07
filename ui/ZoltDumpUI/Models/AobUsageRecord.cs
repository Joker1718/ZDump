/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
using System.Text.Json.Serialization;

namespace ZoltDumpUI.Models;

/// <summary>Per-game AOB scan record for usage tracking.</summary>
public sealed class AobUsageRecord
{
    [JsonPropertyName("gameName")]
    public string GameName { get; init; } = "";

    [JsonPropertyName("peHash")]
    public string PeHash { get; init; } = "";

    [JsonPropertyName("godotVersion")]
    public string GodotVersion { get; init; } = "";

    [JsonPropertyName("scanDate")]
    public DateTime ScanDate { get; init; }

    [JsonPropertyName("patternsUsed")]
    public List<string> PatternsUsed { get; init; } = [];
}

[JsonSerializable(typeof(List<AobUsageRecord>))]
[JsonSourceGenerationOptions(
    PropertyNamingPolicy = JsonKnownNamingPolicy.CamelCase,
    WriteIndented = true)]
internal partial class AobUsageJsonContext : JsonSerializerContext;
