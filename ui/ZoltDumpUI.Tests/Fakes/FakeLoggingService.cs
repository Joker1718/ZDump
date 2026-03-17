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
