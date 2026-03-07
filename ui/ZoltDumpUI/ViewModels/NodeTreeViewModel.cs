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

            const int pageSize = 500;
            for (var offset = 0; offset < total; offset += pageSize)
            {
                ct.ThrowIfCancellationRequested();
                var result = await _dump.GetObjectListAsync(offset, pageSize, ct);
                _allNodes.AddRange(result.Objects);
            }

            // Build class suggestions
            var classes = _allNodes.Select(n => n.ClassName).Distinct().OrderBy(c => c).ToList();
            ClassSuggestions.Clear();
            ClassSuggestions.Add(""); // All
            foreach (var c in classes)
                ClassSuggestions.Add(c);

            ApplyFilter();
            _log.Info("view", $"Loaded {_allNodes.Count} objects");
        }
        catch (OperationCanceledException) { }
        catch (Exception ex)
        {
            SetError($"Load failed: {ex.Message}");
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
