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
using ZoltDumpUI.Services;
using ZoltDumpUI.Tests.Fakes;

namespace ZoltDumpUI.Tests;

/// <summary>
/// Tests DumpService JSON parsing by feeding fake pipe responses.
/// Verifies JSON → Model conversion for all major commands.
/// </summary>
public class DumpServiceParseTests
{
    private readonly FakePipeClient _pipe = new();
    private readonly DumpService _svc;

    public DumpServiceParseTests()
    {
        _svc = new DumpService(_pipe);
    }

    // ================================================================
    // InitAsync
    // ================================================================

    [Fact]
    public async Task InitAsync_ParsesAllFields()
    {
        _pipe.Enqueue("""
        {
            "ok": true,
            "godot_version": "4.3.0.stable",
            "process_name": "Brotato.exe",
            "pid": 12345,
            "module_path": "C:\\Games\\Brotato",
            "scan_method": "AOB",
            "error": ""
        }
        """);

        var result = await _svc.InitAsync();

        Assert.True(result.Ok);
        Assert.Equal("4.3.0.stable", result.GodotVersion);
        Assert.Equal("Brotato.exe", result.ProcessName);
        Assert.Equal(12345, result.Pid);
        Assert.Equal("AOB", result.ScanMethod);
    }

    [Fact]
    public async Task InitAsync_HandlesError()
    {
        _pipe.Enqueue("""{ "ok": false, "error": "scan failed" }""");

        var result = await _svc.InitAsync();

        Assert.False(result.Ok);
        Assert.Equal("scan failed", result.Error);
    }

    // ================================================================
    // GetPointersAsync
    // ================================================================

    [Fact]
    public async Task GetPointers_ParsesAddresses()
    {
        _pipe.Enqueue("""
        {
            "object_db": "7FF6A1234560",
            "class_db": "7FF6A1234570",
            "scene_tree": "7FF6A1234580",
            "engine": "7FF6A1234590",
            "slot_count": 5000,
            "slot_max": 10000,
            "object_count": 4200,
            "object_db_method": "AOB",
            "class_db_method": "AOB",
            "scene_tree_method": "AOB",
            "object_db_pattern_id": "P001",
            "class_db_pattern_id": "P002",
            "scene_tree_pattern_id": "P003",
            "object_db_scan_addr": "7FF6A0000100",
            "class_db_scan_addr": "7FF6A0000200",
            "scene_tree_scan_addr": "7FF6A0000300"
        }
        """);

        var result = await _svc.GetPointersAsync();

        Assert.Equal("7FF6A1234560", result.ObjectDb);
        Assert.Equal("7FF6A1234570", result.ClassDb);
        Assert.Equal("7FF6A1234580", result.SceneTree);
        Assert.Equal(4200, result.ObjectCount);
        Assert.Equal(10000, result.SlotMax);
        Assert.Equal("P001", result.ObjectDbPatternId);
    }

    // ================================================================
    // GetObjectListAsync
    // ================================================================

    [Fact]
    public async Task GetObjectList_ParsesObjects()
    {
        _pipe.Enqueue("""
        {
            "total": 100,
            "offset": 0,
            "scanned": 200,
            "objects": [
                { "address": "1A2B", "name": "Player", "class_name": "CharacterBody2D", "slot_index": 5, "object_id": 12345, "parent_addr": "1A00" },
                { "address": "1A3C", "name": "Enemy", "class_name": "Node2D", "slot_index": 6, "object_id": 12346, "parent_addr": "1A00" }
            ]
        }
        """);

        var result = await _svc.GetObjectListAsync();

        Assert.Equal(100, result.Total);
        Assert.Equal(200, result.Scanned);
        Assert.Equal(2, result.Count);
        Assert.Equal("Player", result.Objects[0].Name);
        Assert.Equal("CharacterBody2D", result.Objects[0].ClassName);
        Assert.Equal(12345, result.Objects[0].ObjectId);
    }

