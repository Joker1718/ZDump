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
