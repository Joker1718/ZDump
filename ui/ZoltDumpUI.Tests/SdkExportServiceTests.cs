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

public class SdkExportServiceTests
{
    private readonly SdkExportService _service = new();

    [Fact]
    public void GenerateClassHeader_SimpleClass()
    {
        var fields = new List<LiveFieldValue>
        {
            new() { Name = "health", Type = "INT", Offset = 0x10, Size = 4 },
            new() { Name = "position", Type = "VECTOR3", Offset = 0x14, Size = 12 },
        };

        var result = _service.GenerateClassHeader("Player", "Node2D", fields);

        Assert.Contains("struct Player : public Node2D", result);
        Assert.Contains("int32_t health; // 0x10 (Size: 0x4)", result);
        Assert.Contains("godot::Vector3 position; // 0x14 (Size: 0xC)", result);
    }

    [Fact]
    public void GenerateClassHeader_InsertsPadding()
    {
        var fields = new List<LiveFieldValue>
        {
            new() { Name = "value", Type = "INT", Offset = 0x20, Size = 4 },
        };

        var result = _service.GenerateClassHeader("MyClass", "", fields);

        Assert.Contains("char _pad_0x0[0x20]; // 0x0", result);
        Assert.Contains("struct MyClass", result);
        Assert.DoesNotContain("public", result); // No super
    }

    [Fact]
    public void GenerateClassHeader_EmptyFields()
    {
        var result = _service.GenerateClassHeader("Empty", "", []);
        Assert.Contains("struct Empty", result);
        Assert.Contains("{", result);
        Assert.Contains("};", result);
    }

    [Fact]
    public void GenerateClassHeader_AllGodotTypes()
    {
        var types = new (string type, int size, string expected)[]
        {
            ("BOOL", 1, "bool"),
            ("INT", 1, "int8_t"),
            ("INT", 2, "int16_t"),
            ("INT", 4, "int32_t"),
            ("INT", 8, "int64_t"),
            ("FLOAT", 4, "float"),
            ("FLOAT", 8, "double"),
            ("STRING", 8, "godot::String"),
            ("STRING_NAME", 8, "godot::StringName"),
            ("NODE_PATH", 8, "godot::NodePath"),
            ("VECTOR2", 8, "godot::Vector2"),
            ("VECTOR3", 12, "godot::Vector3"),
            ("COLOR", 16, "godot::Color"),
            ("OBJECT", 8, "godot::Object*"),
            ("ARRAY", 8, "godot::Array"),
            ("DICTIONARY", 8, "godot::Dictionary"),
        };

        int offset = 0;
        var fields = types.Select(t =>
        {
            var f = new LiveFieldValue { Name = $"f_{t.type}", Type = t.type, Offset = offset, Size = t.size };
            offset += t.size;
            return f;
        }).ToList();

        var result = _service.GenerateClassHeader("AllTypes", "", fields);

        foreach (var (_, _, expected) in types)
        {
            Assert.Contains(expected, result);
        }
    }

    [Fact]
    public void GenerateClassHeader_SanitizesNames()
    {
        var fields = new List<LiveFieldValue>
        {
            new() { Name = "my field/path", Type = "INT", Offset = 0, Size = 4 },
            new() { Name = "123start", Type = "INT", Offset = 4, Size = 4 },
        };

        var result = _service.GenerateClassHeader("Test", "", fields);

        Assert.Contains("my_field_path", result);
        Assert.Contains("_123start", result);
    }

    [Fact]
    public void GenerateClassHeaderFromSchema_Works()
    {
        var classInfo = new ClassInfoModel
        {
            ClassName = "Sprite2D",
            SuperName = "Node2D",
            Properties =
            [
                new FieldInfoModel { Name = "texture", Type = "OBJECT", Offset = 0x100, Size = 8 },
                new FieldInfoModel { Name = "flip_h", Type = "BOOL", Offset = 0x108, Size = 1 },
            ]
        };

        var result = _service.GenerateClassHeaderFromSchema(classInfo);

        Assert.Contains("struct Sprite2D : public Node2D", result);
        Assert.Contains("godot::Object* texture;", result);
        Assert.Contains("bool flip_h;", result);
    }
}
