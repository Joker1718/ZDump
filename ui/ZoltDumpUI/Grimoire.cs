// Grimoire — 魔導書 (Grimoire)
// Application constants and magic strings for ZoltDumpUI.
// All magic strings are centralized here for easy maintenance.

namespace ZoltDumpUI;

/// <summary>
/// Grimoire — 魔導書 — Central repository of application constants.
/// Named after the magical tomes in Frieren — contains all the "spells" (config values) needed.
/// </summary>
public static class Grimoire
{
    // ============================================================
    // Application Identity
    // ============================================================

    /// <summary>Application display name</summary>
    public const string AppName = "Zolt-Dump";

    /// <summary>Application description</summary>
    public const string AppDescription = "Godot Engine Object Dumper";

    // ============================================================
    // Named Pipe — Fern (費倫) communication channel
    // ============================================================

    /// <summary>Named pipe for DLL ↔ UI communication (full path)</summary>
    public const string PipeName = @"\\.\pipe\ZoltDumpBfx";

    /// <summary>Named pipe bare name (without \\.\pipe\ prefix)</summary>
    public const string PipeNameBare = "ZoltDumpBfx";

    /// <summary>Pipe buffer size (bytes)</summary>
    public const int PipeBufferSize = 65536;

    // ============================================================
    // AOBMaker Bridge — integration with AOBMaker tool
    // ============================================================

    /// <summary>AOBMaker's named pipe for sending addresses</summary>
    public const string AobMakerPipeName = @"\\.\pipe\AOBMaker";

    // ============================================================
    // Logging — Solliter (索莉緹爾) trace concealment
    // ============================================================

    /// <summary>Log directory name under %LOCALAPPDATA%</summary>
    public const string LogDirName = "ZoltDump";

    // ============================================================
    // Single Instance — Mutex
    // ============================================================

    /// <summary>Global mutex name for single instance enforcement</summary>
    public const string MutexName = @"Global\ZoltDump_SingleInstance";

    // ============================================================
    // Hint Cache — Himmel (欣梅爾) remembered wisdom
    // ============================================================

    /// <summary>Hint cache file prefix</summary>
    public const string HintCachePrefix = "ZoltDump";

    // ============================================================
    // CE Export — Cheat Engine integration constants
    // ============================================================

    /// <summary>CE XML export clipboard format identifier</summary>
    public const string CeXmlClipboardFormat = "text/xml";

    // ============================================================
    // Proxy DLL — Eisen (艾乍) armored defense
    // ============================================================

    /// <summary>Main DLL filename</summary>
    public const string DllFileName = "ZoltDumper.dll";

    /// <summary>Proxy DLL filename (version.dll)</summary>
    public const string ProxyDllFileName = "version.dll";
}
