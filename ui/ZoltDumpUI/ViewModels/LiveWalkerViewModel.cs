using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ZoltDumpUI.Core;
using ZoltDumpUI.Models;

namespace ZoltDumpUI.ViewModels;

/// <summary>
/// Ubel — 優貝爾 (Precise Extraction Specialist)
/// Live instance property drilldown with breadcrumb navigation.
/// </summary>
public partial class LiveWalkerViewModel : ViewModelBase
{
    private readonly IDumpService _dump;
    private readonly ILoggingService _log;
    private readonly IPlatformService _platform;
    private readonly IAobMakerBridge _aobMaker;

    public ObservableCollection<LiveFieldValue> Fields { get; } = [];
    public ObservableCollection<BreadcrumbItem> Breadcrumbs { get; } = [];
    public ObservableCollection<MethodInfoEntry> Functions { get; } = [];

    [ObservableProperty] private string _currentAddress = "";
    [ObservableProperty] private string _currentName = "";
    [ObservableProperty] private string _currentClassName = "";
    [ObservableProperty] private string _parentAddr = "";
    [ObservableProperty] private string _addressInput = "";
    [ObservableProperty] private string _searchQuery = "";
    [ObservableProperty] private bool _isAutoRefresh;
    [ObservableProperty] private int _autoRefreshInterval = 1000;
    [ObservableProperty] private bool _isLoading;

    private CancellationTokenSource? _refreshCts;

    public LiveWalkerViewModel(IDumpService dump, ILoggingService log, IPlatformService platform, IAobMakerBridge aobMaker)
    {
        _dump = dump;
        _log = log;
        _platform = platform;
        _aobMaker = aobMaker;
    }

    public async Task NavigateToAddressAsync(string address)
    {
        try
        {
            ClearError();
            IsLoading = true;

            var result = await _dump.WalkInstanceAsync(address);
            CurrentAddress = result.Address;
            CurrentName = result.Name;
            CurrentClassName = result.ClassName;
            ParentAddr = result.ParentAddr;

            Fields.Clear();
            foreach (var f in result.Fields)
                Fields.Add(f);

            // Update breadcrumbs
            Breadcrumbs.Add(new BreadcrumbItem(result.Address, result.Name, result.ClassName));

            AddressInput = address;
            _log.Debug("view", $"Walked instance {result.Name} @ {result.Address}");
        }
        catch (Exception ex)
        {
            SetError($"Walk failed: {ex.Message}");
            _log.Error("view", "Instance walk failed", ex);
        }
        finally
        {
            IsLoading = false;
        }
    }

    [RelayCommand]
    private async Task GoToAddressAsync()
    {
        if (string.IsNullOrWhiteSpace(AddressInput)) return;
        var addr = AddressHelper.NormalizeAddress(AddressInput);
        Breadcrumbs.Clear();
        await NavigateToAddressAsync(addr);
    }

    [RelayCommand]
    private async Task StartWorldAsync()
    {
        try
        {
            ClearError();
            IsLoading = true;
            var tree = await _dump.WalkTreeAsync(1);
            if (tree.Root is not null)
            {
                Breadcrumbs.Clear();
                await NavigateToAddressAsync(tree.Root.Address);
            }
            else
            {
                SetError("SceneTree root not found. The game may use a custom SceneTree layout. " +
                         "Use address navigation or Object Tree instead.");
                _log.Info("view", "StartWorld: SceneTree root is null (root probe failed)");
            }
        }
        catch (Exception ex)
        {
            SetError($"SceneTree walk failed: {ex.Message}");
            _log.Error("view", "StartWorld failed", ex);
        }
        finally
        {
            IsLoading = false;
        }
    }

    [RelayCommand]
    private async Task NavigateBackAsync()
    {
        if (Breadcrumbs.Count <= 1) return;
        Breadcrumbs.RemoveAt(Breadcrumbs.Count - 1);
        var prev = Breadcrumbs[^1];
        Breadcrumbs.RemoveAt(Breadcrumbs.Count - 1); // Will be re-added by NavigateToAddress
        await NavigateToAddressAsync(prev.Address);
    }

