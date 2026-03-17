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
