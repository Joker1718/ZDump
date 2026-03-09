using System.Diagnostics;
using Microsoft.Win32;
using ZoltDumpUI.Core;

namespace ZoltDumpUI.Services;

/// <summary>
/// Eisen — 艾乍 (Iron Warrior)
/// Proxy DLL deployment for Godot games via Steam detection.
/// Detects Steam libraries, finds Godot games (.pck files), and
/// manages version.dll proxy deployment/undeployment.
/// </summary>
public sealed class ProxyDeployService : IProxyDeployService
{
    // ────────────────────────────────────────────────────────────────
    // Steam Library Detection
    // ────────────────────────────────────────────────────────────────

    public async Task<List<string>> GetSteamLibraryFoldersAsync()
    {
        return await Task.Run(() =>
        {
            var result = new List<string>();

            string? steamPath = GetSteamInstallPath();
            if (steamPath == null)
                return result;

            string vdfPath = Path.Combine(steamPath, Grimoire.SteamLibraryFoldersVdf);
            if (!File.Exists(vdfPath))
            {
                // Fallback: use Steam path itself as the single library
                result.Add(steamPath);
                return result;
            }

            string vdfContent = File.ReadAllText(vdfPath);
            var paths = VdfParser.ExtractLibraryPaths(vdfContent);

            if (paths.Count == 0)
            {
                result.Add(steamPath);
            }
            else
            {
                foreach (string p in paths)
                {
                    if (Directory.Exists(p))
                        result.Add(p);
                }
            }

            return result;
        });
    }

    private static string? GetSteamInstallPath()
    {
        try
        {
            using var key = Registry.LocalMachine.OpenSubKey(Grimoire.SteamRegistryPath);
            if (key?.GetValue(Grimoire.SteamRegistryKey) is string path && Directory.Exists(path))
                return path;
        }
        catch
        {
            // Registry access may fail
        }

        // Fallback to default Steam path
        if (Directory.Exists(Grimoire.SteamDefaultPath))
            return Grimoire.SteamDefaultPath;

        return null;
    }

    // ────────────────────────────────────────────────────────────────
    // Godot Game Detection
    // ────────────────────────────────────────────────────────────────

    public async Task<List<GameDeployInfo>> FindGodotGamesAsync(List<string> libraryFolders)
    {
        return await Task.Run(() =>
        {
            var games = new List<GameDeployInfo>();
            var seenDirs = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

            foreach (string libPath in libraryFolders)
            {
                string commonDir = Path.Combine(libPath, Grimoire.SteamAppsCommon);
                if (!Directory.Exists(commonDir))
                    continue;

                try
                {
                    foreach (string gameDir in Directory.EnumerateDirectories(commonDir))
                    {
                        if (!seenDirs.Add(gameDir))
                            continue;

                        ScanGameFolder(gameDir, games);
                    }
                }
                catch
                {
                    // Permission error — skip this library
                }
            }

            return games;
        });
    }

    /// <summary>
    /// Detects Godot games by looking for .pck files next to executables.
    /// Godot exported games have a {GameName}.pck alongside {GameName}.exe.
    /// </summary>
    private static void ScanGameFolder(string gameDir, List<GameDeployInfo> games)
    {
        string gameName = Path.GetFileName(gameDir);

        try
        {
            // Look for .pck files — Godot's data container
            var pckFiles = Directory.EnumerateFiles(gameDir, "*" + Grimoire.GodotPckExtension)
                                    .ToList();

            if (pckFiles.Count == 0)
                return; // Not a Godot game

            // Find matching executable for each .pck
            foreach (string pck in pckFiles)
            {
                string baseName = Path.GetFileNameWithoutExtension(pck);
                string exePath = Path.Combine(gameDir, baseName + ".exe");

                if (!File.Exists(exePath))
                {
                    // Some games use different exe names — try any .exe in the folder
                    var exeFiles = Directory.EnumerateFiles(gameDir, "*.exe").ToList();
                    if (exeFiles.Count == 1)
                        exePath = exeFiles[0];
                    else
                        continue;
                }

                games.Add(new GameDeployInfo
                {
                    GameName = gameName,
                    GamePath = gameDir,
                    ExePath = exePath,
                });
                break; // One entry per game folder
            }
        }
        catch
        {
            // Skip inaccessible folders
        }
    }

