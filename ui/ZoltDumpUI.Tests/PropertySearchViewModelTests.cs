using Xunit;
using ZoltDumpUI.Models;
using ZoltDumpUI.Tests.Fakes;
using ZoltDumpUI.ViewModels;

namespace ZoltDumpUI.Tests;

public class PropertySearchViewModelTests
{
    private readonly FakeDumpService _dump = new();
    private readonly FakeLoggingService _log = new();
    private readonly FakePlatformService _platform = new();

    private PropertySearchViewModel CreateVm() => new(_dump, _log, _platform);

    // ================================================================
    // SearchCommand
    // ================================================================

    [Fact]
    public async Task Search_PopulatesResults()
    {
        _dump.SearchResult = new PropertySearchResult
        {
            Count = 2,
            Hits =
            [
                new() { ClassName = "Timer", PropertyName = "time_left", PropertyType = "FLOAT", Offset = 0x1E0 },
                new() { ClassName = "SceneTreeTimer", PropertyName = "time_left", PropertyType = "FLOAT", Offset = 0xE8 }
            ]
        };
        var vm = CreateVm();
        vm.SearchQuery = "time_left";

        await vm.SearchCommand.ExecuteAsync(null);

        Assert.Equal(2, vm.Results.Count);
        Assert.Equal(2, vm.ResultCount);
    }

    [Fact]
    public async Task Search_EmptyQuery_DoesNothing()
    {
        var vm = CreateVm();
        vm.SearchQuery = "";

        await vm.SearchCommand.ExecuteAsync(null);

        Assert.Empty(vm.Results);
        Assert.Empty(_dump.Calls);
    }

    [Fact]
    public async Task Search_WhitespaceQuery_DoesNothing()
    {
        var vm = CreateVm();
        vm.SearchQuery = "   ";

        await vm.SearchCommand.ExecuteAsync(null);

        Assert.Empty(vm.Results);
    }

    [Fact]
    public async Task Search_ClearsOldResults()
    {
        _dump.SearchResult = new PropertySearchResult
        {
            Count = 1,
            Hits = [new() { ClassName = "A", PropertyName = "x" }]
        };
        var vm = CreateVm();
        vm.SearchQuery = "x";
        await vm.SearchCommand.ExecuteAsync(null);
        Assert.Single(vm.Results);

        // New search with different results
        _dump.SearchResult = new PropertySearchResult
        {
            Count = 2,
            Hits =
            [
                new() { ClassName = "B", PropertyName = "y" },
                new() { ClassName = "C", PropertyName = "z" }
            ]
        };
        // Need instances for enrichment not to fail
        _dump.InstancesResult = new FindInstancesResult { Count = 0, Instances = [] };
        vm.SearchQuery = "yz";
        await vm.SearchCommand.ExecuteAsync(null);

        Assert.Equal(2, vm.Results.Count);
    }

    // ================================================================
    // Navigation Events
    // ================================================================

    [Fact]
    public async Task FindInstances_RaisesEvent()
    {
        var vm = CreateVm();
        string? navigated = null;
        vm.NavigateToInstanceFinder += s => navigated = s;

        var hit = new PropertySearchHit { ClassName = "Timer", PropertyName = "wait_time" };
        vm.FindInstancesCommand.Execute(hit);

        Assert.Equal("Timer", navigated);
        await Task.CompletedTask; // suppress warning
    }

    [Fact]
    public async Task OpenInLiveWalker_FindsInstanceAndNavigates()
    {
        _dump.InstancesResult = new FindInstancesResult
        {
            Count = 1,
            Instances = [new() { Address = "DEADBEEF" }]
        };
        var vm = CreateVm();
        string? navigated = null;
        vm.NavigateToLiveWalker += s => navigated = s;

        var hit = new PropertySearchHit { ClassName = "Timer" };
        await vm.OpenInLiveWalkerCommand.ExecuteAsync(hit);

        Assert.Equal("DEADBEEF", navigated);
    }

    [Fact]
    public async Task OpenInLiveWalker_NoInstances_SetsError()
    {
        _dump.InstancesResult = new FindInstancesResult { Count = 0, Instances = [] };
        var vm = CreateVm();

        var hit = new PropertySearchHit { ClassName = "Ghost" };
        await vm.OpenInLiveWalkerCommand.ExecuteAsync(hit);

        Assert.NotEmpty(vm.ErrorMessage);
    }

    // ================================================================
    // CopyOffset
    // ================================================================

    [Fact]
    public async Task CopyOffset_CopiesToClipboard()
    {
        var vm = CreateVm();
        var hit = new PropertySearchHit { Offset = 0x1E0 };

        await vm.CopyOffsetCommand.ExecuteAsync(hit);

        Assert.Equal("0x1E0", _platform.LastCopied);
    }

    // ================================================================
    // PropertySearchHit INotifyPropertyChanged (Preview update)
    // ================================================================

    [Fact]
    public void PropertySearchHit_Preview_NotifiesChange()
    {
        var hit = new PropertySearchHit { ClassName = "Test", PropertyName = "hp" };
        string? changedProp = null;
        hit.PropertyChanged += (_, e) => changedProp = e.PropertyName;

        hit.Preview = "100";

        Assert.Equal("Preview", changedProp);
        Assert.Equal("100", hit.Preview);
    }

    [Fact]
    public void PropertySearchHit_Preview_NoEventIfSameValue()
    {
        var hit = new PropertySearchHit { ClassName = "Test", PropertyName = "hp", Preview = "42" };
        bool fired = false;
        hit.PropertyChanged += (_, _) => fired = true;

        hit.Preview = "42"; // same value

        Assert.False(fired);
    }
}
