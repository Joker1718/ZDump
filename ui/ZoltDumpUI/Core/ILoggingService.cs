namespace ZoltDumpUI.Core;

/// <summary>
/// Solliter — 索莉緹爾 (Mana Concealment)
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
