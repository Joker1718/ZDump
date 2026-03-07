using System.Globalization;

namespace ZoltDumpUI.Core;

/// <summary>Address format and conversion utilities.</summary>
public enum AddressFormat
{
    HexNoPrefix,
    HexWithPrefix,
    ModuleOffset
}

public static class AddressHelper
{
    public static string FormatAddress(string rawHex, AddressFormat format, string moduleName = "")
    {
        if (string.IsNullOrEmpty(rawHex)) return "";

        var clean = rawHex.Replace("0x", "").Replace("0X", "").TrimStart('0');
        if (clean.Length == 0) clean = "0";

        return format switch
        {
            AddressFormat.HexWithPrefix => "0x" + clean.ToUpperInvariant(),
            AddressFormat.ModuleOffset when !string.IsNullOrEmpty(moduleName) =>
                $"\"{moduleName}\"+{clean.ToUpperInvariant()}",
            _ => clean.ToUpperInvariant()
        };
    }

    public static string NormalizeAddress(string input)
    {
        if (string.IsNullOrWhiteSpace(input)) return "";

        var s = input.Trim();

        // Handle "module.exe"+offset format
        var plusIdx = s.IndexOf('+');
        if (plusIdx > 0 && s.Contains('"'))
        {
            s = s[(plusIdx + 1)..].Trim();
        }

        s = s.Replace("0x", "").Replace("0X", "");

        if (ulong.TryParse(s, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var addr))
        {
            return addr.ToString("X");
        }

        return s.ToUpperInvariant();
    }
}
