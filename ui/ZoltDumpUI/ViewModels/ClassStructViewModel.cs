using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using ZoltDumpUI.Core;
using ZoltDumpUI.Models;

namespace ZoltDumpUI.ViewModels;

/// <summary>
/// DiAgolze — 黃金化 (Golden Transmutation)
/// ClassDB class structure browser.
/// </summary>
public partial class ClassStructViewModel : ViewModelBase
{
    private readonly IDumpService _dump;
    private readonly ILoggingService _log;

    public ObservableCollection<FieldInfoModel> Properties { get; } = [];
    public ObservableCollection<MethodInfoEntry> Methods { get; } = [];

    [ObservableProperty] private string _className = "";
    [ObservableProperty] private string _superName = "";
    [ObservableProperty] private string _classAddr = "";
    [ObservableProperty] private int _propertiesCount;
    [ObservableProperty] private bool _isLoading;

    public ClassStructViewModel(IDumpService dump, ILoggingService log)
    {
        _dump = dump;
        _log = log;
    }

    public async Task LoadClassAsync(string className)
    {
        try
        {
            ClearError();
            IsLoading = true;

            var info = await _dump.WalkClassAsync(className);
            ClassName = info.ClassName;
            SuperName = info.SuperName;
            ClassAddr = info.ClassAddr;
            PropertiesCount = info.PropertiesCount;

            Properties.Clear();
            foreach (var p in info.Properties)
                Properties.Add(p);

            Methods.Clear();
            foreach (var m in info.Methods)
                Methods.Add(m);
        }
        catch (Exception ex)
        {
            SetError($"Class load failed: {ex.Message}");
            _log.Error("view", "Class load failed", ex);
        }
        finally
        {
            IsLoading = false;
        }
    }

    public async Task LoadFromObjectAsync(string address)
    {
        try
        {
            var obj = await _dump.GetObjectAsync(address);
            if (!string.IsNullOrEmpty(obj.ClassName))
                await LoadClassAsync(obj.ClassName);
        }
        catch (Exception ex)
        {
            _log.Error("view", $"Object detail load failed: {ex.Message}", ex);
        }
    }
}
