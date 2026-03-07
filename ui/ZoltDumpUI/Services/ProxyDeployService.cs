using System.Diagnostics;
using ZoltDumpUI.Core;

namespace ZoltDumpUI.Services;

/// <summary>
/// Eisen — 艾乍 (Iron Warrior)
/// Proxy DLL deployment for Godot games via Steam detection.
/// </summary>
public sealed class ProxyDeployService : IProxyDeployService
{
    public async Task<List<string>> GetSteamLibraryFoldersAsync()
    {
        // TODO: Parse Steam's libraryfolders.vdf via registry + VdfParser
        var folders = new List<string>();
        await Task.CompletedTask;
        return folders;
    }

    public async Task<List<GameDeployInfo>> FindGodotGamesAsync(List<string> libraryFolders)
    {
        // TODO: Scan steamapps/common for Godot executables
        var games = new List<GameDeployInfo>();
        await Task.CompletedTask;
        return games;
    }

    public async Task RefreshDeployStatusAsync(List<GameDeployInfo> games)
    {
        // TODO: Check deploy status for each game
        await Task.CompletedTask;
    }

    public async Task DeployAsync(GameDeployInfo game, bool force = false)
    {
        // TODO: Copy proxy DLL to game directory
        await Task.CompletedTask;
    }

    public async Task UndeployAsync(GameDeployInfo game)
    {
        // TODO: Remove proxy DLL from game directory
        await Task.CompletedTask;
    }

    public bool IsOurProxyDll(string path)
    {
        // TODO: Check PE version info for ProductName
        if (!File.Exists(path)) return false;
        try
        {
            var vi = FileVersionInfo.GetVersionInfo(path);
            return vi.ProductName?.Contains("Zolt-Dump") == true;
        }
        catch { return false; }
    }

    public string? GetDllVersion(string path)
    {
        if (!File.Exists(path)) return null;
        try
        {
            var vi = FileVersionInfo.GetVersionInfo(path);
            return vi.FileVersion;
        }
        catch { return null; }
    }
}
