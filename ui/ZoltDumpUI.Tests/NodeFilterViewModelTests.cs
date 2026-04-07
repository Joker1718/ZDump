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
using ZoltDumpUI.Tests.Fakes;
using ZoltDumpUI.ViewModels;

namespace ZoltDumpUI.Tests;

public class NodeFilterViewModelTests
{
    private readonly FakeDumpService _dump = new();
    private readonly FakeLoggingService _log = new();

    private NodeFilterViewModel CreateVm() => new(_dump, _log);

    private static List<ClassListItem> SampleClasses() =>
    [
        new() { ClassName = "Player", SuperName = "CharacterBody2D", PropertyCount = 5, MethodCount = 2, IsGameClass = true, Score = 10 },
        new() { ClassName = "Enemy", SuperName = "CharacterBody2D", PropertyCount = 3, MethodCount = 1, IsGameClass = true, Score = 8 },
        new() { ClassName = "MainMenu", SuperName = "Control", PropertyCount = 2, MethodCount = 0, IsGameClass = true, Score = 5 },
        new() { ClassName = "Timer", SuperName = "Node", PropertyCount = 4, MethodCount = 3, IsGameClass = false, Score = 0 },
        new() { ClassName = "Node2D", SuperName = "CanvasItem", PropertyCount = 6, MethodCount = 10, IsGameClass = false, Score = 0 },
    ];

    // ================================================================
    // LoadClassesCommand
    // ================================================================

    [Fact]
    public async Task LoadClasses_PopulatesDisplayedClasses()
    {
        _dump.ClassList = SampleClasses();
        var vm = CreateVm();

        await vm.LoadClassesCommand.ExecuteAsync(null);

        Assert.Equal(5, vm.DisplayedClasses.Count);
        Assert.Equal(5, vm.DisplayCount);
    }

    [Fact]
    public async Task LoadClasses_BuildsSuperSuggestions()
    {
        _dump.ClassList = SampleClasses();
        var vm = CreateVm();

        await vm.LoadClassesCommand.ExecuteAsync(null);

        // Empty string (All) + 4 unique supers
        Assert.Contains("", vm.SuperSuggestions);
        Assert.Contains("CharacterBody2D", vm.SuperSuggestions);
        Assert.Contains("Control", vm.SuperSuggestions);
        Assert.Contains("Node", vm.SuperSuggestions);
        Assert.Contains("CanvasItem", vm.SuperSuggestions);
    }

    [Fact]
    public async Task LoadClasses_SortsByScoreDescending()
    {
        _dump.ClassList = SampleClasses();
        var vm = CreateVm();

        await vm.LoadClassesCommand.ExecuteAsync(null);

        Assert.Equal("Player", vm.DisplayedClasses[0].ClassName);
        Assert.Equal("Enemy", vm.DisplayedClasses[1].ClassName);
        Assert.Equal("MainMenu", vm.DisplayedClasses[2].ClassName);
    }

    // ================================================================
    // FilterText
    // ================================================================

    [Fact]
    public async Task FilterText_FiltersByName()
    {
        _dump.ClassList = SampleClasses();
        var vm = CreateVm();
        await vm.LoadClassesCommand.ExecuteAsync(null);

        vm.FilterText = "play";

        Assert.Single(vm.DisplayedClasses);
        Assert.Equal("Player", vm.DisplayedClasses[0].ClassName);
    }

    [Fact]
    public async Task FilterText_CaseInsensitive()
    {
        _dump.ClassList = SampleClasses();
        var vm = CreateVm();
        await vm.LoadClassesCommand.ExecuteAsync(null);

        vm.FilterText = "ENEMY";

        Assert.Single(vm.DisplayedClasses);
        Assert.Equal("Enemy", vm.DisplayedClasses[0].ClassName);
    }

    [Fact]
    public async Task FilterText_Empty_ShowsAll()
    {
        _dump.ClassList = SampleClasses();
        var vm = CreateVm();
        await vm.LoadClassesCommand.ExecuteAsync(null);

        vm.FilterText = "play";
        Assert.Single(vm.DisplayedClasses);

        vm.FilterText = "";
        Assert.Equal(5, vm.DisplayedClasses.Count);
    }

    // ================================================================
    // SuperFilter
    // ================================================================

    [Fact]
    public async Task SuperFilter_FiltersBySuper()
    {
        _dump.ClassList = SampleClasses();
        var vm = CreateVm();
        await vm.LoadClassesCommand.ExecuteAsync(null);

        vm.SuperFilter = "CharacterBody2D";

        Assert.Equal(2, vm.DisplayedClasses.Count);
        Assert.All(vm.DisplayedClasses, c => Assert.Equal("CharacterBody2D", c.SuperName));
    }

