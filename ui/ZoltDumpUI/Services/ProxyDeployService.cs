using System.Diagnostics;
using Microsoft.Win32;
using ZoltDumpUI.Core;

namespace ZoltDumpUI.Services;

/// <summary>
/// Eisen — 艾乍 (Iron Warrior)
/// Proxy DLL deployment for Godot games via Steam detection.
/// Detects Steam libraries, finds Godot games (.pck files), and
/// manages winmm.dll proxy deployment/undeployment.
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
    /// Falls back to checking for embedded PCK inside the exe (GDPC magic marker).
    /// </summary>
    private static void ScanGameFolder(string gameDir, List<GameDeployInfo> games)
    {
        string gameName = Path.GetFileName(gameDir);

        try
        {
            // Look for .pck files — Godot's data container
            var pckFiles = Directory.EnumerateFiles(gameDir, "*" + Grimoire.GodotPckExtension)
                                    .ToList();

            if (pckFiles.Count > 0)
            {
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
                    return; // One entry per game folder
                }
            }

            // Fallback: Some Godot games embed .pck inside the exe.
            // Check each exe for the GDPC magic marker near end of file.
            var exeCandidates = Directory.EnumerateFiles(gameDir, "*.exe").ToList();
            foreach (string exe in exeCandidates)
            {
                if (HasEmbeddedPck(exe))
                {
                    games.Add(new GameDeployInfo
                    {
                        GameName = gameName,
                        GamePath = gameDir,
                        ExePath = exe,
                    });
                    return;
                }
            }
        }
        catch
        {
            // Skip inaccessible folders
        }
    }

    /// <summary>
    /// Checks if an executable has an embedded Godot PCK by looking for
    /// the "GDPC" magic bytes near the end of the file.
    /// Godot appends: [pck data...][GDPC marker][offset (8 bytes)] at EOF.
    /// </summary>
    private static bool HasEmbeddedPck(string exePath)
    {
        try
        {
            using var fs = new FileStream(exePath, FileMode.Open, FileAccess.Read, FileShare.Read);
            if (fs.Length < 16) return false;

            // Godot writes a 12-byte footer: "GDPC" (4B) + data_offset (8B) at EOF
            // But the PCK data itself also starts with "GDPC" magic.
            // Scan last 256 bytes for "GDPC" as a quick heuristic.
            long scanStart = Math.Max(0, fs.Length - 256);
            int scanLen = (int)(fs.Length - scanStart);
            fs.Seek(scanStart, SeekOrigin.Begin);

            byte[] tail = new byte[scanLen];
            int bytesRead = fs.Read(tail, 0, scanLen);

            // Search for "GDPC" (0x47 0x44 0x50 0x43)
            for (int i = 0; i <= bytesRead - 4; i++)
            {
                if (tail[i] == 0x47 && tail[i + 1] == 0x44 &&
                    tail[i + 2] == 0x50 && tail[i + 3] == 0x43)
                    return true;
            }
        }
        catch
        {
            // File locked or unreadable
        }
        return false;
    }

    // ────────────────────────────────────────────────────────────────
    // Deploy Status
    // ────────────────────────────────────────────────────────────────

    public async Task RefreshDeployStatusAsync(List<GameDeployInfo> games)
    {
        string sourceDll = Path.Combine(
            Grimoire.AppDir, Grimoire.ProxyDllSubdir, Grimoire.ProxyDllFileName);
        string? sourceVersion = GetDllVersion(sourceDll);

        await Task.Run(() =>
        {
            foreach (var game in games)
            {
                string targetDll = Path.Combine(game.GamePath, Grimoire.ProxyDllFileName);
                game.Error = "";

                if (!File.Exists(targetDll))
                {
                    // Check for legacy version.dll — show as outdated so user redeploys
                    string legacyDll = Path.Combine(game.GamePath, Grimoire.LegacyProxyDllFileName);
                    if (File.Exists(legacyDll) && IsOurProxyDll(legacyDll))
                    {
                        game.Status = DeployStatus.DeployedOutdated;
                        game.DeployedVersion = GetDllVersion(legacyDll) ?? "";
                        game.Error = "Legacy version.dll — redeploy to migrate";
                        continue;
                    }

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

                if (sourceVersion == null)
                {
                    // Source DLL not found — can't compare, assume current
                    game.Status = DeployStatus.DeployedCurrent;
                }
                else if (installed == sourceVersion)
                {
                    game.Status = DeployStatus.DeployedCurrent;
                }
                else
                {
                    game.Status = DeployStatus.DeployedOutdated;
                }
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
                Grimoire.AppDir, Grimoire.ProxyDllSubdir, Grimoire.ProxyDllFileName);

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

            // Read source version before copy — FileVersionInfo may cache stale data
            // from the overwritten target file on Windows.
            string? srcVersion = GetDllVersion(sourceDll);

            File.Copy(sourceDll, targetDll, overwrite: true);
            game.Status = DeployStatus.DeployedCurrent;
            game.DeployedVersion = srcVersion ?? "";
            game.Error = "";

            // Deploy ZoltReflector.dll for .NET Godot games
            DeployReflector(game.GamePath);

            // Clean up legacy version.dll if it's ours (prevent mixed proxy state)
            CleanupLegacyProxy(game.GamePath);
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

            // Remove ZoltReflector.dll if present
            UndeployReflector(game.GamePath);

            // Also clean up legacy version.dll if it's ours
            CleanupLegacyProxy(game.GamePath);

            game.Status = DeployStatus.NotDeployed;
            game.DeployedVersion = "";
            game.Error = "";
        });
    }

    /// <summary>
    /// Remove legacy version.dll proxy if it belongs to us.
    /// Prevents mixed proxy state when migrating from version.dll to winmm.dll.
    /// </summary>
    private void CleanupLegacyProxy(string gameDir)
    {
        string legacyDll = Path.Combine(gameDir, Grimoire.LegacyProxyDllFileName);
        try
        {
            if (File.Exists(legacyDll) && IsOurProxyDll(legacyDll))
                File.Delete(legacyDll);
        }
        catch
        {
            // Best-effort cleanup — file may be locked
        }
    }

    // ────────────────────────────────────────────────────────────────
    // ZoltReflector — .NET reflection bridge for Godot + .NET games
    // ────────────────────────────────────────────────────────────────

    /// <summary>
    /// Deploy ZoltReflector.dll to a game directory if the game uses .NET
    /// (detected by GodotSharp.dll or coreclr.dll presence).
    /// Always overwrites — the DLL is small and versioning is tied to the proxy.
    /// </summary>
    private static void DeployReflector(string gameDir)
    {
        if (!IsDotNetGame(gameDir)) return;

        string sourceReflector = Path.Combine(
            Grimoire.AppDir, Grimoire.ProxyDllSubdir, Grimoire.ReflectorDllFileName);
        if (!File.Exists(sourceReflector)) return;

        string targetReflector = Path.Combine(gameDir, Grimoire.ReflectorDllFileName);
        try
        {
            File.Copy(sourceReflector, targetReflector, overwrite: true);
        }
        catch
        {
            // Best-effort — file may be locked by running game
        }
    }

    /// <summary>
    /// Remove ZoltReflector.dll from a game directory during undeploy.
    /// </summary>
    private static void UndeployReflector(string gameDir)
    {
        string reflector = Path.Combine(gameDir, Grimoire.ReflectorDllFileName);
        try
        {
            if (File.Exists(reflector))
                File.Delete(reflector);
        }
        catch
        {
            // Best-effort — file may be locked
        }
    }

    /// <summary>
    /// Detect if a game uses .NET by looking for GodotSharp or coreclr indicators.
    /// Checks exe directory and data_*/ subdirectories.
    /// </summary>
    private static bool IsDotNetGame(string gameDir)
    {
        // Check for GodotSharp.dll in game dir or subdirs
        if (File.Exists(Path.Combine(gameDir, "GodotSharp.dll")))
            return true;

        // Check data_*/ subdirectories (Godot export layout)
        try
        {
            foreach (string subDir in Directory.EnumerateDirectories(gameDir, "data_*"))
            {
                if (File.Exists(Path.Combine(subDir, "GodotSharp.dll")))
                    return true;
                // Also check for .NET runtime indicators
                if (File.Exists(Path.Combine(subDir, "coreclr.dll")))
                    return true;
            }
        }
        catch { }

        // Check for .runtimeconfig.json with GodotSharp reference
        try
        {
            foreach (string cfg in Directory.EnumerateFiles(gameDir, "*.runtimeconfig.json"))
            {
                string content = File.ReadAllText(cfg);
                if (content.Contains("GodotSharp", StringComparison.OrdinalIgnoreCase))
                    return true;
            }
        }
        catch { }

        return false;
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