    [Fact]
    public async Task GetObjectList_EmptyArray()
    {
        _pipe.Enqueue("""{ "total": 0, "offset": 0, "scanned": 0, "objects": [] }""");

        var result = await _svc.GetObjectListAsync();

        Assert.Empty(result.Objects);
        Assert.Equal(0, result.Count);
    }

    // ================================================================
    // GetObjectAsync
    // ================================================================

    [Fact]
    public async Task GetObject_ParsesAllFields()
    {
        _pipe.Enqueue("""
        {
            "address": "1A2B3C",
            "name": "WaveTimer",
            "class_name": "Timer",
            "class_addr": "7FF6A100",
            "parent_addr": "1A0000",
            "parent_name": "Main",
            "node_path": "/root/Main/WaveTimer",
            "object_id": 9999,
            "slot_index": 42,
            "is_node": true,
            "is_ref_counted": false
        }
        """);

        var result = await _svc.GetObjectAsync("1A2B3C");

        Assert.Equal("WaveTimer", result.Name);
        Assert.Equal("Timer", result.ClassName);
        Assert.Equal("/root/Main/WaveTimer", result.NodePath);
        Assert.True(result.IsNode);
        Assert.False(result.IsRefCounted);
    }

    // ================================================================
    // FindByAddressAsync
    // ================================================================

    [Fact]
    public async Task FindByAddress_ParsesMatch()
    {
        _pipe.Enqueue("""
        {
            "found": true,
            "match_type": "exact",
            "address": "1A2B3C",
            "slot_index": 10,
            "name": "Player",
            "class_name": "CharacterBody2D",
            "parent_addr": "1A0000",
            "member_offset": 256
        }
        """);

        var result = await _svc.FindByAddressAsync("1A2B3C");

        Assert.True(result.Found);
        Assert.Equal("exact", result.MatchType);
        Assert.Equal("Player", result.Name);
        Assert.Equal(256, result.OffsetFromBase);
    }

    [Fact]
    public async Task FindByAddress_NotFound()
    {
        _pipe.Enqueue("""{ "found": false }""");

        var result = await _svc.FindByAddressAsync("DEAD");

        Assert.False(result.Found);
    }

    // ================================================================
    // WalkClassAsync
    // ================================================================

    [Fact]
    public async Task WalkClass_ParsesPropertiesAndMethods()
    {
        _pipe.Enqueue("""
        {
            "class_name": "Timer",
            "super_name": "Node",
            "class_addr": "7FF6A100",
            "properties": [
                { "name": "wait_time", "type": "FLOAT", "offset": 472, "size": 8, "bit_shift": 0, "bit_mask": 0, "hint": 0, "hint_string": "", "usage": 7 },
                { "name": "one_shot", "type": "BOOL", "offset": 480, "size": 1, "hint": 0, "hint_string": "", "usage": 7 }
            ],
            "methods": [
                {
                    "name": "start",
                    "return_type": "void",
                    "address": "7FF6A200",
                    "flags": 1,
                    "args": [
                        { "name": "time_sec", "type": "FLOAT" }
                    ]
                }
            ]
        }
        """);

        var result = await _svc.WalkClassAsync("Timer");

        Assert.Equal("Timer", result.ClassName);
        Assert.Equal("Node", result.SuperName);
        Assert.Equal(2, result.PropertiesCount);
        Assert.Equal(1, result.MethodCount);
        Assert.Equal("wait_time", result.Properties[0].Name);
        Assert.Equal(472, result.Properties[0].Offset);
        Assert.Equal("start", result.Methods[0].Name);
        Assert.Single(result.Methods[0].Args);
        Assert.Equal("time_sec", result.Methods[0].Args[0].Name);
    }

    // ================================================================
    // WalkInstanceAsync
    // ================================================================

