using Avalonia;
using Avalonia.Controls;

namespace ZoltDumpUI.Core;

/// <summary>
/// Grimoire — 魔導書 (Grimoire)
/// String resource loader from en.axaml dictionary.
/// </summary>
public static class Res
{
    public static string Get(string key)
    {
        if (Application.Current is { } app && app.TryGetResource(key, null, out var value) && value is string s)
            return s;
        return $"[{key}]";
    }

    public static string Format(string key, params object[] args)
    {
        var template = Get(key);
        try { return string.Format(template, args); }
        catch { return template; }
    }
}
