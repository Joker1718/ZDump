/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
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