    [RelayCommand]
    private async Task NavigateToParentAsync()
    {
        if (string.IsNullOrEmpty(ParentAddr)) return;
        await NavigateToAddressAsync(ParentAddr);
    }

    [RelayCommand]
    private async Task RefreshAsync()
    {
        if (string.IsNullOrEmpty(CurrentAddress)) return;
        await NavigateToAddressAsync(CurrentAddress);
    }

    [RelayCommand]
    private async Task NavigatePointerAsync(LiveFieldValue field)
    {
        if (!field.IsPointer || string.IsNullOrEmpty(field.PtrValue)) return;
        await NavigateToAddressAsync(field.PtrValue);
    }

    [RelayCommand]
    private async Task CopyAddressAsync()
    {
        await _platform.CopyToClipboardAsync(CurrentAddress);
    }

    [RelayCommand]
    private async Task CopyNameAsync()
    {
        await _platform.CopyToClipboardAsync(CurrentName);
    }

    [RelayCommand]
    private async Task CopyFieldAddressAsync(LiveFieldValue field)
    {
        await _platform.CopyToClipboardAsync(field.Address);
    }

    [RelayCommand]
    private async Task CopyFieldNameAsync(LiveFieldValue field)
    {
        await _platform.CopyToClipboardAsync(field.Name);
    }

    [RelayCommand]
    private async Task NavigateToBreadcrumbAsync(BreadcrumbItem crumb)
    {
        // Remove all breadcrumbs after this one
        var idx = -1;
        for (var i = 0; i < Breadcrumbs.Count; i++)
        {
            if (Breadcrumbs[i].Address == crumb.Address) { idx = i; break; }
        }
        if (idx < 0) return;
        while (Breadcrumbs.Count > idx)
            Breadcrumbs.RemoveAt(Breadcrumbs.Count - 1);
        await NavigateToAddressAsync(crumb.Address);
    }

    [RelayCommand]
    private async Task NavigateHexViewAsync()
    {
        if (string.IsNullOrEmpty(CurrentAddress)) return;
        await _aobMaker.NavigateHexViewAsync(CurrentAddress);
    }

    partial void OnIsAutoRefreshChanged(bool value)
    {
        if (value)
            StartAutoRefresh();
        else
            StopAutoRefresh();
    }

    private void StartAutoRefresh()
    {
        StopAutoRefresh();
        _refreshCts = new CancellationTokenSource();
        _ = AutoRefreshLoopAsync(_refreshCts.Token);
    }

    private void StopAutoRefresh()
    {
        _refreshCts?.Cancel();
        _refreshCts = null;
    }

    private async Task AutoRefreshLoopAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            await Task.Delay(AutoRefreshInterval, ct);
            if (!string.IsNullOrEmpty(CurrentAddress))
            {
                try
                {
                    var result = await _dump.WalkInstanceAsync(CurrentAddress, ct);
                    Fields.Clear();
                    foreach (var f in result.Fields)
                        Fields.Add(f);
                }
                catch (OperationCanceledException) { break; }
                catch { /* ignore refresh errors */ }
            }
        }
    }

    public void ApplySearch()
    {
        if (string.IsNullOrWhiteSpace(SearchQuery))
        {
            foreach (var f in Fields) f.IsSearchMatch = false;
            return;
        }
        foreach (var f in Fields)
        {
            f.IsSearchMatch = f.Name.Contains(SearchQuery, StringComparison.OrdinalIgnoreCase) ||
                             f.Type.Contains(SearchQuery, StringComparison.OrdinalIgnoreCase);
        }
    }
}

/// <summary>Navigation breadcrumb for LiveWalker.</summary>
public sealed class BreadcrumbItem(string address, string name, string className)
{
    public string Address { get; } = address;
    public string Name { get; } = name;
    public string ClassName { get; } = className;
    public string Display => string.IsNullOrEmpty(Name) ? Address : Name;
}
