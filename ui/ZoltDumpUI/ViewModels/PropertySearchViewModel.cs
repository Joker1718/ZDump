using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ZoltDumpUI.Core;
using ZoltDumpUI.Models;

namespace ZoltDumpUI.ViewModels;

/// <summary>Property keyword search across all classes with live value enrichment.</summary>
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
    public event Action<string>? NavigateToLiveWalker;

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

            // Kick off async value enrichment (non-blocking)
            _ = EnrichPreviewValuesAsync();
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

    /// <summary>Find first instance of the hit's class and open in Live Walker.</summary>
    [RelayCommand]
    private async Task OpenInLiveWalkerAsync(PropertySearchHit hit)
    {
        try
        {
            var result = await _dump.FindInstancesAsync(hit.ClassName, exactMatch: true);
            if (result.Instances.Count > 0)
            {
                NavigateToLiveWalker?.Invoke(result.Instances[0].Address);
            }
            else
            {
                SetError($"No instances found for '{hit.ClassName}'");
            }
        }
        catch (Exception ex)
        {
            _log.Error("view", $"Open in LiveWalker failed: {ex.Message}", ex);
        }
    }

    [RelayCommand]
    private async Task CopyOffsetAsync(PropertySearchHit hit)
    {
        await _platform.CopyToClipboardAsync($"0x{hit.Offset:X}");
    }

    /// <summary>
    /// After search completes, enrich Preview column with live values from sample instances.
    /// Groups hits by class, finds one instance per class, walks it, and fills in previews.
    /// Limited to first 20 unique classes to avoid excessive pipe calls.
    /// </summary>
    private async Task EnrichPreviewValuesAsync()
    {
        try
        {
            var hitsByClass = Results
                .GroupBy(h => h.ClassName)
                .Take(20)
                .ToList();

            foreach (var group in hitsByClass)
            {
                try
                {
                    // Find one instance of this class
                    var instances = await _dump.FindInstancesAsync(group.Key, exactMatch: true);
                    if (instances.Instances.Count == 0) continue;

                    // Walk the first instance to get live values
                    var walk = await _dump.WalkInstanceAsync(instances.Instances[0].Address);

                    // Match property names to search hits and fill preview
                    var fieldLookup = walk.Fields
                        .ToDictionary(f => f.Name, f => f.Value, StringComparer.OrdinalIgnoreCase);

                    foreach (var hit in group)
                    {
                        if (fieldLookup.TryGetValue(hit.PropertyName, out var value) &&
                            !string.IsNullOrEmpty(value))
                        {
                            hit.Preview = value;
                        }
                    }
                }
                catch
                {
                    // Skip classes that fail — don't break enrichment for others
                }
            }
        }
        catch (Exception ex)
        {
            _log.Error("view", $"Preview enrichment failed: {ex.Message}", ex);
        }
    }
}
