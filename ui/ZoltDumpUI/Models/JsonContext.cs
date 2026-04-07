/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
using System.Text.Json.Serialization;

namespace ZoltDumpUI.Models;

/// <summary>
/// Source-generated JSON serialization context for pipe protocol models.
/// Used for AOT-compatible JSON serialization.
/// </summary>
[JsonSerializable(typeof(EngineState))]
[JsonSerializable(typeof(PointerState))]
[JsonSerializable(typeof(ObjectListResult))]
[JsonSerializable(typeof(ObjectEntry))]
[JsonSerializable(typeof(ObjectNode))]
[JsonSerializable(typeof(ClassInfoModel))]
[JsonSerializable(typeof(FieldInfoModel))]
[JsonSerializable(typeof(MethodInfoEntry))]
[JsonSerializable(typeof(MethodArgEntry))]
[JsonSerializable(typeof(TreeWalkResult))]
[JsonSerializable(typeof(TreeNodeInfo))]
[JsonSerializable(typeof(InstanceWalkResult))]
[JsonSerializable(typeof(LiveFieldValue))]
[JsonSerializable(typeof(FindInstancesResult))]
[JsonSerializable(typeof(AddressLookupResult))]
[JsonSerializable(typeof(CePointerInfo))]
[JsonSerializable(typeof(BreadcrumbEntry))]
[JsonSerializable(typeof(PropertySearchResult))]
[JsonSerializable(typeof(PropertySearchHit))]
[JsonSerializable(typeof(ClassListItem))]
[JsonSerializable(typeof(ScanStatusResult))]
[JsonSourceGenerationOptions(
    PropertyNamingPolicy = JsonKnownNamingPolicy.CamelCase,
    DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull)]
internal partial class ZoltJsonContext : JsonSerializerContext;
