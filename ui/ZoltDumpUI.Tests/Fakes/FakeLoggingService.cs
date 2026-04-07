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

/// <summary>No-op logging service for tests.</summary>
public sealed class FakeLoggingService : ILoggingService
{
    public void Info(string message) { }
    public void Info(string category, string message) { }
    public void Warn(string message) { }
    public void Warn(string category, string message) { }
    public void Error(string message, Exception? ex = null) { }
    public void Error(string category, string message, Exception? ex = null) { }
    public void Debug(string message) { }
    public void Debug(string category, string message) { }
    public void StartProcessMirror(string processName) { }
    public void StopProcessMirror() { }
}
