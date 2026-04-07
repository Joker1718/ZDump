/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ZoltDumpUI.Core;
using ZoltDumpUI.Models;

namespace ZoltDumpUI.ViewModels;

/// <summary>
/// Serie — 賽莉耶 (Living History)
/// Node tree panel ViewModel for SceneTree hierarchy browsing.
/// </summary>
public partial class NodeTreeViewModel : ViewModelBase
{
    private readonly IDumpService _dump;
    private readonly ILoggingService _log;
    private readonly IPlatformService _platform;
    private List<ObjectEntry> _allNodes = [];
    private CancellationTokenSource? _loadCts;

    public ObservableCollection<ObjectEntry> DisplayedNodes { get; } = [];

    [ObservableProperty] private string _filterText = "";
    [ObservableProperty] private string _classFilter = "";
    [ObservableProperty] private int _displayCount;
    [ObservableProperty] private int _totalCount;
    [ObservableProperty] private bool _isLoading;
    [ObservableProperty] private ObjectEntry? _selectedNode;
    [ObservableProperty] private string _statusText = "";

    public ObservableCollection<string> ClassSuggestions { get; } = [];
    public event Action<ObjectEntry?>? SelectionChanged;

    public NodeTreeViewModel(IDumpService dump, ILoggingService log, IPlatformService platform)
    {
        _dump = dump;
        _log = log;
        _platform = platform;
    }

    partial void OnSelectedNodeChanged(ObjectEntry? value)
    {
        SelectionChanged?.Invoke(value);
    }

    partial void OnFilterTextChanged(string value) => ApplyFilter();
    partial void OnClassFilterChanged(string value) => ApplyFilter();

    [RelayCommand]
    private async Task LoadAllAsync()
    {
        _loadCts?.Cancel();
        _loadCts = new CancellationTokenSource();
        var ct = _loadCts.Token;

        try
        {
            IsLoading = true;
            ClearError();
            _allNodes.Clear();

            var total = await _dump.GetObjectCountAsync(ct);
            TotalCount = total;
            StatusText = $"Loading... 0 / {total:N0}";

            // Use large page size like UE5CEDumper (2000 per batch).
            // DLL's get_object_list uses slot index as offset — advance by
            // last returned slot_index + 1, not by page size.
            const int pageSize = 2000;
            var offset = 0;

            while (true)
            {
                ct.ThrowIfCancellationRequested();
                var result = await _dump.GetObjectListAsync(offset, pageSize, ct);

                if (result.Objects.Count == 0)
                    break;

                _allNodes.AddRange(result.Objects);
                StatusText = $"Loading... {_allNodes.Count:N0} / {total:N0}";

                // Next offset = slot index after the last returned object
                var lastSlotIndex = result.Objects[^1].SlotIndex;
                offset = lastSlotIndex + 1;

                // Stop if we've loaded all objects
                if (_allNodes.Count >= total)
                    break;
            }

            // Build class suggestions (filter out empty class names)
            var classes = _allNodes
                .Select(n => n.ClassName)
                .Where(c => !string.IsNullOrEmpty(c))
                .Distinct()
                .OrderBy(c => c)
                .ToList();
            ClassSuggestions.Clear();
            ClassSuggestions.Add(""); // All
            foreach (var c in classes)
                ClassSuggestions.Add(c);

            ApplyFilter();
            StatusText = $"Loaded {_allNodes.Count:N0} objects";
            _log.Info("view", $"Loaded {_allNodes.Count} objects");
        }
        catch (OperationCanceledException)
        {
            StatusText = _allNodes.Count > 0
                ? $"Cancelled — {_allNodes.Count:N0} objects loaded"
                : "";
        }
        catch (Exception ex)
        {
            SetError($"Load failed: {ex.Message}");
            StatusText = "";
            _log.Error("view", "Object load failed", ex);
        }
        finally
        {
            IsLoading = false;
        }
    }

    [RelayCommand]
    private void CancelLoad()
    {
        _loadCts?.Cancel();
    }

    [RelayCommand]
    private async Task CopyClassNameAsync()
    {
        if (SelectedNode is not null)
            await _platform.CopyToClipboardAsync(SelectedNode.ClassName);
    }

    [RelayCommand]
    private async Task CopyNameAsync()
    {
        if (SelectedNode is not null)
            await _platform.CopyToClipboardAsync(SelectedNode.Name);
    }

    [RelayCommand]
    private async Task CopyAddressAsync()
    {
        if (SelectedNode is not null)
            await _platform.CopyToClipboardAsync(SelectedNode.Address);
    }

    private void ApplyFilter()
    {
        var filtered = _allNodes.AsEnumerable();

        if (!string.IsNullOrWhiteSpace(ClassFilter))
            filtered = filtered.Where(n => n.ClassName.Equals(ClassFilter, StringComparison.OrdinalIgnoreCase));

        if (!string.IsNullOrWhiteSpace(FilterText))
        {
            var text = FilterText;
            filtered = filtered.Where(n =>
                n.Name.Contains(text, StringComparison.OrdinalIgnoreCase) ||
                n.ClassName.Contains(text, StringComparison.OrdinalIgnoreCase) ||
                n.Address.Contains(text, StringComparison.OrdinalIgnoreCase));
        }

        const int maxDisplay = 5000;
        var items = filtered.Take(maxDisplay).ToList();

        DisplayedNodes.Clear();
        foreach (var item in items)
            DisplayedNodes.Add(item);
        DisplayCount = items.Count;
    }
}