    [Fact]
    public async Task WalkInstance_ParsesFields()
    {
        _pipe.Enqueue("""
        {
            "address": "1A2B",
            "name": "Player",
            "class_name": "CharacterBody2D",
            "parent_addr": "1A00",
            "is_node": true,
            "fields": [
                {
                    "name": "HP",
                    "type": "int",
                    "value": "100",
                    "hex": "64000000",
                    "address": "1A3B",
                    "offset": -1,
                    "size": 8,
                    "is_pointer": false,
                    "is_struct": false,
                    "is_container": false,
                    "is_editable": true,
                    "variant_type": 2
                }
            ]
        }
        """);

        var result = await _svc.WalkInstanceAsync("1A2B");

        Assert.Equal("Player", result.Name);
        Assert.True(result.IsNode);
        Assert.Single(result.Fields);
        Assert.Equal("HP", result.Fields[0].Name);
        Assert.Equal("100", result.Fields[0].Value);
        Assert.Equal(-1, result.Fields[0].Offset);
        Assert.True(result.Fields[0].IsEditable);
    }

    [Fact]
    public async Task WalkInstance_ParsesSubFields()
    {
        _pipe.Enqueue("""
        {
            "address": "1A2B",
            "name": "Obj",
            "class_name": "Node2D",
            "parent_addr": "",
            "is_node": true,
            "fields": [
                {
                    "name": "position",
                    "type": "VECTOR2",
                    "value": "(100, 200)",
                    "offset": 200,
                    "size": 8,
                    "is_struct": true,
                    "sub_fields": [
                        { "name": "x", "type": "FLOAT", "value": "100", "offset": 200, "size": 4 },
                        { "name": "y", "type": "FLOAT", "value": "200", "offset": 204, "size": 4 }
                    ]
                }
            ]
        }
        """);

        var result = await _svc.WalkInstanceAsync("1A2B");

        Assert.True(result.Fields[0].IsStruct);
        Assert.Equal(2, result.Fields[0].SubFields.Count);
        Assert.Equal("x", result.Fields[0].SubFields[0].Name);
        Assert.Equal("100", result.Fields[0].SubFields[0].Value);
    }

    [Fact]
    public async Task WalkInstance_ParsesContainerInfo()
    {
        _pipe.Enqueue("""
        {
            "address": "1A2B",
            "name": "Obj",
            "class_name": "Node",
            "parent_addr": "",
            "is_node": true,
            "fields": [
                {
                    "name": "weapons",
                    "type": "Array",
                    "value": "Array[3]",
                    "offset": 100,
                    "size": 8,
                    "is_container": true,
                    "element_count": 3,
                    "element_type": "Object",
                    "variant_type": 28
                }
            ]
        }
        """);

        var result = await _svc.WalkInstanceAsync("1A2B");

        Assert.True(result.Fields[0].IsContainer);
        Assert.Equal(3, result.Fields[0].ElementCount);
        Assert.Equal("Object", result.Fields[0].ElementType);
        Assert.Equal(28, result.Fields[0].VariantType);
    }

    // ================================================================
    // WalkContainerAsync
    // ================================================================

    [Fact]
    public async Task WalkContainer_ParsesElements()
    {
        _pipe.Enqueue("""
        {
            "address": "1A2B",
            "container_type": "Array",
            "element_count": 2,
            "element_type": "int",
            "key_type": "",
            "fields": [
                { "name": "[0]", "type": "int", "value": "42", "offset": 0, "size": 8 },
                { "name": "[1]", "type": "int", "value": "99", "offset": 1, "size": 8 }
            ]
        }
        """);

        var result = await _svc.WalkContainerAsync("1A2B", 28);

        Assert.Equal("Array", result.ContainerType);
        Assert.Equal(2, result.ElementCount);
        Assert.Equal("int", result.ElementType);
        Assert.Equal(2, result.Fields.Count);
        Assert.Equal("[0]", result.Fields[0].Name);
        Assert.Equal("42", result.Fields[0].Value);
    }

    // ================================================================
    // FindInstancesAsync
    // ================================================================

