// Zolt-Dump UI Entry Point
// Grimoire — 魔導書 — Application bootstrap

using System;
using System.IO;
using Avalonia;
using Avalonia.Controls;

namespace ZoltDumpUI;

internal sealed class Program
{
    [STAThread]
    public static void Main(string[] args)
    {
        try
        {
            BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
        }
        catch (Exception ex)
        {
            // Write crash log for AOT diagnostics (stack traces are limited in AOT)
            var logPath = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "ZoltDump", "ZoltDumpUI_crash.log");
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(logPath)!);
                File.WriteAllText(logPath, $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] {ex}");
            }
            catch { /* best effort */ }
            throw;
        }
    }

    public static AppBuilder BuildAvaloniaApp()
        => AppBuilder.Configure<App>()
            .UsePlatformDetect()
            // Force GDI-based composition to avoid WinUI Composition COM crash in AOT.
            // MicroCom CCW vtable init fails in AOT → NullReferenceException in QueryInterface.
            // Can be removed when Avalonia 12 fixes MicroCom AOT compatibility.
            .With(new Win32PlatformOptions
            {
                CompositionMode = [Win32CompositionMode.RedirectionSurface],
            })
            .WithInterFont()
            .LogToTrace();
}