    // ────────────────────────────────────────────────────────────────
    // Deploy Status
    // ────────────────────────────────────────────────────────────────

    public async Task RefreshDeployStatusAsync(List<GameDeployInfo> games)
    {
        string sourceDll = Path.Combine(
            AppDomain.CurrentDomain.BaseDirectory, Grimoire.ProxyDllFileName);
        string? sourceVersion = GetDllVersion(sourceDll);

        await Task.Run(() =>
        {
            foreach (var game in games)
            {
                string targetDll = Path.Combine(game.GamePath, Grimoire.ProxyDllFileName);
                game.Error = "";

                if (!File.Exists(targetDll))
                {
                    game.Status = DeployStatus.NotDeployed;
                    game.DeployedVersion = "";
                    continue;
                }

                if (!IsOurProxyDll(targetDll))
                {
                    game.Status = DeployStatus.OtherProxy;
                    game.DeployedVersion = "";
                    try
                    {
                        var info = FileVersionInfo.GetVersionInfo(targetDll);
                        game.Error = $"Other: {info.ProductName ?? info.FileDescription ?? "unknown"}";
                    }
                    catch
                    {
                        game.Error = "Other proxy DLL detected";
                    }
                    continue;
                }

                // It's our DLL — check version
                string? installed = GetDllVersion(targetDll);
                game.DeployedVersion = installed ?? "";

                if (sourceVersion != null && installed == sourceVersion)
                    game.Status = DeployStatus.DeployedCurrent;
                else
                    game.Status = DeployStatus.DeployedOutdated;
            }
        });
    }

    // ────────────────────────────────────────────────────────────────
    // Deploy / Undeploy
    // ────────────────────────────────────────────────────────────────

    public async Task DeployAsync(GameDeployInfo game, bool force = false)
    {
        await Task.Run(() =>
        {
            string sourceDll = Path.Combine(
                AppDomain.CurrentDomain.BaseDirectory, Grimoire.ProxyDllFileName);

            if (!File.Exists(sourceDll))
                throw new FileNotFoundException($"Proxy DLL not found: {sourceDll}");

            string targetDll = Path.Combine(game.GamePath, Grimoire.ProxyDllFileName);

            // Refuse to overwrite another program's proxy DLL
            if (File.Exists(targetDll) && !IsOurProxyDll(targetDll) && !force)
            {
                game.Status = DeployStatus.OtherProxy;
                game.Error = "Refused: another program's proxy DLL";
                return;
            }

            // Skip if same version (unless force)
            if (!force && File.Exists(targetDll) && IsOurProxyDll(targetDll))
            {
                string? srcVer = GetDllVersion(sourceDll);
                string? tgtVer = GetDllVersion(targetDll);
                if (srcVer != null && srcVer == tgtVer)
                {
                    game.Status = DeployStatus.DeployedCurrent;
                    return;
                }
            }

            File.Copy(sourceDll, targetDll, overwrite: true);
            game.Status = DeployStatus.DeployedCurrent;
            game.DeployedVersion = GetDllVersion(targetDll) ?? "";
            game.Error = "";
        });
    }

    public async Task UndeployAsync(GameDeployInfo game)
    {
        await Task.Run(() =>
        {
            string targetDll = Path.Combine(game.GamePath, Grimoire.ProxyDllFileName);

            if (!File.Exists(targetDll))
            {
                game.Status = DeployStatus.NotDeployed;
                game.DeployedVersion = "";
                return;
            }

            // Refuse to delete another program's proxy DLL
            if (!IsOurProxyDll(targetDll))
            {
                game.Status = DeployStatus.OtherProxy;
                game.Error = "Refused: not our proxy DLL";
                return;
            }

            File.Delete(targetDll);
            game.Status = DeployStatus.NotDeployed;
            game.DeployedVersion = "";
            game.Error = "";
        });
    }

    // ────────────────────────────────────────────────────────────────
    // DLL Identification
    // ────────────────────────────────────────────────────────────────

    public bool IsOurProxyDll(string path)
    {
        if (!File.Exists(path)) return false;
        try
        {
            var vi = FileVersionInfo.GetVersionInfo(path);
            return vi.ProductName?.Contains(Grimoire.ProxyProductName) == true;
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