    [Fact]
    public async Task FindInstances_ParsesInstances()
    {
        _pipe.Enqueue("""
        {
            "class_name": "Timer",
            "count": 3,
            "instances": [
                { "address": "AA", "name": "T1", "class_name": "Timer", "slot_index": 1, "object_id": 100, "parent_addr": "00" },
                { "address": "BB", "name": "T2", "class_name": "Timer", "slot_index": 2, "object_id": 101, "parent_addr": "00" },
                { "address": "CC", "name": "T3", "class_name": "Timer", "slot_index": 3, "object_id": 102, "parent_addr": "00" }
            ]
        }
        """);

        var result = await _svc.FindInstancesAsync("Timer");

        Assert.Equal("Timer", result.ClassName);
        Assert.Equal(3, result.Count);
        Assert.Equal(3, result.Instances.Count);
        Assert.Equal("T2", result.Instances[1].Name);
    }

    // ================================================================
    // ListClassesAsync
    // ================================================================

    [Fact]
    public async Task ListClasses_ParsesClassList()
    {
        _pipe.Enqueue("""
        {
            "classes": [
                { "class_name": "Player", "super_name": "Node2D", "property_count": 5, "method_count": 2, "is_game_class": true, "score": 10 },
                { "class_name": "Node", "super_name": "Object", "property_count": 20, "method_count": 50, "is_game_class": false, "score": 0 }
            ]
        }
        """);

        var result = await _svc.ListClassesAsync();

        Assert.Equal(2, result.Count);
        Assert.Equal("Player", result[0].ClassName);
        Assert.True(result[0].IsGameClass);
        Assert.Equal(10, result[0].Score);
        Assert.False(result[1].IsGameClass);
    }

    // ================================================================
    // SearchPropertiesAsync
    // ================================================================

    [Fact]
    public async Task SearchProperties_ParsesHits()
    {
        _pipe.Enqueue("""
        {
            "hits": [
                { "class_name": "Timer", "super_name": "Node", "property_name": "time_left", "property_type": "FLOAT", "offset": 480, "size": 8, "preview": "" },
                { "class_name": "AnimationPlayer", "super_name": "Node", "property_name": "current_animation_length", "property_type": "FLOAT", "offset": 300, "size": 8, "preview": "2.5" }
            ]
        }
        """);

        var result = await _svc.SearchPropertiesAsync("time");

        Assert.Equal(2, result.Count);
        Assert.Equal("time_left", result.Hits[0].PropertyName);
        Assert.Equal("FLOAT", result.Hits[0].PropertyType);
        Assert.Equal(480, result.Hits[0].Offset);
        Assert.Equal("2.5", result.Hits[1].Preview);
    }

    // ================================================================
    // WalkTreeAsync
    // ================================================================

    [Fact]
    public async Task WalkTree_ParsesNestedNodes()
    {
        _pipe.Enqueue("""
        {
            "root": {
                "address": "1000",
                "name": "root",
                "class_name": "Node",
                "node_path": "/root",
                "child_count": 1,
                "children": [
                    {
                        "address": "2000",
                        "name": "Main",
                        "class_name": "Node2D",
                        "node_path": "/root/Main",
                        "child_count": 0,
                        "children": []
                    }
                ]
            }
        }
        """);

        var result = await _svc.WalkTreeAsync();

        Assert.NotNull(result.Root);
        Assert.Equal("root", result.Root!.Name);
        Assert.Single(result.Root.Children);
        Assert.Equal("Main", result.Root.Children[0].Name);
        Assert.Equal("/root/Main", result.Root.Children[0].NodePath);
    }

    [Fact]
    public async Task WalkTree_NullRoot()
    {
        _pipe.Enqueue("""{ }""");

        var result = await _svc.WalkTreeAsync();

        Assert.Null(result.Root);
    }

    // ================================================================
    // ReadMemAsync / WriteMemAsync
    // ================================================================

    [Fact]
    public async Task ReadMem_ParsesHexData()
    {
        _pipe.Enqueue("""{ "data": "48656C6C6F" }""");

        var result = await _svc.ReadMemAsync("1000", 5);

        Assert.Equal(5, result.Length);
        Assert.Equal((byte)'H', result[0]);
        Assert.Equal((byte)'o', result[4]);
    }

