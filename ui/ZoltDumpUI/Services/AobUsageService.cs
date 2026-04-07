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
using ZoltDumpUI.Models;

namespace ZoltDumpUI.Services;

/// <summary>Persists AOB scan results for usage analysis.</summary>
public sealed class AobUsageService
{
    private readonly string _filePath;

    public AobUsageService(string appDataPath)
    {
        _filePath = Path.Combine(appDataPath, "aob_usage.json");
    }

    public async Task<List<AobUsageRecord>> LoadAsync(CancellationToken ct = default)
    {
        if (!File.Exists(_filePath)) return [];
        var json = await File.ReadAllTextAsync(_filePath, ct);
        return JsonSerializer.Deserialize(json, AobUsageJsonContext.Default.ListAobUsageRecord) ?? [];
    }

    public async Task RecordScanAsync(AobUsageRecord record, CancellationToken ct = default)
    {
        var records = await LoadAsync(ct);
        var existing = records.FindIndex(r => r.PeHash == record.PeHash);
        if (existing >= 0)
            records[existing] = record;
        else
            records.Add(record);
        await SaveAsync(records, ct);
    }

    public async Task DeleteGameAsync(string peHash, CancellationToken ct = default)
    {
        var records = await LoadAsync(ct);
        records.RemoveAll(r => r.PeHash == peHash);
        await SaveAsync(records, ct);
    }

    public async Task ResetAllAsync(CancellationToken ct = default)
    {
        if (File.Exists(_filePath))
        {
            // Numbered backup rotation (up to 10)
            for (var i = 9; i >= 1; i--)
            {
                var from = i == 1 ? _filePath : $"{_filePath}.bak{i - 1}";
                var to = $"{_filePath}.bak{i}";
                if (File.Exists(from))
                {
                    try { File.Move(from, to, overwrite: true); } catch { }
                }
            }
        }
        await SaveAsync([], ct);
    }

    private async Task SaveAsync(List<AobUsageRecord> records, CancellationToken ct)
    {
        var json = JsonSerializer.Serialize(records, AobUsageJsonContext.Default.ListAobUsageRecord);
        var tempPath = _filePath + ".tmp";
        await File.WriteAllTextAsync(tempPath, json, ct);
        File.Move(tempPath, _filePath, overwrite: true);
    }
}
