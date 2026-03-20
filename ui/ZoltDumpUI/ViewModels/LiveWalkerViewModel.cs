using System.Collections.ObjectModel;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ZoltDumpUI.Core;
using ZoltDumpUI.Models;
using ZoltDumpUI.Services;
using ZoltDumpUI.Views;

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
    [ObservableProperty] private string _statusMessage = "";
    [ObservableProperty] private bool _isNode;
    /// <summary>
    /// Set by Back/navigation to request the View scroll to and select a specific field by name.
    /// The View observes this property change and scrolls the DataGrid accordingly.
    /// Reset to null after the View processes it.
    /// </summary>
    [ObservableProperty] private string? _pendingScrollTarget;

    /// <summary>Module name (process name) for CE XML export address formatting.</summary>
    public string ModuleName { get; set; } = "";

    /// <summary>Set a transient status message (auto-clears after 3 seconds).</summary>
    private void SetStatus(string message)
    {
        StatusMessage = message;
        _log.Info("view", message);
        // Auto-clear after 3 seconds
        _ = ClearStatusAfterDelayAsync();
    }

    private async Task ClearStatusAfterDelayAsync()
    {
        await Task.Delay(3000);
        StatusMessage = "";
    }

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

    public async Task NavigateToAddressAsync(string address, string? sourceFieldName = null)
    {
        await FetchAndShowAsync(address);
        // Add breadcrumb only on navigation (not refresh)
        if (!string.IsNullOrEmpty(CurrentAddress))
            Breadcrumbs.Add(new BreadcrumbItem(CurrentAddress, CurrentName, CurrentClassName,
                isNodeObject: IsNode, memberName: sourceFieldName));
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
            IsNode = result.IsNode;

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
        // Remember the member name we came from (to scroll to it after loading)
        var leaving = Breadcrumbs[^1];
        string? scrollTarget = leaving.MemberName; // e.g. "values" when leaving values(Dict)
        Breadcrumbs.RemoveAt(Breadcrumbs.Count - 1);
        var prev = Breadcrumbs[^1];
        if (prev.IsContainer)
            await FetchAndShowContainerAsync(prev.Address, prev.VariantType, prev.Name);
        else
            await FetchAndShowAsync(prev.Address);
        // Request the View to scroll to the field we navigated from
        if (!string.IsNullOrEmpty(scrollTarget))
            PendingScrollTarget = scrollTarget;
    }

    [RelayCommand]
    private async Task NavigateToParentAsync()
    {
        if (string.IsNullOrEmpty(ParentAddr)) return;
        await NavigateToAddressAsync(ParentAddr);
    }

    /// <summary>
    /// Name of the currently selected field in the DataGrid.
    /// Set by the View; used to restore selection after Refresh.
    /// </summary>
    [ObservableProperty] private string? _selectedFieldName;

    [RelayCommand]
    private async Task RefreshAsync()
    {
        if (string.IsNullOrEmpty(CurrentAddress)) return;
        // Remember selected field to restore after refresh
        string? restoreField = SelectedFieldName;

        // If current view is a container drill-down, re-fetch container instead of instance
        var current = Breadcrumbs.Count > 0 ? Breadcrumbs[^1] : null;
        if (current?.IsContainer == true)
            await FetchAndShowContainerAsync(current.Address, current.VariantType, current.Name);
        else
            await FetchAndShowAsync(CurrentAddress);

        // Restore selection and scroll position
        if (!string.IsNullOrEmpty(restoreField))
            PendingScrollTarget = restoreField;
    }

    [RelayCommand]
    private async Task NavigatePointerAsync(LiveFieldValue field)
    {
        if (!field.IsPointer || string.IsNullOrEmpty(field.PtrValue)) return;
        await NavigateToAddressAsync(field.PtrValue, sourceFieldName: field.Name);
    }

    [RelayCommand]
    internal async Task NavigateContainerAsync(LiveFieldValue field)
    {
        if (!field.IsContainer || string.IsNullOrEmpty(field.Address)) return;
        try
        {
            ClearError();
            IsLoading = true;

            var result = await _dump.WalkContainerAsync(field.Address, field.VariantType);

            // Update header to show container context
            string containerLabel = $"{field.Name} ({result.ContainerType}[{result.ElementCount}])";
            CurrentName = containerLabel;
            // Keep CurrentAddress/CurrentClassName from parent — we're drilling into a field
            IsNode = false;

            Fields.Clear();
            foreach (var f in result.Fields)
                Fields.Add(f);

            // Add breadcrumb for container navigation (with variant type for re-fetch)
            // Store the original member name (field.Name before container decoration)
            Breadcrumbs.Add(new BreadcrumbItem(
                field.Address, containerLabel, result.ContainerType,
                isContainer: true, variantType: field.VariantType,
                memberName: field.Name));

            _log.Debug("view", $"Container drilldown: {containerLabel} ({result.Fields.Count} elements)");
        }
        catch (Exception ex)
        {
            SetError($"Container walk failed: {ex.Message}");
            _log.Error("view", "Container walk failed", ex);
        }
        finally
        {
            IsLoading = false;
        }
    }

    /// <summary>Re-fetch and display a container without modifying breadcrumbs (for Back/Refresh).</summary>
    private async Task FetchAndShowContainerAsync(string address, int variantType, string label)
    {
        try
        {
            ClearError();
            IsLoading = true;

            var result = await _dump.WalkContainerAsync(address, variantType);
            CurrentName = label;
            IsNode = false;

            Fields.Clear();
            foreach (var f in result.Fields)
                Fields.Add(f);
        }
        catch (Exception ex)
        {
            SetError($"Container refresh failed: {ex.Message}");
            _log.Error("view", "Container refresh failed", ex);
        }
        finally
        {
            IsLoading = false;
        }
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
    internal async Task CopyFieldAddressAsync(LiveFieldValue field)
    {
        await _platform.CopyToClipboardAsync(field.Address);
        SetStatus($"Address copied: {field.Address}");
    }

    [RelayCommand]
    internal async Task CopyFieldNameAsync(LiveFieldValue field)
    {
        await _platform.CopyToClipboardAsync(field.Name);
        SetStatus($"Name copied: {field.Name}");
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
    internal async Task GenerateCeScriptAsync(LiveFieldValue? field)
    {
        if (field is null || string.IsNullOrEmpty(CurrentName)) return;
        try
        {
            ClearError();
            string memberName = field.Name;
            string script;
            string label;
            string symbolName; // Shared across all branches — used for both Lua registerSymbol AND memory record

            // ── Container context detection ──
            // When inside a container drill-down (e.g. Data → values (Dict[477]) → inventory.iron),
            // walk breadcrumbs back to find the parent object and generate a script that:
            //   1. Finds the parent object (by node name or class+member)
            //   2. Resolves the GDScript member (e.g. "values")
            //   3. Walks the dictionary to find the element by key (e.g. "inventory.iron")
            var containerContext = FindContainerContext();
            if (containerContext is not null)
            {
                string dictKey = ExtractContainerKey(field.Name);
                string parentMember = containerContext.Value.memberName;

                if (containerContext.Value.isNode)
                {
                    // Node parent: FIND_NODE → GET_MEMBER_ADDR + dict key
                    string nodeName = containerContext.Value.parentName;
                    symbolName = CeLuaScriptService.MakeSymbolName(nodeName, dictKey);
                    script = _ceLua.GenerateMemberScript(nodeName, parentMember, symbolName,
                        containerKey: dictKey);
                    label = $"{nodeName}.{parentMember}[{dictKey}]";
                }
                else
                {
                    // Non-Node parent: FIND_INSTANCE_WITH_MEMBER + dict key
                    string className = containerContext.Value.parentClassName;
                    symbolName = CeLuaScriptService.MakeSymbolName(className, dictKey);
                    script = _ceLua.GenerateFindInstanceMemberScript(
                        className, parentMember, criteria: null, symbolName,
                        containerKey: dictKey);
                    label = $"{className}.{parentMember}[{dictKey}]";
                }
            }
            else if (IsNode)
            {
                string nodeName = CurrentName;

                // Detect @ClassName@NNN pattern — node name changes on restart
                // Offer path-match approach for GDScript members on such nodes
                if (field.Offset < 0 && IsAutoGeneratedNodeName(nodeName))
                {
                    // Show PathMatchDialog for stable identification
                    var pathCriteria = await ShowPathMatchDialogAsync(
                        CurrentClassName, memberName, nodeName);
                    if (pathCriteria is null)
                    {
                        SetStatus("CE script generation cancelled");
                        return;
                    }

                    symbolName = CeLuaScriptService.MakeSymbolName(
                        pathCriteria.MatchValue, memberName);
                    script = _ceLua.GenerateFindInstanceByPathScript(
                        pathCriteria.ClassName, pathCriteria.MatchPath,
                        pathCriteria.MatchValue, memberName, symbolName);
                    label = $"{pathCriteria.MatchPath}={pathCriteria.MatchValue} → {memberName}";
                }
                else
                {
                    // Normal node: use FIND_NODE by node name
                    symbolName = CeLuaScriptService.MakeSymbolName(nodeName, memberName);

                    if (field.Offset >= 0)
                    {
                        // Native property with known offset: FIND_NODE + direct offset
                        script = _ceLua.GenerateNativeOffsetScript(nodeName, memberName, field.Offset, symbolName);
                    }
                    else
                    {
                        // GDScript member (offset < 0): FIND_NODE → GET_MEMBER_ADDR
                        script = _ceLua.GenerateMemberScript(nodeName, memberName, symbolName);
                    }
                    label = $"{nodeName}.{memberName}";
                }
            }
            else
            {
                // Non-Node (Reference, Resource, etc.): decide between GDScript and .NET managed mode.
                string className = CurrentClassName;

                // Detect managed mode: if we have positive-offset int/float fields
                // and no script members (Offset < 0), this is a .NET managed object.
                bool isManagedMode = IsManagedObjectContext();

                if (isManagedMode)
                {
                    // .NET managed mode: use CMD_FIND_MANAGED_INSTANCES (bulk scan)
                    var managedSiblings = GetManagedSiblingFields(field);

                    var criteria = await ShowManagedFilterDialogAsync(
                        memberName, field.Offset, managedSiblings);
                    if (criteria is null)
                    {
                        SetStatus("CE script generation cancelled");
                        return;
                    }

                    // Enrich criteria with .NET reflection info for update-resilient scripts.
                    // field.ClassName is the .NET type (e.g. "MegaCrit...Creature") from Denken.
                    // memberName is the field name (e.g. "_currentHp") if Denken-resolved.
                    if (!string.IsNullOrEmpty(field.ClassName) && field.ClassName != "(managed)")
                    {
                        criteria.DotNetTypeName = field.ClassName;
                        if (!memberName.StartsWith("+0x", StringComparison.OrdinalIgnoreCase))
                            criteria.TargetFieldName = memberName;
                    }

                    // Use the Godot class from breadcrumbs (the parent object's class)
                    string godotClass = GetParentGodotClass() ?? className;
                    // Get node name for narrowing the scan (e.g. "Creature")
                    string? nodeName = GetParentNodeName();
                    // When node name filter is available, skip MT — auto-discover
                    // is reliable with the narrowed object set. MT changes every
                    // restart so hardcoding it makes scripts single-use.
                    string? mtHex = nodeName is null
                        ? GetCurrentMethodTableHex()
                        : null;
                    // L2 child ref offset: wrapper field that references the child
                    // (e.g. 0x58 when navigated wrapper → +0x58 → child)
                    int l2ChildRefOffset = GetL2ChildRefOffset();
                    symbolName = CeLuaScriptService.MakeSymbolName(godotClass, $"managed_0x{field.Offset:X}");
                    script = _ceLua.GenerateManagedInstanceScript(
                        godotClass, criteria, symbolName, memberName, mtHex, nodeName,
                        l2ChildRefOffset);
                    label = criteria.FindAll
                        ? $"{godotClass} .NET managed +0x{field.Offset:X} (all)"
                        : $"{godotClass} .NET managed +0x{field.Offset:X}";
                }
                else
                {
                    // GDScript mode: FIND_INSTANCE_WITH_MEMBER with value-based filtering.
                    var siblings = GetBaseTypeSiblingMembers(memberName);

                    var criteria = await ShowMemberFilterDialogAsync(memberName, siblings);
                    if (criteria is null)
                    {
                        SetStatus("CE script generation cancelled");
                        return;
                    }

                    symbolName = CeLuaScriptService.MakeSymbolName(className, memberName);
                    script = _ceLua.GenerateFindInstanceMemberScript(className, memberName, criteria, symbolName);
                    label = $"{className}.{memberName}";
                }
            }

            // Always copy to clipboard so user has the script
            await _platform.CopyToClipboardAsync(script);

            // Also try AOBMaker: send AA script + create memory record as flat entries
            // Use the SAME symbolName that the Lua script uses for registerSymbol
            bool sent = await TrySendToAobMakerAsync($"ZoltDump: {label}", script);

            if (sent)
            {
                // For multi-result managed mode, the Lua script creates its own group header
                // + child memory records. Don't create an extra single-address record.
                bool isManagedMultiResult = label.Contains(".NET managed") && label.Contains("(all)");
                if (!isManagedMultiResult)
                {
                    var ceType = MapGodotTypeToCeValueType(field.Type);
                    try
                    {
                        await _aobMaker.CreateMemoryRecordAsync(
                            label,
                            symbolName,
                            ceType,
                            isSigned: ceType is CeValueType.Dword or CeValueType.Qword);
                    }
                    catch (Exception ex)
                    {
                        _log.Debug("view", $"CreateMemoryRecord failed: {ex.Message}");
                    }
                }
            }

            SetStatus(sent
                ? $"CE script + memory record for '{label}' sent to AOBMaker"
                : $"CE script for '{label}' copied to clipboard");
        }
        catch (Exception ex)
        {
            SetError($"CE script generation failed: {ex.Message}");
            _log.Error("view", "CE script generation failed", ex);
        }
    }

    /// <summary>
    /// Generate CE Lua script using path match approach (always shows PathMatchDialog).
    /// Callable from context menu "Generate CE Script (Path Match)".
    /// </summary>
    [RelayCommand]
    internal async Task GeneratePathMatchScriptAsync(LiveFieldValue? field)
    {
        if (field is null || string.IsNullOrEmpty(CurrentName)) return;
        try
        {
            ClearError();
            string memberName = field.Name;

            var pathCriteria = await ShowPathMatchDialogAsync(
                CurrentClassName, memberName, CurrentName);
            if (pathCriteria is null)
            {
                SetStatus("CE script generation cancelled");
                return;
            }

            string symbolName = CeLuaScriptService.MakeSymbolName(
                pathCriteria.MatchValue, memberName);
            string script = _ceLua.GenerateFindInstanceByPathScript(
                pathCriteria.ClassName, pathCriteria.MatchPath,
                pathCriteria.MatchValue, memberName, symbolName);
            string label = $"{pathCriteria.MatchPath}={pathCriteria.MatchValue} → {memberName}";

            await _platform.CopyToClipboardAsync(script);

            bool sent = await TrySendToAobMakerAsync($"ZoltDump: {label}", script);
            if (sent)
            {
                var ceType = MapGodotTypeToCeValueType(field.Type);
                try
                {
                    await _aobMaker.CreateMemoryRecordAsync(
                        label, symbolName, ceType,
                        isSigned: ceType is CeValueType.Dword or CeValueType.Qword);
                }
                catch (Exception ex)
                {
                    _log.Debug("view", $"CreateMemoryRecord failed: {ex.Message}");
                }
            }

            SetStatus(sent
                ? $"CE script + memory record for '{label}' sent to AOBMaker"
                : $"CE script for '{label}' copied to clipboard");
        }
        catch (Exception ex)
        {
            SetError($"CE script generation (path match) failed: {ex.Message}");
            _log.Error("view", "CE script generation (path match) failed", ex);
        }
    }

    // ============================================================
    // Container context helpers for CE Lua generation
    // ============================================================

    /// <summary>
    /// Walk breadcrumbs backwards to find the parent object context when inside a container.
    /// Returns null if not inside a container drill-down.
    ///
    /// Currently supports single-level container depth only (parent → Dict/Array → element).
    /// Multi-level nested containers (Dict → Dict → element) return null because the DLL's
    /// matchStr field only supports one key lookup per command.
    /// </summary>
    private (string parentName, string parentClassName, string memberName, bool isNode)?
        FindContainerContext()
    {
        // Need at least 2 breadcrumbs: [parent object] → [container]
        if (Breadcrumbs.Count < 2) return null;

        // Check if the most recent breadcrumb is a container
        var containerCrumb = Breadcrumbs[^1];
        if (!containerCrumb.IsContainer) return null;

        // The previous breadcrumb must be the parent OBJECT (not another container).
        // If it's also a container, we're in a nested container → not supported yet.
        var parentCrumb = Breadcrumbs[^2];
        if (parentCrumb.IsContainer) return null; // multi-level nested — not supported

        // The member name that leads to this container (stored when navigating into container)
        string? memberName = containerCrumb.MemberName;
        if (string.IsNullOrEmpty(memberName)) return null;

        return (parentCrumb.Name, parentCrumb.ClassName, memberName, parentCrumb.IsNodeObject);
    }

    /// <summary>
    /// Extract the dictionary key or array index from a container element display name.
    /// Strips the " → [index]" suffix that the DLL adds for display.
    /// Examples:
    ///   "inventory.iron → [384]" → "inventory.iron" (dict string key)
    ///   "(Int)42 → [5]"         → "(Int)42"         (dict non-string key)
    ///   "[5]"                    → "5"               (array index)
    /// </summary>
    private static string ExtractContainerKey(string fieldName)
    {
        // Dict elements: "key → [index]" — extract the key part
        int arrowIdx = fieldName.IndexOf(" \u2192 [", StringComparison.Ordinal);
        if (arrowIdx >= 0)
            return fieldName[..arrowIdx];

        // Array elements: "[index]" — extract the numeric index
        if (fieldName.StartsWith('[') && fieldName.EndsWith(']'))
            return fieldName[1..^1];

        // Fallback: use as-is
        return fieldName;
    }

    /// <summary>
    /// Get sibling GDScript members of base types (int, real, bool) for filter criteria.
    /// Excludes the target member itself and generic script_var_N names.
    /// </summary>
    private List<string> GetBaseTypeSiblingMembers(string excludeMember)
    {
        var result = new List<string>();
        foreach (var f in Fields)
        {
            if (f.Name == excludeMember) continue;
            if (f.Name.StartsWith("script_var_", StringComparison.Ordinal)) continue;
            // Script members have Offset < 0 (displayed as "(script)")
            if (f.Offset >= 0) continue;
            // Only base types that can be compared as integers
            string t = f.Type.ToUpperInvariant();
            if (t is "INT" or "FLOAT" or "REAL" or "BOOL")
            {
                result.Add(f.Name);
            }
        }
        return result;
    }

    /// <summary>
    /// Show the member filter criteria dialog and return the result (GDScript mode).
    /// Returns null if the user cancels.
    /// </summary>
    private static async Task<MemberFilterCriteria?> ShowMemberFilterDialogAsync(
        string targetMember, List<string> siblingMembers)
    {
        if (Application.Current?.ApplicationLifetime is not IClassicDesktopStyleApplicationLifetime desktop)
            return null;

        var window = desktop.MainWindow;
        if (window is null) return null;

        var dialog = new MemberFilterDialog(targetMember, siblingMembers);
        await dialog.ShowDialog(window);
        return dialog.Result;
    }

    /// <summary>
    /// Show the managed filter criteria dialog (.NET mode) with offset-based fields.
    /// Returns null if the user cancels.
    /// </summary>
    private static async Task<MemberFilterCriteria?> ShowManagedFilterDialogAsync(
        string targetMember, int targetOffset,
        List<MemberFilterDialog.ManagedFieldInfo> managedSiblings)
    {
        if (Application.Current?.ApplicationLifetime is not IClassicDesktopStyleApplicationLifetime desktop)
            return null;

        var window = desktop.MainWindow;
        if (window is null) return null;

        var dialog = new MemberFilterDialog(targetMember, targetOffset, managedSiblings);
        await dialog.ShowDialog(window);
        return dialog.Result;
    }

    /// <summary>
    /// Detect if the current view is showing .NET managed object fields.
    /// Managed objects have no GDScript members (Offset &lt; 0) and have
    /// positive-offset fields with names like "+0x84".
    /// </summary>
    private bool IsManagedObjectContext()
    {
        bool hasScriptMembers = false;
        bool hasManagedFields = false;

        foreach (var f in Fields)
        {
            if (f.Offset < 0) hasScriptMembers = true;
            // Managed fields have non-negative offsets — either raw "+0x84" names
            // (heuristic scan) or Denken-resolved names like "_currentHp".
            if (f.Offset >= 0) hasManagedFields = true;
        }

        // Managed mode: has positive-offset fields and no GDScript members
        return hasManagedFields && !hasScriptMembers;
    }

    /// <summary>
    /// Get sibling managed fields for the filter dropdown (excluding the target field).
    /// Returns fields with their current values for reference.
    /// </summary>
    private List<MemberFilterDialog.ManagedFieldInfo> GetManagedSiblingFields(LiveFieldValue excludeField)
    {
        var result = new List<MemberFilterDialog.ManagedFieldInfo>();
        foreach (var f in Fields)
        {
            if (f == excludeField) continue;
            if (f.Offset < 0) continue; // skip script members
            // Include fields with known primitive types, OR fields whose
            // current value is a parseable integer (covers C# enums like
            // CombatSide which are stored as int32 but have custom type names).
            string t = f.Type.ToUpperInvariant();
            int size = GetManagedFieldSize(t);
            if (size <= 0)
            {
                // Unknown type — check if value or hex looks numeric (covers
                // C# enums like CombatSide whose Value shows the type reference
                // but Hex shows the underlying integer, e.g. "1")
                if (IsNumericValue(f.Value) || IsNumericValue(f.Hex))
                    size = 4; // assume int32 for enums and unknown small types
                else
                    continue;
            }
            // For enum/unknown types, Value may show a type reference (e.g.
            // "MegaCrit...CombatSide@1F86..."). Use Hex value instead for display.
            string displayValue = IsNumericValue(f.Value) || string.IsNullOrEmpty(f.Value)
                ? f.Value
                : f.Hex;
            result.Add(new MemberFilterDialog.ManagedFieldInfo(
                f.Name, f.Offset, f.Type, displayValue, size));
        }
        return result;
    }

    /// <summary>
    /// Get the byte size of a managed field type for CE filter comparison.
    /// Returns 0 if the type is not a recognized numeric/bool type.
    /// </summary>
    private static int GetManagedFieldSize(string typeUpperInvariant)
    {
        return typeUpperInvariant switch
        {
            // Godot-style names
            "INT" or "FLOAT" or "REAL" or "BOOL" => 4,
            // C# type names from Denken reflection
            "SYSTEM.INT32" or "SYSTEM.UINT32" or "SYSTEM.SINGLE" => 4,
            "SYSTEM.INT64" or "SYSTEM.UINT64" or "SYSTEM.DOUBLE" => 8,
            "SYSTEM.INT16" or "SYSTEM.UINT16" => 2,
            "SYSTEM.BYTE" or "SYSTEM.SBYTE" or "SYSTEM.BOOLEAN" => 1,
            // Short C# names (used by some DLL paths)
            "INT32" or "UINT32" or "SINGLE" => 4,
            "INT64" or "UINT64" or "DOUBLE" or "LONG" => 8,
            "INT16" or "UINT16" or "SHORT" => 2,
            "BYTE" or "SBYTE" or "BOOLEAN" => 1,
            _ => 0,
        };
    }

    /// <summary>
    /// Check if a value string represents a numeric value (integer or "true"/"false").
    /// Used to detect C# enums and other value types with integer storage.
    /// </summary>
    private static bool IsNumericValue(string value)
    {
        if (string.IsNullOrEmpty(value)) return false;
        // Boolean values stored as 0/1
        if (value is "true" or "false") return true;
        // Integer value (decimal or hex)
        if (long.TryParse(value, out _)) return true;
        if (value.StartsWith("0x", StringComparison.OrdinalIgnoreCase) &&
            long.TryParse(value.AsSpan(2), System.Globalization.NumberStyles.HexNumber,
                null, out _)) return true;
        return false;
    }

    /// <summary>
    /// Get the Godot class name from the parent breadcrumb (before managed object drill-down).
    /// When viewing a managed object, the current class may be "ManagedObject" or empty.
    /// Walk back through breadcrumbs to find the original Godot class.
    /// </summary>
    private string? GetParentGodotClass()
    {
        for (int i = Breadcrumbs.Count - 1; i >= 0; i--)
        {
            var crumb = Breadcrumbs[i];
            if (!string.IsNullOrEmpty(crumb.ClassName) &&
                crumb.ClassName != "ManagedObject")
            {
                return crumb.ClassName;
            }
        }
        return null;
    }

    /// <summary>
    /// Get the Godot node name from the parent breadcrumb (e.g. "Creature").
    /// Used as a node name filter for CMD_FIND_MANAGED_INSTANCES to prevent
    /// unrelated objects (Game, HealthBar) from polluting auto-discover MT.
    ///
    /// For auto-generated node names like "@Control@1978", strips the trailing
    /// instance number to produce "@Control@" — this matches any instance of
    /// that pattern since the number changes per session/battle.
    /// </summary>
    private string? GetParentNodeName()
    {
        for (int i = Breadcrumbs.Count - 1; i >= 0; i--)
        {
            var crumb = Breadcrumbs[i];
            if (!string.IsNullOrEmpty(crumb.ClassName) &&
                crumb.ClassName != "ManagedObject" &&
                !string.IsNullOrEmpty(crumb.Name))
            {
                return NormalizeNodeNameForFilter(crumb.Name);
            }
        }
        return null;
    }

    /// <summary>
    /// Normalize a Godot node name for use as a CMD_FIND_MANAGED_INSTANCES filter.
    /// Godot auto-generated names follow pattern "@ClassName@NNN" where NNN is a
    /// session-specific instance ID that changes every battle/scene load.
    /// Strip the trailing digits to produce "@ClassName@" for stable matching.
    /// Regular names (e.g. "Creature") are returned as-is.
    /// </summary>
    private static string NormalizeNodeNameForFilter(string name)
    {
        // Pattern: "@Something@1234" → "@Something@"
        // Must start with '@' and have a second '@' followed by only digits
        if (name.Length > 2 && name[0] == '@')
        {
            int secondAt = name.IndexOf('@', 1);
            if (secondAt > 1 && secondAt < name.Length - 1)
            {
                // Check if everything after second '@' is digits
                bool allDigits = true;
                for (int j = secondAt + 1; j < name.Length; j++)
                {
                    if (!char.IsDigit(name[j]))
                    {
                        allDigits = false;
                        break;
                    }
                }
                if (allDigits)
                    return name[..(secondAt + 1)]; // "@Control@"
            }
        }
        return name;
    }

    /// <summary>
    /// Extract the .NET MethodTable hex value from the current breadcrumb context.
    /// Breadcrumb names for managed objects follow the pattern: "MT:7FF7D90BE0F8 (size=0xB0)"
    /// Returns the hex string (e.g. "7FF7D90BE0F8") or null if not found.
    /// </summary>
    private string? GetCurrentMethodTableHex()
    {
        // Check current name first, then walk breadcrumbs
        string? ExtractMT(string name)
        {
            const string prefix = "MT:";
            int idx = name.IndexOf(prefix, StringComparison.OrdinalIgnoreCase);
            if (idx < 0) return null;
            int start = idx + prefix.Length;
            int end = start;
            while (end < name.Length && Uri.IsHexDigit(name[end])) end++;
            return end > start ? name[start..end] : null;
        }

        // Current object might be the managed object
        var mt = ExtractMT(CurrentName);
        if (mt is not null) return mt;

        // Walk breadcrumbs backwards — the managed object entry has the MT
        for (int i = Breadcrumbs.Count - 1; i >= 0; i--)
        {
            mt = ExtractMT(Breadcrumbs[i].Name);
            if (mt is not null) return mt;
        }

        return null;
    }

    /// <summary>
    /// Get the L2 child reference offset within the wrapper managed object.
    /// Two navigation patterns reach a child managed object:
    ///   1. Godot node (shows wrapper inline) → click +0x58 (ref) → child ManagedObject
    ///   2. Godot node → wrapper ManagedObject → click +0x58 (ref) → child ManagedObject
    /// In both cases, the current breadcrumb has ClassName="ManagedObject" and
    /// MemberName="+0x58 (ref)" — the wrapper field offset to the child.
    /// Returns -1 if no L2 navigation detected (data is directly in the wrapper).
    /// </summary>
    private int GetL2ChildRefOffset()
    {
        if (Breadcrumbs.Count < 2) return -1;

        // Current breadcrumb (the managed child we're viewing)
        var current = Breadcrumbs[^1];
        if (current.ClassName != "ManagedObject") return -1;

        // The field used to navigate here should be like "+0x58 (ref)"
        string? memberName = current.MemberName;
        if (string.IsNullOrEmpty(memberName)) return -1;

        // Parse "+0xNN" or "+0xNN (ref)" pattern
        if (memberName.StartsWith("+0x", StringComparison.OrdinalIgnoreCase))
        {
            int end = 3;
            while (end < memberName.Length && Uri.IsHexDigit(memberName[end])) end++;
            if (end > 3 && int.TryParse(memberName[3..end], System.Globalization.NumberStyles.HexNumber,
                null, out int offset))
            {
                // Previous breadcrumb can be either:
                //   - A Godot node (wrapper data shown inline) — most common
                //   - A ManagedObject (explicit wrapper navigation)
                // Both are valid L2 sources. The offset is always within the wrapper.
                return offset;
            }
        }
        return -1;
    }

    /// <summary>
    /// Detect auto-generated node names like "@ClassName@123" that change on restart.
    /// These follow the Godot pattern: @ScriptName@InstanceID
    /// </summary>
    private static bool IsAutoGeneratedNodeName(string nodeName)
    {
        // Pattern: @Something@Digits or @@Digits
        if (!nodeName.StartsWith('@')) return false;
        int lastAt = nodeName.LastIndexOf('@');
        if (lastAt <= 0 || lastAt == nodeName.Length - 1) return false;
        // After last '@', should be digits
        for (int i = lastAt + 1; i < nodeName.Length; i++)
        {
            if (!char.IsDigit(nodeName[i])) return false;
        }
        return true;
    }

    /// <summary>
    /// Get Object-typed script member names (for path match auto-suggestion).
    /// </summary>
    private List<string> GetObjectTypeSiblingMembers()
    {
        var result = new List<string>();
        foreach (var f in Fields)
        {
            if (f.Name.StartsWith("script_var_", StringComparison.Ordinal)) continue;
            if (f.Offset >= 0) continue; // only script members
            if (f.Type.Equals("Object", StringComparison.OrdinalIgnoreCase))
            {
                result.Add(f.Name);
            }
        }
        return result;
    }

    /// <summary>
    /// Show the path match criteria dialog and return the result.
    /// Returns null if the user cancels.
    /// </summary>
    private async Task<PathMatchCriteria?> ShowPathMatchDialogAsync(
        string className, string memberName, string nodeName)
    {
        if (Application.Current?.ApplicationLifetime is not IClassicDesktopStyleApplicationLifetime desktop)
            return null;

        var window = desktop.MainWindow;
        if (window is null) return null;

        var objectMembers = GetObjectTypeSiblingMembers();
        var dialog = new PathMatchDialog(className, memberName, nodeName, objectMembers);
        await dialog.ShowDialog(window);
        return dialog.Result;
    }

    /// <summary>Map Godot Variant type string to CE TVariableType.</summary>
    /// <remarks>Handles both Godot 4.x and 3.x type names.</remarks>
    private static CeValueType MapGodotTypeToCeValueType(string godotType) => godotType.ToUpperInvariant() switch
    {
        "BOOL" => CeValueType.Byte,
        "INT" => CeValueType.Qword,                      // Godot Variant::INT = int64 (8 bytes)
        "REAL" => CeValueType.Double,                     // Godot 3.x real_t = always double
        "FLOAT" => CeValueType.Double,                    // Godot 4.x Variant::FLOAT = double (8 bytes)
        "STRING" or "STRING_NAME" or "NODE_PATH" => CeValueType.String,
        "OBJECT" => CeValueType.Qword,
        _ => CeValueType.Dword
    };

    [RelayCommand]
    private async Task GenerateInstanceScriptAsync()
    {
        if (string.IsNullOrEmpty(CurrentName)) return;
        try
        {
            string script;
            string label;

            if (IsNode)
            {
                // Node-derived: use FIND_NODE (by node name) — unique in scene tree
                script = _ceLua.GenerateFindNodeScript(CurrentName);
                label = $"Find {CurrentName}";
            }
            else
            {
                // Non-Node: use FIND_INSTANCE (by class name)
                script = _ceLua.GenerateFindInstanceScript(CurrentClassName);
                label = $"Find {CurrentClassName}";
            }

            bool sent = await TrySendToAobMakerAsync($"ZoltDump: {label}", script);

            if (!sent)
                await _platform.CopyToClipboardAsync(script);
        }
        catch (Exception ex)
        {
            SetError($"Instance script generation failed: {ex.Message}");
            _log.Error("view", "Instance script generation failed", ex);
        }
    }

    /// <summary>Try sending an AA script to AOBMaker CE plugin. Returns true if sent.</summary>
    private async Task<bool> TrySendToAobMakerAsync(string label, string script)
    {
        try
        {
            if (await _aobMaker.CheckAvailabilityAsync())
            {
                await _aobMaker.CreateAAScriptAsync(label, script);
                _log.Info("view", $"CE script '{label}' sent to AOBMaker");
                return true;
            }
        }
        catch (Exception ex)
        {
            _log.Debug("view", $"AOBMaker unavailable: {ex.Message}");
        }
        return false;
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
                SetStatus($"CE XML copied ({fields.Count} fields)");
            }
        }
        catch (Exception ex)
        {
            SetError($"CE XML export failed: {ex.Message}");
            _log.Error("view", "CE XML export failed", ex);
        }
    }

    [RelayCommand]
    internal async Task CopyCeFieldAsync(LiveFieldValue? field)
    {
        if (field is null) return;
        try
        {
            ClearError();
            var xml = _ceXml.GenerateFieldXml(field);
            if (!string.IsNullOrEmpty(xml))
            {
                await _platform.CopyToClipboardAsync(xml);
                SetStatus($"CE field '{field.Name}' XML copied to clipboard");
            }
            else
            {
                SetError($"CE field '{field.Name}': no exportable data (type={field.Type}, offset={field.Offset})");
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
                SetStatus($"CSX for '{CurrentClassName}' copied to clipboard");
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
                SetStatus($"SDK header for '{CurrentClassName}' copied to clipboard");
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
                    string? restoreField = SelectedFieldName;
                    var result = await _dump.WalkInstanceAsync(CurrentAddress, ct);
                    Fields.Clear();
                    foreach (var f in result.Fields)
                        Fields.Add(f);
                    if (!string.IsNullOrEmpty(restoreField))
                        PendingScrollTarget = restoreField;
                }
                catch (OperationCanceledException) { break; }
                catch { /* ignore refresh errors */ }
            }
        }
    }

    partial void OnSearchQueryChanged(string value)
    {
        ApplySearch();
    }

    public void ApplySearch()
    {
        _searchMatchIndex = -1;
        if (string.IsNullOrWhiteSpace(SearchQuery))
        {
            foreach (var f in Fields) f.IsSearchMatch = false;
            return;
        }
        LiveFieldValue? firstMatch = null;
        foreach (var f in Fields)
        {
            bool match = f.Name.Contains(SearchQuery, StringComparison.OrdinalIgnoreCase) ||
                         f.Type.Contains(SearchQuery, StringComparison.OrdinalIgnoreCase);
            f.IsSearchMatch = match;
            firstMatch ??= match ? f : null;
        }
        // Scroll to first match
        if (firstMatch is not null)
            PendingScrollTarget = firstMatch.Name;
    }

    /// <summary>Index of current search match for next/prev navigation.</summary>
    private int _searchMatchIndex = -1;

    /// <summary>Navigate to the next search match. Wraps around at the end.</summary>
    public void NextSearchMatch()
    {
        if (string.IsNullOrWhiteSpace(SearchQuery)) return;
        var matches = Fields.Where(f => f.IsSearchMatch).ToList();
        if (matches.Count == 0) return;
        _searchMatchIndex = (_searchMatchIndex + 1) % matches.Count;
        PendingScrollTarget = matches[_searchMatchIndex].Name;
    }
}

/// <summary>Navigation breadcrumb for LiveWalker.</summary>
public sealed class BreadcrumbItem(string address, string name, string className,
    bool isContainer = false, int variantType = -1, string? memberName = null,
    bool isNodeObject = false)
{
    public string Address { get; } = address;
    public string Name { get; } = name;
    public string ClassName { get; } = className;
    /// <summary>True if this breadcrumb represents a container drill-down (Dict/Array).</summary>
    public bool IsContainer { get; } = isContainer;
    /// <summary>Variant type for container re-fetch (Dictionary=27, Array=28, etc.).</summary>
    public int VariantType { get; } = variantType;
    /// <summary>Original GDScript member name that leads to this container (e.g. "values").</summary>
    public string? MemberName { get; } = memberName;
    /// <summary>True if this breadcrumb's object is a Node (uses FIND_NODE for CE scripts).</summary>
    public bool IsNodeObject { get; } = isNodeObject;
    public string Display => string.IsNullOrEmpty(Name) ? Address : Name;
}
