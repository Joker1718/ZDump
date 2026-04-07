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

/// <summary>
/// Solitaer — 索莉塔 (Mana Concealment)
/// Logging service with category-based routing.
/// </summary>
public interface ILoggingService
{
    void Info(string message);
    void Info(string category, string message);
    void Warn(string message);
    void Warn(string category, string message);
    void Error(string message, Exception? ex = null);
    void Error(string category, string message, Exception? ex = null);
    void Debug(string message);
    void Debug(string category, string message);

    void StartProcessMirror(string processName);
    void StopProcessMirror();
}
