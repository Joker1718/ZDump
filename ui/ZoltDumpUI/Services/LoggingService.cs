/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
using Serilog;
using Serilog.Core;
using ZoltDumpUI.Core;

namespace ZoltDumpUI.Services;

/// <summary>
/// Solitaer — 索莉塔 (Mana Concealment)
/// Category-based file logging with process mirroring.
/// </summary>
public sealed class LoggingService : ILoggingService, IDisposable
{
    private readonly Logger _initLogger;
    private readonly Logger _pipeLogger;
    private readonly Logger _viewLogger;
    private readonly Logger _consoleLogger;
    private Logger? _mirrorLogger;
    private readonly string _logDir;

    public LoggingService(string logDir)
    {
        _logDir = logDir;
        Directory.CreateDirectory(logDir);

        _initLogger = CreateFileLogger("init");
        _pipeLogger = CreateFileLogger("pipe");
        _viewLogger = CreateFileLogger("view");
        _consoleLogger = new LoggerConfiguration()
            .MinimumLevel.Debug()
            .WriteTo.Console()
            .CreateLogger();
    }

    public void Info(string message) => Info("init", message);
    public void Info(string category, string message)
    {
        GetLogger(category).Information(message);
        _consoleLogger.Information("[{Category}] {Message}", category, message);
        _mirrorLogger?.Information("[{Category}] {Message}", category, message);
    }

    public void Warn(string message) => Warn("init", message);
    public void Warn(string category, string message)
    {
        GetLogger(category).Warning(message);
        _consoleLogger.Warning("[{Category}] {Message}", category, message);
        _mirrorLogger?.Warning("[{Category}] {Message}", category, message);
    }

    public void Error(string message, Exception? ex = null) => Error("init", message, ex);
    public void Error(string category, string message, Exception? ex = null)
    {
        if (ex is not null)
            GetLogger(category).Error(ex, message);
        else
            GetLogger(category).Error(message);
        _consoleLogger.Error(ex, "[{Category}] {Message}", category, message);
        _mirrorLogger?.Error(ex, "[{Category}] {Message}", category, message);
    }

    public void Debug(string message) => Debug("init", message);
    public void Debug(string category, string message)
    {
        GetLogger(category).Debug(message);
        _mirrorLogger?.Debug("[{Category}] {Message}", category, message);
    }

    public void StartProcessMirror(string processName)
    {
        StopProcessMirror();
        var mirrorDir = Path.Combine(_logDir, processName);
        Directory.CreateDirectory(mirrorDir);
        _mirrorLogger = new LoggerConfiguration()
            .MinimumLevel.Debug()
            .WriteTo.File(
                Path.Combine(mirrorDir, "mirror.log"),
                rollingInterval: RollingInterval.Infinite,
                fileSizeLimitBytes: 8 * 1024 * 1024,
                retainedFileCountLimit: 4)
            .CreateLogger();
    }

    public void StopProcessMirror()
    {
        _mirrorLogger?.Dispose();
        _mirrorLogger = null;
    }

    private Logger GetLogger(string category) => category switch
    {
        "pipe" => _pipeLogger,
        "view" => _viewLogger,
        _ => _initLogger
    };

    private Logger CreateFileLogger(string category)
    {
        var path = Path.Combine(_logDir, $"{category}.log");
        return new LoggerConfiguration()
            .MinimumLevel.Debug()
            .WriteTo.File(path,
                rollingInterval: RollingInterval.Infinite,
                fileSizeLimitBytes: 8 * 1024 * 1024,
                retainedFileCountLimit: 4)
            .CreateLogger();
    }

    public void Dispose()
    {
        _initLogger.Dispose();
        _pipeLogger.Dispose();
        _viewLogger.Dispose();
        _consoleLogger.Dispose();
        _mirrorLogger?.Dispose();
    }
}