    [Fact]
    public async Task SuperFilter_CaseInsensitive()
    {
        _dump.ClassList = SampleClasses();
        var vm = CreateVm();
        await vm.LoadClassesCommand.ExecuteAsync(null);

        vm.SuperFilter = "control";

        Assert.Single(vm.DisplayedClasses);
        Assert.Equal("MainMenu", vm.DisplayedClasses[0].ClassName);
    }

    // ================================================================
    // Combined Filters
    // ================================================================

    [Fact]
    public async Task FilterText_And_SuperFilter_Combined()
    {
        _dump.ClassList = SampleClasses();
        var vm = CreateVm();
        await vm.LoadClassesCommand.ExecuteAsync(null);

        vm.SuperFilter = "CharacterBody2D";
        vm.FilterText = "play";

        Assert.Single(vm.DisplayedClasses);
        Assert.Equal("Player", vm.DisplayedClasses[0].ClassName);
    }

    [Fact]
    public async Task CombinedFilter_NoMatch_Empty()
    {
        _dump.ClassList = SampleClasses();
        var vm = CreateVm();
        await vm.LoadClassesCommand.ExecuteAsync(null);

        vm.SuperFilter = "Control";
        vm.FilterText = "Player";

        Assert.Empty(vm.DisplayedClasses);
        Assert.Equal(0, vm.DisplayCount);
    }

    // ================================================================
    // LoadClassDetailAsync
    // ================================================================

    [Fact]
    public async Task LoadClassDetail_PopulatesProperties()
    {
        _dump.ClassInfo = new ClassInfoModel
        {
            ClassName = "Timer",
            SuperName = "Node",
            PropertiesCount = 2,
            Properties =
            [
                new() { Name = "wait_time", Type = "FLOAT", Offset = 0x1D8, Size = 8 },
                new() { Name = "time_left", Type = "FLOAT", Offset = 0x1E0, Size = 8 }
            ],
            Methods = [new() { Name = "start", ReturnType = "void" }]
        };
        var vm = CreateVm();

        await vm.LoadClassDetailAsync("Timer");

        Assert.Equal("Timer", vm.SelectedClassName);
        Assert.Equal("Node", vm.SelectedSuperName);
        Assert.Equal(2, vm.SelectedPropertyCount);
        Assert.Equal(2, vm.SelectedProperties.Count);
        Assert.Single(vm.SelectedMethods);
    }

    [Fact]
    public async Task LoadClassDetail_EmptyClassName_DoesNothing()
    {
        var vm = CreateVm();
        await vm.LoadClassDetailAsync("");

        Assert.Empty(vm.SelectedProperties);
        Assert.Empty(_dump.Calls);
    }

    // ================================================================
    // Navigation Events
    // ================================================================

    [Fact]
    public async Task FindInstances_RaisesNavigateToInstanceFinder()
    {
        _dump.ClassList = SampleClasses();
        var vm = CreateVm();
        await vm.LoadClassesCommand.ExecuteAsync(null);

        string? navigatedTo = null;
        vm.NavigateToInstanceFinder += s => navigatedTo = s;

        vm.FindInstancesCommand.Execute(vm.DisplayedClasses[0]);

        Assert.Equal("Player", navigatedTo);
    }

    [Fact]
    public async Task OpenInLiveWalker_FindsInstanceAndNavigates()
    {
        _dump.ClassList = SampleClasses();
        _dump.InstancesResult = new FindInstancesResult
        {
            ClassName = "Player",
            Count = 1,
            Instances = [new() { Address = "1A2B3C4D" }]
        };
        var vm = CreateVm();
        await vm.LoadClassesCommand.ExecuteAsync(null);

        string? navigatedTo = null;
        vm.NavigateToLiveWalker += s => navigatedTo = s;

        await vm.OpenInLiveWalkerCommand.ExecuteAsync(vm.DisplayedClasses[0]);

        Assert.Equal("1A2B3C4D", navigatedTo);
    }

    [Fact]
    public async Task OpenInLiveWalker_NoInstances_SetsError()
    {
        _dump.InstancesResult = new FindInstancesResult { Count = 0, Instances = [] };
        var vm = CreateVm();

        await vm.OpenInLiveWalkerCommand.ExecuteAsync(
            new ClassListItem { ClassName = "Ghost" });

        Assert.NotEmpty(vm.ErrorMessage);
    }
}
