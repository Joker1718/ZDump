/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
using CommunityToolkit.Mvvm.ComponentModel;

namespace ZoltDumpUI.ViewModels;

/// <summary>Base ViewModel providing common error handling.</summary>
public partial class ViewModelBase : ObservableObject
{
    [ObservableProperty]
    private string _errorMessage = "";

    protected void SetError(string message) => ErrorMessage = message;
    protected void ClearError() => ErrorMessage = "";
}
