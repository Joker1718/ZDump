using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ZoltDumpUI.Core;
using ZoltDumpUI.Models;

namespace ZoltDumpUI.ViewModels;

/// <summary>Global pointers panel — ObjectDB, ClassDB, SceneTree addresses.</summary>
public partial class PointerPanelViewModel : ViewModelBase
{
    private readonly IDumpService _dump;
    private readonly ILoggingService _log;
    private readonly IPlatformService _platform;
    private readonly IAobMakerBridge _aobMaker;

    [ObservableProperty] private string _godotVersion = "";
    [ObservableProperty] private string _objectDbAddr = "";
    [ObservableProperty] private string _classDbAddr = "";
    [ObservableProperty] private string _sceneTreeAddr = "";
    [ObservableProperty] private string _engineAddr = "";
    [ObservableProperty] private int _objectCount;
    [ObservableProperty] private int _slotCount;
    [ObservableProperty] private int _slotMax;

    // Scan metadata
    [ObservableProperty] private string _objectDbMethod = "";
    [ObservableProperty] private string _classDbMethod = "";
    [ObservableProperty] private string _sceneTreeMethod = "";
    [ObservableProperty] private string _objectDbPatternId = "";
    [ObservableProperty] private string _classDbPatternId = "";
    [ObservableProperty] private string _sceneTreePatternId = "";

    [ObservableProperty] private bool _isLoading;

    public PointerPanelViewModel(IDumpService dump, ILoggingService log, IPlatformService platform, IAobMakerBridge aobMaker)
    {
        _dump = dump;
        _log = log;
        _platform = platform;
        _aobMaker = aobMaker;
    }

    public async Task LoadPointersAsync()
    {
        try
        {
            ClearError();
            IsLoading = true;

            var state = await _dump.GetPointersAsync();
            ObjectDbAddr = state.ObjectDb;
            ClassDbAddr = state.ClassDb;
            SceneTreeAddr = state.SceneTree;
            EngineAddr = state.Engine;
            ObjectCount = state.ObjectCount;
            SlotCount = state.SlotCount;
            SlotMax = state.SlotMax;
            ObjectDbMethod = state.ObjectDbMethod;
            ClassDbMethod = state.ClassDbMethod;
            SceneTreeMethod = state.SceneTreeMethod;
            ObjectDbPatternId = state.ObjectDbPatternId;
            ClassDbPatternId = state.ClassDbPatternId;
            SceneTreePatternId = state.SceneTreePatternId;
        }
        catch (Exception ex)
        {
            SetError($"Failed to load pointers: {ex.Message}");
            _log.Error("Failed to load pointers", ex);
        }
        finally
        {
            IsLoading = false;
        }
    }

    [RelayCommand]
    private async Task CopyAddressAsync(string address)
    {
        await _platform.CopyToClipboardAsync(address);
    }

    [RelayCommand]
    private async Task NavigateHexAsync(string address)
    {
        if (!string.IsNullOrEmpty(address))
            await _aobMaker.NavigateHexViewAsync(address);
    }

    [RelayCommand]
    private async Task NavigateDisasmAsync(string address)
    {
        if (!string.IsNullOrEmpty(address))
            await _aobMaker.NavigateDisassemblerAsync(address);
    }
}
