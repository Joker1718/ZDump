using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;

namespace ZoltDumpUI.Core;

/// <summary>
/// Eisen — 艾冉 (Iron Warrior)
/// Proxy DLL deployment service for Godot games.
/// </summary>
public interface IProxyDeployService
{
    Task<List<string>> GetSteamLibraryFoldersAsync();
    Task<List<GameDeployInfo>> FindGodotGamesAsync(List<string> libraryFolders);
    Task RefreshDeployStatusAsync(List<GameDeployInfo> games);
    Task DeployAsync(GameDeployInfo game, bool force = false);
    Task UndeployAsync(GameDeployInfo game);
    bool IsOurProxyDll(string path);
    string? GetDllVersion(string path);
}

/// <summary>Deployment info for a single Godot game.</summary>
[DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.PublicProperties)]
public sealed class GameDeployInfo : INotifyPropertyChanged
{
    public string GameName { get; set; } = "";
    public string GamePath { get; set; } = "";
    public string ExePath { get; set; } = "";
    public DeployStatus Status { get; set; }
    public string DeployedVersion { get; set; } = "";
    public string Error { get; set; } = "";

    private bool _isSelected;
    public bool IsSelected
    {
        get => _isSelected;
        set { if (_isSelected != value) { _isSelected = value; OnPropertyChanged(); } }
    }

    public event PropertyChangedEventHandler? PropertyChanged;
    private void OnPropertyChanged([CallerMemberName] string? name = null)
        => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}

public enum DeployStatus
{
    NotDeployed,
    DeployedCurrent,
    DeployedOutdated,
    OtherProxy,
    Error
}
