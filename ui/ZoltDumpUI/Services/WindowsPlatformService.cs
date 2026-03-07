using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Input.Platform;
using Avalonia.Platform.Storage;
using ZoltDumpUI.Core;

namespace ZoltDumpUI.Services;

/// <summary>Windows-specific platform operations.</summary>
public sealed class WindowsPlatformService : IPlatformService
{
    private Mutex? _mutex;

    public bool TryAcquireSingleInstance()
    {
        _mutex = new Mutex(true, Grimoire.MutexName, out var created);
        if (!created)
        {
            _mutex.Dispose();
            _mutex = null;
        }
        return created;
    }

    public void ReleaseSingleInstance()
    {
        _mutex?.ReleaseMutex();
        _mutex?.Dispose();
        _mutex = null;
    }

    public string GetAppDataPath()
    {
        var path = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "ZoltDump");
        Directory.CreateDirectory(path);
        return path;
    }

    public string GetLogDirectoryPath()
    {
        var path = Path.Combine(GetAppDataPath(), "Logs");
        Directory.CreateDirectory(path);
        return path;
    }

    public async Task CopyToClipboardAsync(string text)
    {
        if (Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var clipboard = desktop.MainWindow?.Clipboard;
            if (clipboard is not null)
                await clipboard.SetTextAsync(text);
        }
    }

    public string GetMachineName() => Environment.MachineName;

    public async Task<string?> ShowSaveFileDialogAsync(string title, string defaultFileName, string filter)
    {
        if (Application.Current?.ApplicationLifetime is not IClassicDesktopStyleApplicationLifetime desktop)
            return null;

        var window = desktop.MainWindow;
        if (window is null) return null;

        var file = await window.StorageProvider.SaveFilePickerAsync(new FilePickerSaveOptions
        {
            Title = title,
            SuggestedFileName = defaultFileName,
            DefaultExtension = Path.GetExtension(defaultFileName)
        });
        return file?.Path.LocalPath;
    }
}
