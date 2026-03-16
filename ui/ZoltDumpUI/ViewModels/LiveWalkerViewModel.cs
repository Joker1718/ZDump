using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ZoltDumpUI.Core;
using ZoltDumpUI.Models;
using ZoltDumpUI.Services;

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
    private readonly CeLuaScriptService _ceLua;
    private readonly CeXmlExportService _ceXml;
    private readonly CsxExportService _csx;
    private readonly SdkExportService _sdk;

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

    /// <summary>Module name (process name) for CE XML export address formatting.</summary>
    public string ModuleName { get; set; } = "";

    private CancellationTokenSource? _refreshCts;

    public LiveWalkerViewModel(IDumpService dump, ILoggingService log, IPlatformService platform,
        IAobMakerBridge aobMaker, CeLuaScriptService ceLua,
        CeXmlExportService ceXml, CsxExportService csx, SdkExportService sdk)
    {
        _dump = dump;
        _log = log;
        _platform = platform;
        _aobMaker = aobMaker;
        _ceLua = ceLua;
        _ceXml = ceXml;
        _csx = csx;
        _sdk = sdk;
    }

    public async Task NavigateToAddressAsync(string address)
    {
        await FetchAndShowAsync(address);
        // Add breadcrumb only on navigation (not refresh)
        if (!string.IsNullOrEmpty(CurrentAddress))
            Breadcrumbs.Add(new BreadcrumbItem(CurrentAddress, CurrentName, CurrentClassName));
    }

    /// <summary>Fetch instance data and update fields without adding a breadcrumb.</summary>
    private async Task FetchAndShowAsync(string address)
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
        await FetchAndShowAsync(prev.Address);
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
        await FetchAndShowAsync(CurrentAddress);
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
        // Remove all breadcrumbs after this one (keep the clicked one)
        var idx = -1;
        for (var i = 0; i < Breadcrumbs.Count; i++)
        {
            if (Breadcrumbs[i].Address == crumb.Address) { idx = i; break; }
        }
        if (idx < 0) return;
        while (Breadcrumbs.Count > idx + 1)
            Breadcrumbs.RemoveAt(Breadcrumbs.Count - 1);
        await FetchAndShowAsync(crumb.Address);
    }

    [RelayCommand]
    private async Task NavigateHexViewAsync()
    {
        if (string.IsNullOrEmpty(CurrentAddress)) return;
        await _aobMaker.NavigateHexViewAsync(CurrentAddress);
    }

    // ============================================================
    // CE Lua script generation (mailbox-based, survives restarts)
    // ============================================================

    [RelayCommand]
    private async Task GenerateCeScriptAsync(LiveFieldValue? field)
    {
        if (field is null || string.IsNullOrEmpty(CurrentClassName)) return;
        try
        {
            // Use resolved member name (prefer non-script_var_ names)
            string memberName = field.Name;
            string className = CurrentName;  // Node name (e.g. "Main"), not class name

            var script = _ceLua.GenerateMemberScript(className, memberName);

            // Try AOBMaker first, fallback to clipboard
            bool sent = false;
            try
            {
                if (await _aobMaker.CheckAvailabilityAsync())
                {
                    await _aobMaker.CreateAAScriptAsync(
                        $"ZoltDump: {className}.{memberName}", script);
                    sent = true;
                    _log.Info("view", $"CE script for '{memberName}' sent to AOBMaker");
                }
            }
            catch { /* AOBMaker unavailable */ }

            if (!sent)
            {
                await _platform.CopyToClipboardAsync(script);
                _log.Info("view", $"CE script for '{memberName}' copied to clipboard");
            }
        }
        catch (Exception ex)
        {
            SetError($"CE script generation failed: {ex.Message}");
            _log.Error("view", "CE script generation failed", ex);
        }
    }

    [RelayCommand]
    private async Task GenerateInstanceScriptAsync()
    {
        if (string.IsNullOrEmpty(CurrentName) || string.IsNullOrEmpty(CurrentClassName)) return;
        try
        {
            var script = _ceLua.GenerateFindInstanceScript(CurrentName);

            bool sent = false;
            try
            {
                if (await _aobMaker.CheckAvailabilityAsync())
                {
                    await _aobMaker.CreateAAScriptAsync(
                        $"ZoltDump: Find {CurrentName}", script);
                    sent = true;
                }
            }
            catch { /* AOBMaker unavailable */ }

            if (!sent)
                await _platform.CopyToClipboardAsync(script);
        }
        catch (Exception ex)
        {
            SetError($"Instance script generation failed: {ex.Message}");
            _log.Error("view", "Instance script generation failed", ex);
        }
    }

    // ============================================================
    // Export commands
    // ============================================================

    [RelayCommand]
    private async Task CopyCeXmlAsync()
    {
        if (Fields.Count == 0) return;
        try
        {
            // Build BreadcrumbEntry list from UI breadcrumbs
            var entries = Breadcrumbs.Select(b => new BreadcrumbEntry
            {
                Address = b.Address,
                Name = b.Name,
                ClassName = b.ClassName,
                Offset = 0,
                IsPointer = true
            }).ToList();

            var fields = Fields.ToList();
            var xml = _ceXml.GenerateHierarchicalXml(entries, fields, ModuleName, AddressFormat.HexNoPrefix);
            if (!string.IsNullOrEmpty(xml))
            {
                await _platform.CopyToClipboardAsync(xml);
                _log.Info("view", "Copied CE XML to clipboard");
            }
        }
        catch (Exception ex)
        {
            SetError($"CE XML export failed: {ex.Message}");
            _log.Error("view", "CE XML export failed", ex);
        }
    }

    [RelayCommand]
    private async Task CopyCeFieldAsync(LiveFieldValue? field)
    {
        if (field is null) return;
        try
        {
            var xml = _ceXml.GenerateFieldXml(field);
            if (!string.IsNullOrEmpty(xml))
            {
                await _platform.CopyToClipboardAsync(xml);
                _log.Info("view", $"Copied CE field '{field.Name}' to clipboard");
            }
        }
        catch (Exception ex)
        {
            SetError($"CE field export failed: {ex.Message}");
            _log.Error("view", "CE field export failed", ex);
        }
    }

    [RelayCommand]
    private async Task ExportCsxAsync()
    {
        if (Fields.Count == 0 || string.IsNullOrEmpty(CurrentClassName)) return;
        try
        {
            var fields = Fields.ToList();
            var csx = await _csx.GenerateCsxAsync(CurrentClassName, fields);
            if (!string.IsNullOrEmpty(csx))
            {
                await _platform.CopyToClipboardAsync(csx);
                _log.Info("view", $"Copied CSX for '{CurrentClassName}' to clipboard");
            }
        }
        catch (Exception ex)
        {
            SetError($"CSX export failed: {ex.Message}");
            _log.Error("view", "CSX export failed", ex);
        }
    }

    [RelayCommand]
    private async Task ExportSdkAsync()
    {
        if (Fields.Count == 0 || string.IsNullOrEmpty(CurrentClassName)) return;
        try
        {
            var fields = Fields.ToList();
            var header = _sdk.GenerateClassHeader(CurrentClassName, "", fields);
            if (!string.IsNullOrEmpty(header))
            {
                await _platform.CopyToClipboardAsync(header);
                _log.Info("view", $"Copied SDK header for '{CurrentClassName}' to clipboard");
            }
        }
        catch (Exception ex)
        {
            SetError($"SDK export failed: {ex.Message}");
            _log.Error("view", "SDK export failed", ex);
        }
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
