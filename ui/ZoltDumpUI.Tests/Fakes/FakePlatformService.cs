/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
using ZoltDumpUI.Core;

namespace ZoltDumpUI.Tests.Fakes;

/// <summary>No-op platform service for tests.</summary>
public sealed class FakePlatformService : IPlatformService
{
    public string LastCopied { get; private set; } = "";

    public bool TryAcquireSingleInstance() => true;
    public void ReleaseSingleInstance() { }
    public string GetAppDataPath() => Path.GetTempPath();
    public string GetLogDirectoryPath() => Path.GetTempPath();
    public Task CopyToClipboardAsync(string text) { LastCopied = text; return Task.CompletedTask; }
    public string GetMachineName() => "TEST";
    public Task<string?> ShowSaveFileDialogAsync(string title, string defaultFileName, string filter)
        => Task.FromResult<string?>(null);
}
