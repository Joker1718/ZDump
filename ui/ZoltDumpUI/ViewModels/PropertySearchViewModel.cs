using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ZoltDumpUI.Core;
using ZoltDumpUI.Models;

namespace ZoltDumpUI.ViewModels;

/// <summary>Property keyword search across all classes.</summary>
public partial class PropertySearchViewModel : ViewModelBase
{
    private readonly IDumpService _dump;
    private readonly ILoggingService _log;
    private readonly IPlatformService _platform;

    public ObservableCollection<PropertySearchHit> Results { get; } = [];

    [ObservableProperty] private string _searchQuery = "";
    [ObservableProperty] private bool _gameOnly;
    [ObservableProperty] private int _resultCount;
    [ObservableProperty] private bool _isSearching;

    public event Action<string>? NavigateToInstanceFinder;

    public PropertySearchViewModel(IDumpService dump, ILoggingService log, IPlatformService platform)
    {
        _dump = dump;
        _log = log;
        _platform = platform;
    }

    [RelayCommand]
    private async Task SearchAsync()
    {
        if (string.IsNullOrWhiteSpace(SearchQuery)) return;
        try
        {
            ClearError();
            IsSearching = true;

            var result = await _dump.SearchPropertiesAsync(SearchQuery, GameOnly);
            Results.Clear();
            foreach (var hit in result.Hits)
                Results.Add(hit);
            ResultCount = result.Count;
        }
        catch (Exception ex)
        {
            SetError($"Search failed: {ex.Message}");
            _log.Error("view", "Property search failed", ex);
        }
        finally
        {
            IsSearching = false;
        }
    }

    [RelayCommand]
    private void FindInstances(PropertySearchHit hit)
    {
        NavigateToInstanceFinder?.Invoke(hit.ClassName);
    }

    [RelayCommand]
    private async Task CopyOffsetAsync(PropertySearchHit hit)
    {
        await _platform.CopyToClipboardAsync($"0x{hit.Offset:X}");
    }
}
