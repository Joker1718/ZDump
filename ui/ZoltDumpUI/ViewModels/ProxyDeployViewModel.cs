using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using ZoltDumpUI.Core;

namespace ZoltDumpUI.ViewModels;

/// <summary>
/// Eisen — 艾乍 (Iron Warrior)
/// Proxy DLL deployment management.
/// </summary>
public partial class ProxyDeployViewModel : ViewModelBase
{
    private readonly IProxyDeployService _deploy;
    private readonly ILoggingService _log;

    public ObservableCollection<GameDeployInfo> Games { get; } = [];

    [ObservableProperty] private bool _isScanning;
    [ObservableProperty] private string _sourceVersion = "";
    [ObservableProperty] private bool _force;

    public ProxyDeployViewModel(IProxyDeployService deploy, ILoggingService log)
    {
        _deploy = deploy;
        _log = log;

        var dllPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, Grimoire.ProxyDllSubdir, Grimoire.ProxyDllFileName);
        SourceVersion = _deploy.GetDllVersion(dllPath) ?? "N/A";
    }

    [RelayCommand]
    private async Task ScanGamesAsync()
    {
        try
        {
            ClearError();
            IsScanning = true;

            var folders = await _deploy.GetSteamLibraryFoldersAsync();
            var games = await _deploy.FindGodotGamesAsync(folders);
            await _deploy.RefreshDeployStatusAsync(games);

            Games.Clear();
            foreach (var g in games)
                Games.Add(g);
        }
        catch (Exception ex)
        {
            SetError($"Scan failed: {ex.Message}");
            _log.Error("Scan failed", ex);
        }
        finally
        {
            IsScanning = false;
        }
    }

    [RelayCommand]
    private async Task DeploySelectedAsync()
    {
        foreach (var game in Games.Where(g => g.IsSelected))
        {
            try
            {
                await _deploy.DeployAsync(game, Force);
            }
            catch (Exception ex)
            {
                game.Error = ex.Message;
            }
        }
        await RefreshStatusAsync();
    }

    [RelayCommand]
    private async Task UndeploySelectedAsync()
    {
        foreach (var game in Games.Where(g => g.IsSelected))
        {
            try
            {
                await _deploy.UndeployAsync(game);
            }
            catch (Exception ex)
            {
                game.Error = ex.Message;
            }
        }
        await RefreshStatusAsync();
    }

    [RelayCommand]
    private void SelectAll()
    {
        foreach (var g in Games) g.IsSelected = true;
    }

    [RelayCommand]
    private void UnselectAll()
    {
        foreach (var g in Games) g.IsSelected = false;
    }

    [RelayCommand]
    private void InvertSelection()
    {
        foreach (var g in Games) g.IsSelected = !g.IsSelected;
    }

    private async Task RefreshStatusAsync()
    {
        var list = Games.ToList();
        await _deploy.RefreshDeployStatusAsync(list);
        // Force UI refresh by replacing collection
        Games.Clear();
        foreach (var g in list)
            Games.Add(g);
    }
}