    [Fact]
    public async Task WriteMem_ReturnsOk()
    {
        _pipe.Enqueue("""{ "ok": true }""");

        var result = await _svc.WriteMemAsync("1000", [0x42]);

        Assert.True(result);
        Assert.Equal("write_mem", _pipe.Sent[0].Command);
    }

    // ================================================================
    // GetScanStatusAsync
    // ================================================================

    [Fact]
    public async Task GetScanStatus_ParsesFields()
    {
        _pipe.Enqueue("""
        {
            "status": "scanning",
            "progress": 50,
            "total": 100,
            "message": "Scanning objects...",
            "is_complete": false
        }
        """);

        var result = await _svc.GetScanStatusAsync();

        Assert.Equal("scanning", result.Status);
        Assert.Equal(50, result.Progress);
        Assert.Equal(100, result.Total);
        Assert.False(result.IsComplete);
    }

    // ================================================================
    // Edge cases — missing fields default gracefully
    // ================================================================

    [Fact]
    public async Task ParseObjectNode_MissingFields_DefaultsGracefully()
    {
        _pipe.Enqueue("""{ "address": "1A2B" }""");

        var result = await _svc.GetObjectAsync("1A2B");

        Assert.Equal("1A2B", result.Address);
        Assert.Equal("", result.Name);
        Assert.Equal("", result.ClassName);
        Assert.False(result.IsNode);
    }

    [Fact]
    public async Task ParseLiveField_MissingFields_DefaultsGracefully()
    {
        _pipe.Enqueue("""
        {
            "address": "1A2B",
            "name": "",
            "class_name": "",
            "parent_addr": "",
            "is_node": false,
            "fields": [
                { "name": "x", "type": "int" }
            ]
        }
        """);

        var result = await _svc.WalkInstanceAsync("1A2B");

        Assert.Single(result.Fields);
        Assert.Equal("x", result.Fields[0].Name);
        Assert.Equal("", result.Fields[0].Value);
        Assert.Equal(0, result.Fields[0].Offset);
        Assert.False(result.Fields[0].IsPointer);
        Assert.False(result.Fields[0].IsContainer);
    }

    // ================================================================
    // Pipe command verification
    // ================================================================

    [Fact]
    public async Task SendsCorrectCommand_GetObjectList()
    {
        _pipe.Enqueue("""{ "total": 0, "objects": [] }""");

        await _svc.GetObjectListAsync(offset: 10, limit: 50);

        Assert.Equal("get_object_list", _pipe.Sent[0].Command);
        Assert.Equal(10, _pipe.Sent[0].Payload!["offset"]!.GetValue<int>());
        Assert.Equal(50, _pipe.Sent[0].Payload!["limit"]!.GetValue<int>());
    }

    [Fact]
    public async Task SendsCorrectCommand_FindInstances()
    {
        _pipe.Enqueue("""{ "class_name": "Timer", "count": 0, "instances": [] }""");

        await _svc.FindInstancesAsync("Timer", exactMatch: true);

        Assert.Equal("find_instances", _pipe.Sent[0].Command);
        Assert.Equal("Timer", _pipe.Sent[0].Payload!["class_name"]!.GetValue<string>());
        Assert.True(_pipe.Sent[0].Payload!["exact"]!.GetValue<bool>());
    }

    [Fact]
    public async Task SendsCorrectCommand_WalkContainer()
    {
        _pipe.Enqueue("""{ "address": "1A2B", "container_type": "Array", "element_count": 0, "fields": [] }""");

        await _svc.WalkContainerAsync("1A2B", 28, offset: 5, limit: 50);

        Assert.Equal("walk_container", _pipe.Sent[0].Command);
        Assert.Equal(28, _pipe.Sent[0].Payload!["variant_type"]!.GetValue<int>());
        Assert.Equal(5, _pipe.Sent[0].Payload!["offset"]!.GetValue<int>());
        Assert.Equal(50, _pipe.Sent[0].Payload!["limit"]!.GetValue<int>());
    }
}
