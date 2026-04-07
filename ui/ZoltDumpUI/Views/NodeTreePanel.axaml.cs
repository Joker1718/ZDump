/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
using Avalonia.Controls;
using Avalonia.Input;
using ZoltDumpUI.ViewModels;

namespace ZoltDumpUI.Views;

public partial class NodeTreePanel : UserControl
{
    public NodeTreePanel()
    {
        InitializeComponent();
    }

    /// <summary>Enter key in search triggers Load.</summary>
    protected override void OnKeyDown(KeyEventArgs e)
    {
        base.OnKeyDown(e);
        if (e.Key == Key.Enter && DataContext is NodeTreeViewModel vm)
        {
            if (vm.LoadAllCommand.CanExecute(null))
                vm.LoadAllCommand.Execute(null);
            e.Handled = true;
        }
    }
}
