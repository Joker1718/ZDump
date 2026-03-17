using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ZoltDumpUI.Core;
using ZoltDumpUI.Models;

namespace ZoltDumpUI.ViewModels;

/// <summary>Node class filter/browser — browse and filter ClassDB entries with property detail.</summary>
public partial class NodeFilterViewModel : ViewModelBase
{
    private readonly IDumpService _dump;
    private readonly ILoggingService _log;
    private List<ClassListItem> _allClasses = [];

    public ObservableCollection<ClassListItem> DisplayedClasses { get; } = [];
    public ObservableCollection<string> SuperSuggestions { get; } = [];
    public ObservableCollection<FieldInfoModel> SelectedProperties { get; } = [];
    public ObservableCollection<MethodInfoEntry> SelectedMethods { get; } = [];

    [ObservableProperty] private string _filterText = "";
    [ObservableProperty] private string _superFilter = "";
    [ObservableProperty] private bool _gameOnly = true;
    [ObservableProperty] private int _displayCount;
    [ObservableProperty] private bool _isLoading;
    [ObservableProperty] private string _selectedClassName = "";
    [ObservableProperty] private string _selectedSuperName = "";
    [ObservableProperty] private int _selectedPropertyCount;
    [ObservableProperty] private bool _isLoadingDetail;

    public event Action<string>? NavigateToInstanceFinder;
    public event Action<string>? NavigateToLiveWalker;

    public NodeFilterViewModel(IDumpService dump, ILoggingService log)
    {
        _dump = dump;
        _log = log;
    }

    partial void OnFilterTextChanged(string value) => ApplyFilter();
    partial void OnSuperFilterChanged(string value) => ApplyFilter();

    [RelayCommand]
    private async Task LoadClassesAsync()
    {
        try
        {
            ClearError();
            IsLoading = true;

            _allClasses = await _dump.ListClassesAsync(GameOnly);

            // Build super suggestions
            var supers = _allClasses.Select(c => c.SuperName).Where(s => !string.IsNullOrEmpty(s)).Distinct().OrderBy(s => s);
            SuperSuggestions.Clear();
            SuperSuggestions.Add(""); // All
            foreach (var s in supers)
                SuperSuggestions.Add(s);

            ApplyFilter();
        }
        catch (Exception ex)
        {
            SetError($"Load failed: {ex.Message}");
            _log.Error("view", "Class list load failed", ex);
        }
        finally
        {
            IsLoading = false;
        }
    }

    [RelayCommand]
    private void FindInstances(ClassListItem item)
    {
        NavigateToInstanceFinder?.Invoke(item.ClassName);
    }

    /// <summary>Find first instance of selected class and open in Live Walker.</summary>
    [RelayCommand]
    private async Task OpenInLiveWalkerAsync(ClassListItem item)
    {
        try
        {
            var result = await _dump.FindInstancesAsync(item.ClassName, exactMatch: true);
            if (result.Instances.Count > 0)
            {
                NavigateToLiveWalker?.Invoke(result.Instances[0].Address);
            }
            else
            {
                SetError($"No instances found for '{item.ClassName}'");
            }
        }
        catch (Exception ex)
        {
            _log.Error("view", $"Open in LiveWalker failed: {ex.Message}", ex);
        }
    }

    /// <summary>Load class detail (properties + methods) for the selected class row.</summary>
    public async Task LoadClassDetailAsync(string className)
    {
        if (string.IsNullOrEmpty(className)) return;
        try
        {
            IsLoadingDetail = true;
            var info = await _dump.WalkClassAsync(className);
            SelectedClassName = info.ClassName;
            SelectedSuperName = info.SuperName;
            SelectedPropertyCount = info.PropertiesCount;

            SelectedProperties.Clear();
            foreach (var p in info.Properties)
                SelectedProperties.Add(p);

            SelectedMethods.Clear();
            foreach (var m in info.Methods)
                SelectedMethods.Add(m);
        }
        catch (Exception ex)
        {
            _log.Error("view", $"Class detail load failed: {ex.Message}", ex);
        }
        finally
        {
            IsLoadingDetail = false;
        }
    }

    private void ApplyFilter()
    {
        var filtered = _allClasses.AsEnumerable();

        if (!string.IsNullOrWhiteSpace(SuperFilter))
            filtered = filtered.Where(c => c.SuperName.Equals(SuperFilter, StringComparison.OrdinalIgnoreCase));

        if (!string.IsNullOrWhiteSpace(FilterText))
        {
            var text = FilterText;
            filtered = filtered.Where(c => c.ClassName.Contains(text, StringComparison.OrdinalIgnoreCase));
        }

        var items = filtered.OrderByDescending(c => c.Score).Take(5000).ToList();

        DisplayedClasses.Clear();
        foreach (var item in items)
            DisplayedClasses.Add(item);
        DisplayCount = items.Count;
    }
}
