/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
namespace ZoltDumpUI.Core;

/// <summary>Platform abstraction for OS-specific operations.</summary>
public interface IPlatformService
{
    bool TryAcquireSingleInstance();
    void ReleaseSingleInstance();
    string GetAppDataPath();
    string GetLogDirectoryPath();
    Task CopyToClipboardAsync(string text);
    string GetMachineName();
    Task<string?> ShowSaveFileDialogAsync(string title, string defaultFileName, string filter);
}
