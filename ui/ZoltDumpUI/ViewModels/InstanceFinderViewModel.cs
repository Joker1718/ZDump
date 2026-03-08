using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ZoltDumpUI.Core;
using ZoltDumpUI.Models;

namespace ZoltDumpUI.ViewModels;

/// <summary>Instance finder — search all objects of a class.</summary>
public partial class InstanceFinderViewModel : ViewModelBase
{
    private readonly IDumpService _dump;
    private readonly ILoggingService _log;
    private readonly IPlatformService _platform;

    public ObservableCollection<ObjectEntry> Instances { get; } = [];
    public ObservableCollection<LiveFieldValue> SelectedFields { get; } = [];

    [ObservableProperty] private string _searchClassName = "";
    [ObservableProperty] private string _addressSearch = "";
    [ObservableProperty] private bool _exactMatch;
    [ObservableProperty] private int _instanceCount;
    [ObservableProperty] private ObjectEntry? _selectedInstance;
    [ObservableProperty] private bool _isLoading;
    [ObservableProperty] private string _statusText = "";

    public event Action<string>? NavigateToLiveWalker;

    public InstanceFinderViewModel(IDumpService dump, ILoggingService log, IPlatformService platform)
    {
        _dump = dump;
        _log = log;
        _platform = platform;
    }

    partial void OnSelectedInstanceChanged(ObjectEntry? value)
    {
        if (value is not null)
            _ = LoadInstanceFieldsAsync(value.Address);
    }

    public async Task SearchAsync(string className)
    {
        SearchClassName = className;
        await FindInstancesAsync();
    }

    [RelayCommand]
    private async Task FindInstancesAsync()
    {
        if (string.IsNullOrWhiteSpace(SearchClassName)) return;
        try
        {
            ClearError();
            IsLoading = true;
            StatusText = "Searching...";

            var result = await _dump.FindInstancesAsync(SearchClassName, ExactMatch);
            Instances.Clear();
            foreach (var inst in result.Instances)
                Instances.Add(inst);
            InstanceCount = result.Count;

            StatusText = result.Count > 0
                ? $"Found {result.Count} instance{(result.Count == 1 ? "" : "s")}"
                : $"No instances of '{SearchClassName}' found";
        }
        catch (Exception ex)
        {
            SetError($"Search failed: {ex.Message}");
            StatusText = "";
            _log.Error("view", "Instance search failed", ex);
        }
        finally
        {
            IsLoading = false;
        }
    }

    [RelayCommand]
    private async Task FindByAddressAsync()
    {
        if (string.IsNullOrWhiteSpace(AddressSearch)) return;
        try
        {
            ClearError();
            StatusText = "Looking up...";
            var addr = AddressHelper.NormalizeAddress(AddressSearch);
            var result = await _dump.FindByAddressAsync(addr);
            if (result.Found)
            {
                Instances.Clear();
                Instances.Add(new ObjectEntry
                {
                    Address = result.Address,
                    Name = result.Name,
                    ClassName = result.ClassName,
                    SlotIndex = result.SlotIndex
                });
                InstanceCount = 1;
                StatusText = $"Found: {result.Name} ({result.ClassName})";
            }
            else
            {
                Instances.Clear();
                InstanceCount = 0;
                StatusText = $"No object found at {addr}";
            }
        }
        catch (Exception ex)
        {
            SetError($"Lookup failed: {ex.Message}");
            StatusText = "";
        }
    }

    [RelayCommand]
    private void OpenInLiveWalker(ObjectEntry entry)
    {
        NavigateToLiveWalker?.Invoke(entry.Address);
    }

    [RelayCommand]
    private async Task CopyAddressAsync(ObjectEntry entry)
    {
        await _platform.CopyToClipboardAsync(entry.Address);
    }

    private async Task LoadInstanceFieldsAsync(string address)
    {
        try
        {
            var result = await _dump.WalkInstanceAsync(address);
            SelectedFields.Clear();
            foreach (var f in result.Fields)
                SelectedFields.Add(f);
        }
        catch (Exception ex)
        {
            _log.Error("view", $"Instance field load failed: {ex.Message}", ex);
        }
    }
}
