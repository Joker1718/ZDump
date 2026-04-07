/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
using System.Text.Json;
using System.Text.Json.Serialization;

namespace ZoltDumpUI.Services;

/// <summary>
/// Himmel — 欣梅爾 (Hero's Memory)
/// Records DLL inject history for tracking tested games.
/// Writes to %LOCALAPPDATA%\ZoltDump\inject_history.json.
/// </summary>
public static class InjectHistoryService
{
    private const int MaxEntries = 20;
    private static readonly string HistoryPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "ZoltDump", "inject_history.json");

    /// <summary>Record a successful inject event.</summary>
    public static void Record(string processName, int pid, string godotVersion, int objectCount)
    {
        try
        {
            var entries = Load();

            entries.Add(new InjectHistoryEntry
            {
                Timestamp = DateTime.Now.ToString("o"),
                Game = processName,
                Pid = pid,
                GodotVersion = godotVersion,
                ObjectCount = objectCount
            });

            // Keep only the last N entries
            if (entries.Count > MaxEntries)
                entries.RemoveRange(0, entries.Count - MaxEntries);

            var dir = Path.GetDirectoryName(HistoryPath);
            if (!string.IsNullOrEmpty(dir))
                Directory.CreateDirectory(dir);

            var json = JsonSerializer.Serialize(entries, InjectHistoryJsonContext.Default.ListInjectHistoryEntry);
            File.WriteAllText(HistoryPath, json);
        }
        catch
        {
            // Best-effort — don't crash if history write fails
        }
    }

    private static List<InjectHistoryEntry> Load()
    {
        try
        {
            if (!File.Exists(HistoryPath)) return [];
            var json = File.ReadAllText(HistoryPath);
            return JsonSerializer.Deserialize(json, InjectHistoryJsonContext.Default.ListInjectHistoryEntry) ?? [];
        }
        catch
        {
            return [];
        }
    }
}

/// <summary>Single inject history entry.</summary>
public sealed class InjectHistoryEntry
{
    [JsonPropertyName("timestamp")]
    public string Timestamp { get; set; } = "";

    [JsonPropertyName("game")]
    public string Game { get; set; } = "";

    [JsonPropertyName("pid")]
    public int Pid { get; set; }

    [JsonPropertyName("godot_version")]
    public string GodotVersion { get; set; } = "";

    [JsonPropertyName("object_count")]
    public int ObjectCount { get; set; }
}

/// <summary>AOT-compatible JSON context for inject history.</summary>
[JsonSerializable(typeof(List<InjectHistoryEntry>))]
[JsonSerializable(typeof(InjectHistoryEntry))]
internal partial class InjectHistoryJsonContext : JsonSerializerContext;
