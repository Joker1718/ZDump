using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ZoltDumpUI.Core;
using ZoltDumpUI.Models;

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

        NodeTree = new NodeTreeViewModel(dump, log, platform);
        LiveWalker = new LiveWalkerViewModel(dump, log, platform, aobMaker);
        ClassStruct = new ClassStructViewModel(dump, log);
        InstanceFinder = new InstanceFinderViewModel(dump, log, platform);
        Pointers = new PointerPanelViewModel(dump, log, platform, aobMaker);
        PropertySearch = new PropertySearchViewModel(dump, log, platform);
        NodeFilter = new NodeFilterViewModel(dump, log);
        ProxyDeploy = new ProxyDeployViewModel(proxyDeploy, log);

        // Wire cross-VM events
        NodeTree.SelectionChanged += OnNodeTreeSelectionChanged;
        InstanceFinder.NavigateToLiveWalker += OnNavigateToLiveWalker;
        NodeFilter.NavigateToInstanceFinder += OnNavigateToInstanceFinder;
        PropertySearch.NavigateToInstanceFinder += OnNavigateToInstanceFinder;

        _pipe.ConnectionStateChanged += connected =>
        {
            IsConnected = connected;
            StatusText = connected ? "Connected" : "Disconnected";
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
            StatusText = $"Connected — Godot {state.GodotVersion}";
            _log.Info("Connected to game process");

            // Load pointers
            await Pointers.LoadPointersAsync();
        }
        catch (Exception ex)
        {
            SetError($"Connect failed: {ex.Message}");
            StatusText = "Disconnected";
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

            StatusText = "Scan complete";
            await Pointers.LoadPointersAsync();
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
