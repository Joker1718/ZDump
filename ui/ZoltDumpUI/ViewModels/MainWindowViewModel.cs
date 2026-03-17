using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ZoltDumpUI.Core;
using ZoltDumpUI.Models;
using ZoltDumpUI.Services;

namespace ZoltDumpUI.ViewModels;

/// <summary>Main orchestrator ViewModel for the application window.</summary>
public partial class MainWindowViewModel : ViewModelBase
{
    private readonly IPipeClient _pipe;
    private readonly IDumpService _dump;
    private readonly ILoggingService _log;
    private readonly IPlatformService _platform;

    // Child ViewModels
    public NodeTreeViewModel NodeTree { get; }
    public LiveWalkerViewModel LiveWalker { get; }
    public ClassStructViewModel ClassStruct { get; }
    public InstanceFinderViewModel InstanceFinder { get; }
    public PointerPanelViewModel Pointers { get; }
    public PropertySearchViewModel PropertySearch { get; }
    public NodeFilterViewModel NodeFilter { get; }
    public ProxyDeployViewModel ProxyDeploy { get; }

    private const string DefaultTitle = "Zolt-Dump — Godot Engine Dumper";

    [ObservableProperty] private string _windowTitle = DefaultTitle;
    [ObservableProperty] private string _statusText = "Disconnected";
    [ObservableProperty] private bool _isConnected;
    [ObservableProperty] private string _godotVersion = "";
    [ObservableProperty] private int _selectedTabIndex;
    [ObservableProperty] private AddressFormat _addressFormat = AddressFormat.HexNoPrefix;
    [ObservableProperty] private bool _collapsePointers = true;
    [ObservableProperty] private int _arrayLimit = 100;
    [ObservableProperty] private int _csxDepth = 3;

    public MainWindowViewModel(
        IPipeClient pipe,
        IDumpService dump,
        ILoggingService log,
        IPlatformService platform,
        IProxyDeployService proxyDeploy,
        IAobMakerBridge aobMaker)
    {
        _pipe = pipe;
        _dump = dump;
        _log = log;
        _platform = platform;

        var ceXml = new CeXmlExportService(dump);
        var csx = new CsxExportService(dump);
        var sdk = new SdkExportService();
        var ceLua = new CeLuaScriptService();

        NodeTree = new NodeTreeViewModel(dump, log, platform);
        LiveWalker = new LiveWalkerViewModel(dump, log, platform, aobMaker, ceLua, ceXml, csx, sdk);
        ClassStruct = new ClassStructViewModel(dump, log);
        InstanceFinder = new InstanceFinderViewModel(dump, log, platform, ceXml, sdk);
        Pointers = new PointerPanelViewModel(dump, log, platform, aobMaker);
        PropertySearch = new PropertySearchViewModel(dump, log, platform);
        NodeFilter = new NodeFilterViewModel(dump, log);
        ProxyDeploy = new ProxyDeployViewModel(proxyDeploy, log);

        // Wire cross-VM events
        NodeTree.SelectionChanged += OnNodeTreeSelectionChanged;
        InstanceFinder.NavigateToLiveWalker += OnNavigateToLiveWalker;
        NodeFilter.NavigateToInstanceFinder += OnNavigateToInstanceFinder;
        NodeFilter.NavigateToLiveWalker += OnNavigateToLiveWalker;
        PropertySearch.NavigateToInstanceFinder += OnNavigateToInstanceFinder;
        PropertySearch.NavigateToLiveWalker += OnNavigateToLiveWalker;

        _pipe.ConnectionStateChanged += connected =>
        {
            IsConnected = connected;
            StatusText = connected ? "Connected" : "Disconnected";
            if (!connected) WindowTitle = DefaultTitle;
        };
    }

    [RelayCommand]
    private async Task ConnectAsync()
    {
        try
        {
            ClearError();
            StatusText = "Connecting...";
            await _pipe.ConnectAsync();

            var state = await _dump.InitAsync();
            if (!state.Ok)
            {
                SetError($"Init failed: {state.Error}");
                return;
            }

            GodotVersion = state.GodotVersion;
            Pointers.GodotVersion = state.GodotVersion;
            WindowTitle = $"Zolt-Dump — {state.ProcessName} (PID {state.Pid})";
            StatusText = $"Connected — Godot {state.GodotVersion}";
            LiveWalker.ModuleName = state.ProcessName;
            InstanceFinder.ModuleName = state.ProcessName;
            _log.Info("Connected to game process");

            // Load pointers
            await Pointers.LoadPointersAsync();
            StatusText = $"Connected — Godot {state.GodotVersion} ({Pointers.ObjectCount:N0} objects)";

            // Record inject history
            InjectHistoryService.Record(state.ProcessName, state.Pid, state.GodotVersion, Pointers.ObjectCount);
        }
        catch (Exception ex)
        {
            SetError($"Connect failed: {ex.Message}");
            StatusText = "Disconnected";
            WindowTitle = DefaultTitle;
            _log.Error("Connection failed", ex);
        }
    }

    [RelayCommand]
    private async Task DisconnectAsync()
    {
        try
        {
            await _pipe.DisconnectAsync();
            StatusText = "Disconnected";
            GodotVersion = "";
            WindowTitle = DefaultTitle;
            _log.Info("Disconnected");
        }
        catch (Exception ex)
        {
            _log.Error("Disconnect error", ex);
        }
    }

    [RelayCommand]
    private async Task TriggerScanAsync()
    {
        try
        {
            StatusText = "Scanning...";
            await _dump.TriggerScanAsync();

            // Poll scan status
            while (true)
            {
                await Task.Delay(500);
                var status = await _dump.GetScanStatusAsync();
                StatusText = $"Scanning... {status.Progress}/{status.Total}";
                if (status.IsComplete) break;
            }

            await Pointers.LoadPointersAsync();
            StatusText = $"Scan complete — {Pointers.ObjectCount:N0} objects";
        }
        catch (Exception ex)
        {
            SetError($"Scan failed: {ex.Message}");
            _log.Error("Scan failed", ex);
        }
    }

    private void OnNodeTreeSelectionChanged(ObjectEntry? entry)
    {
        if (entry is null) return;
        _ = ClassStruct.LoadFromObjectAsync(entry.Address);
        _ = LiveWalker.NavigateToAddressAsync(entry.Address);
    }

    private void OnNavigateToLiveWalker(string address)
    {
        SelectedTabIndex = 0; // LiveWalker tab
        _ = LiveWalker.NavigateToAddressAsync(address);
    }

    private void OnNavigateToInstanceFinder(string className)
    {
        SelectedTabIndex = 1; // InstanceFinder tab
        _ = InstanceFinder.SearchAsync(className);
    }
}
