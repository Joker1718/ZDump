/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
// Test types that simulate a Godot .NET game's class hierarchy.
// These are used to verify that ZoltReflector can correctly enumerate
// fields, offsets, and type layouts.

using System.Runtime.InteropServices;

namespace ZoltReflector.Tests;

// Simulates a typical game entity base class
public class GameEntity
{
    public int EntityId;
    public float PositionX;
    public float PositionY;
    private bool _isActive;
    protected string? _name;
}

// Simulates a creature class (like STS2's Creature)
public class Creature : GameEntity
{
    public int CurrentHp;
    public int MaxHp;
    public int Block;
    public bool IsPlayer;
    private int _gold;
    public long UniqueId;
}

// Simulates a card class
public class Card
{
    public int CardId;
    public string? CardName;
    public int Cost;
    public int BaseDamage;
    public bool Upgraded;
    public byte Rarity;
}

// Value type with sequential layout (like a Godot Vector2)
[StructLayout(LayoutKind.Sequential)]
public struct Vector2
{
    public float X;
    public float Y;
}

// Value type with explicit layout
[StructLayout(LayoutKind.Explicit)]
public struct VariantData
{
    [FieldOffset(0)] public int IntValue;
    [FieldOffset(0)] public float FloatValue;
    [FieldOffset(0)] public long LongValue;
    [FieldOffset(8)] public int Type;
}

// Class with various field types for comprehensive testing
public class MixedFields
{
    public byte ByteField;
    public short ShortField;
    public int IntField;
    public long LongField;
    public float FloatField;
    public double DoubleField;
    public bool BoolField;
    public char CharField;
    public string? StringField;
    public object? ObjectField;
    public int[]? ArrayField;
    public Vector2 StructField;
    public static int StaticField;
    public static string? StaticStringField;
}

// Nested class structure
public class OuterContainer
{
    public int Id;
    public InnerData? Data;

    public class InnerData
    {
        public int Value;
        public string? Label;
    }
}
