/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
using Xunit;
using ZoltDumpUI.Core;

namespace ZoltDumpUI.Tests;

public class AddressHelperTests
{
    // ================================================================
    // FormatAddress
    // ================================================================

    [Theory]
    [InlineData("7FF742C84100", AddressFormat.HexNoPrefix, "", "7FF742C84100")]
    [InlineData("7FF742C84100", AddressFormat.HexWithPrefix, "", "0x7FF742C84100")]
    [InlineData("0x7FF742C84100", AddressFormat.HexNoPrefix, "", "7FF742C84100")]
    [InlineData("0X00ABC", AddressFormat.HexNoPrefix, "", "ABC")]
    public void FormatAddress_BasicFormats(string raw, AddressFormat fmt, string mod, string expected)
    {
        Assert.Equal(expected, AddressHelper.FormatAddress(raw, fmt, mod));
    }

    [Fact]
    public void FormatAddress_ModuleOffset()
    {
        var result = AddressHelper.FormatAddress("ABC", AddressFormat.ModuleOffset, "game.exe");
        Assert.Equal("\"game.exe\"+ABC", result);
    }

    [Fact]
    public void FormatAddress_ModuleOffset_FallsBackWhenNoModule()
    {
        var result = AddressHelper.FormatAddress("ABC", AddressFormat.ModuleOffset, "");
        Assert.Equal("ABC", result);
    }

    [Fact]
    public void FormatAddress_EmptyInput_ReturnsEmpty()
    {
        Assert.Equal("", AddressHelper.FormatAddress("", AddressFormat.HexNoPrefix));
        Assert.Equal("", AddressHelper.FormatAddress(null!, AddressFormat.HexNoPrefix));
    }

    [Fact]
    public void FormatAddress_AllZeros_ReturnsZero()
    {
        Assert.Equal("0", AddressHelper.FormatAddress("0000", AddressFormat.HexNoPrefix));
        Assert.Equal("0x0", AddressHelper.FormatAddress("0000", AddressFormat.HexWithPrefix));
    }

    // ================================================================
    // NormalizeAddress
    // ================================================================

    [Theory]
    [InlineData("0x7FF742C84100", "7FF742C84100")]
    [InlineData("0X00ABC", "ABC")]
    [InlineData("  ABC  ", "ABC")]
    [InlineData("DEAD", "DEAD")]
    public void NormalizeAddress_BasicCases(string input, string expected)
    {
        Assert.Equal(expected, AddressHelper.NormalizeAddress(input));
    }

    [Fact]
    public void NormalizeAddress_ModuleOffset_ExtractsOffset()
    {
        Assert.Equal("ABC", AddressHelper.NormalizeAddress("\"game.exe\"+ABC"));
        Assert.Equal("1234", AddressHelper.NormalizeAddress("\"module.dll\"+0x1234"));
    }

    [Fact]
    public void NormalizeAddress_EmptyInput_ReturnsEmpty()
    {
        Assert.Equal("", AddressHelper.NormalizeAddress(""));
        Assert.Equal("", AddressHelper.NormalizeAddress("   "));
        Assert.Equal("", AddressHelper.NormalizeAddress(null!));
    }
}
