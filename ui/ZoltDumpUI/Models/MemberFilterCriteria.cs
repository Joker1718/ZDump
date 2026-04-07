/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
namespace ZoltDumpUI.Models;

/// <summary>
/// Value-based filter criteria for CMD_FIND_INSTANCE_WITH_MEMBER (GDScript)
/// or CMD_FIND_MANAGED_INSTANCES (.NET managed objects).
/// Used to disambiguate when multiple objects share the same member name.
/// </summary>
public sealed class MemberFilterCriteria
{
    /// <summary>Target member name (the one to resolve address for).</summary>
    public string TargetMember { get; init; } = "";

    /// <summary>FilterOp for target member value (0=none).</summary>
    public int TargetOp { get; set; }

    /// <summary>Comparison value for target member.</summary>
    public long TargetValue { get; set; }

    /// <summary>Secondary filter member name (empty = no filter, GDScript mode).</summary>
    public string FilterMember { get; set; } = "";

    /// <summary>FilterOp for filter member value (0=none).</summary>
    public int FilterOp { get; set; }

    /// <summary>Comparison value for filter member.</summary>
    public long FilterValue { get; set; }

    // ── .NET managed mode fields ──

    /// <summary>True when operating on .NET managed fields (by offset, not GDScript member name).</summary>
    public bool IsManagedMode { get; init; }

    /// <summary>Find ALL matching instances (multi-result) instead of first match only.</summary>
    public bool FindAll { get; init; }

    /// <summary>Managed: byte offset of the target field within .NET object.</summary>
    public int TargetFieldOffset { get; init; }

    /// <summary>Managed: byte offset of the filter field within .NET object (-1 = no filter).</summary>
    public int FilterFieldOffset { get; init; } = -1;

    /// <summary>Managed: read size for filter field (1, 2, 4, or 8 bytes).</summary>
    public int FilterFieldSize { get; init; } = 4;

    // ── .NET reflection info (for update-resilient scripts) ──

    /// <summary>Fully qualified .NET type name (e.g. "MegaCrit.Sts2.Core.Entities.Creatures.Creature").</summary>
    public string DotNetTypeName { get; set; } = "";

    /// <summary>.NET field name for the target field (e.g. "_currentHp", "&lt;Side&gt;k__BackingField").</summary>
    public string TargetFieldName { get; set; } = "";

    /// <summary>.NET field name for the filter field (e.g. "&lt;Side&gt;k__BackingField").</summary>
    public string FilterFieldName { get; set; } = "";
}
