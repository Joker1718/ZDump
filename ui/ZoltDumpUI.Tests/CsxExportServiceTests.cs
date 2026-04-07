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
using ZoltDumpUI.Models;
using ZoltDumpUI.Services;

namespace ZoltDumpUI.Tests;

public class CsxExportServiceTests
{
    private readonly CsxExportService _service = new(null!);

    [Fact]
    public async Task GenerateCsx_EmptyFields_HasStructure()
    {
        var result = await _service.GenerateCsxAsync("Player", []);

        Assert.Contains("<Structures>", result);
        Assert.Contains("Name=\"Player\"", result);
        Assert.Contains("</Structures>", result);
    }

    [Fact]
    public async Task GenerateCsx_ScalarFields()
    {
        var fields = new List<LiveFieldValue>
        {
            new() { Name = "health", Type = "INT", Offset = 0x10, Size = 4 },
            new() { Name = "speed", Type = "FLOAT", Offset = 0x14, Size = 4 },
            new() { Name = "alive", Type = "BOOL", Offset = 0x18, Size = 1 },
        };

        var result = await _service.GenerateCsxAsync("Player", fields);

        Assert.Contains("Offset=\"16\"", result);  // 0x10
        Assert.Contains("Vartype=\"4 Bytes\"", result);
        Assert.Contains("Description=\"health\"", result);

        Assert.Contains("Offset=\"20\"", result);  // 0x14
        Assert.Contains("Vartype=\"Float\"", result);
        Assert.Contains("Description=\"speed\"", result);

        Assert.Contains("Offset=\"24\"", result);  // 0x18
        Assert.Contains("Vartype=\"Byte\"", result);
        Assert.Contains("Description=\"alive\"", result);
    }

    [Fact]
    public async Task GenerateCsx_CompositeFlattening()
    {
        var fields = new List<LiveFieldValue>
        {
            new() { Name = "pos", Type = "VECTOR3", Offset = 0x20, Size = 12 },
        };

        var result = await _service.GenerateCsxAsync("Node", fields);

        // VECTOR3 should flatten into pos.x, pos.y, pos.z
        Assert.Contains("Description=\"pos.x\"", result);
        Assert.Contains("Description=\"pos.y\"", result);
        Assert.Contains("Description=\"pos.z\"", result);
        Assert.Contains("Vartype=\"Float\"", result);

        // Offsets: base(0x20=32) + 0, 4, 8
        Assert.Contains("Offset=\"32\"", result);
        Assert.Contains("Offset=\"36\"", result);
        Assert.Contains("Offset=\"40\"", result);
    }

    [Fact]
    public async Task GenerateCsx_IntegerComposite_ShowsSigned()
    {
        var fields = new List<LiveFieldValue>
        {
            new() { Name = "grid", Type = "VECTOR2I", Offset = 0, Size = 8 },
        };

        var result = await _service.GenerateCsxAsync("Tile", fields);

        Assert.Contains("grid.x", result);
        Assert.Contains("grid.y", result);
        Assert.Contains("Vartype=\"4 Bytes\"", result);
        Assert.Contains("signed integer", result);
    }

    [Fact]
    public async Task GenerateCsx_PointerField()
    {
        var fields = new List<LiveFieldValue>
        {
            new() { Name = "parent", Type = "OBJECT", Offset = 0x30, Size = 8, IsPointer = true },
        };

        var result = await _service.GenerateCsxAsync("Child", fields);

        Assert.Contains("Vartype=\"Pointer\"", result);
        Assert.Contains("Bytesize=\"8\"", result);
        Assert.Contains("hexadecimal", result);
    }

    [Fact]
    public async Task GenerateCsx_SkipsNegativeOffset()
    {
        var fields = new List<LiveFieldValue>
        {
            new() { Name = "bad", Type = "INT", Offset = -1, Size = 4 },
            new() { Name = "good", Type = "INT", Offset = 0x10, Size = 4 },
        };

        var result = await _service.GenerateCsxAsync("Test", fields);

        Assert.DoesNotContain("bad", result);
        Assert.Contains("good", result);
    }

    [Fact]
    public async Task GenerateCsx_InlineStruct_Recurses()
    {
        var fields = new List<LiveFieldValue>
        {
            new()
            {
                Name = "data",
                Type = "CUSTOM",
                Offset = 0x10,
                Size = 8,
                IsStruct = true,
                SubFields =
                [
                    new() { Name = "x", Type = "INT", Offset = 0, Size = 4 },
                    new() { Name = "y", Type = "INT", Offset = 4, Size = 4 },
                ]
            }
        };

        var result = await _service.GenerateCsxAsync("Obj", fields);

        Assert.Contains("data.x", result);
        Assert.Contains("data.y", result);
    }

    [Fact]
    public async Task GenerateCsx_XmlEscaping()
    {
        var fields = new List<LiveFieldValue>
        {
            new() { Name = "a<b>&c", Type = "INT", Offset = 0, Size = 4 },
        };

        var result = await _service.GenerateCsxAsync("Test", fields);

        Assert.Contains("a&lt;b&gt;&amp;c", result);
    }
}
