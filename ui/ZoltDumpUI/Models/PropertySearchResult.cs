/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;

namespace ZoltDumpUI.Models;

/// <summary>Result of property search across classes.</summary>
public sealed class PropertySearchResult
{
    public int Count { get; init; }
    public List<PropertySearchHit> Hits { get; init; } = [];
}

/// <summary>Single property search hit with live-updateable Preview.</summary>
[DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.PublicProperties)]
public sealed class PropertySearchHit : INotifyPropertyChanged
{
    public string ClassName { get; init; } = "";
    public string SuperName { get; init; } = "";
    public string PropertyName { get; init; } = "";
    public string PropertyType { get; init; } = "";
    public int Offset { get; init; }
    public int Size { get; init; }

    private string _preview = "";
    public string Preview
    {
        get => _preview;
        set
        {
            if (_preview != value)
            {
                _preview = value;
                OnPropertyChanged();
            }
        }
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged([CallerMemberName] string? name = null)
        => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}
