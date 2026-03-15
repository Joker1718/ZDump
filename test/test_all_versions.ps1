# test_all_versions.ps1 — Test Zolt-Dump scanner against multiple Godot versions & games
# Uses winmm.dll proxy for injection.
#
# Usage:
#   .\test_all_versions.ps1                  # Test all targets
#   .\test_all_versions.ps1 -Only editors    # Only Godot editor builds
#   .\test_all_versions.ps1 -Only games      # Only real games

param(
    [int]$TimeoutSeconds = 20,
    [ValidateSet("all","editors","games")]
    [string]$Only = "all"
)

$testDir    = "D:\Github\Zolt-Dump\test\godot"
$logRoot    = "$env:LOCALAPPDATA\ZoltDump\Logs"
$proxyPath  = "D:\Github\Zolt-Dump\build_proxy\dll\winmm.dll"

if (-not (Test-Path $proxyPath)) {
    Write-Host "ERROR: Proxy DLL not found at $proxyPath. Build first!" -ForegroundColor Red
    exit 1
}

# ================================================================
# Test targets: Godot editor headless builds + real games
# ================================================================

$editors = @(
    @{ Name="Godot 4.0";   Exe="$testDir\Godot_v4.0-stable_win64.exe";   Args="--headless"; LogDir="Godot_v4.0-stable_win64";   ProxyDir=$testDir },
    @{ Name="Godot 4.1";   Exe="$testDir\Godot_v4.1-stable_win64.exe";   Args="--headless"; LogDir="Godot_v4.1-stable_win64";   ProxyDir=$testDir },
    @{ Name="Godot 4.2";   Exe="$testDir\Godot_v4.2-stable_win64.exe";   Args="--headless"; LogDir="Godot_v4.2-stable_win64";   ProxyDir=$testDir },
    @{ Name="Godot 4.3";   Exe="$testDir\Godot_v4.3-stable_win64.exe";   Args="--headless"; LogDir="Godot_v4.3-stable_win64";   ProxyDir=$testDir },
    @{ Name="Godot 4.4.1"; Exe="$testDir\Godot_v4.4.1-stable_win64.exe"; Args="--headless"; LogDir="Godot_v4.4.1-stable_win64"; ProxyDir=$testDir }
)

$games = @(
    @{ Name="Brotato (3.7dev)";          Exe="H:\SteamLibrary\steamapps\common\Brotato\Brotato.exe";                         Args=$null; LogDir="Brotato";                   ProxyDir="H:\SteamLibrary\steamapps\common\Brotato" },
    @{ Name="20 Small Mazes (3.5.3)";    Exe="H:\SteamLibrary\steamapps\common\20 Small Mazes\20SM.exe";                     Args=$null; LogDir="20SM";                      ProxyDir="H:\SteamLibrary\steamapps\common\20 Small Mazes" },
    @{ Name="Magic Archery (4.2.2)";     Exe="H:\SteamLibrary\steamapps\common\Magic Archery\Magic Archery.exe";             Args=$null; LogDir="Magic Archery";             ProxyDir="H:\SteamLibrary\steamapps\common\Magic Archery" },
    @{ Name="One Potion Please (4.3)";   Exe="H:\SteamLibrary\steamapps\common\One Potion Please\OnePotionPlease.exe";       Args=$null; LogDir="OnePotionPlease";           ProxyDir="H:\SteamLibrary\steamapps\common\One Potion Please" },
    @{ Name="Virtual Cottage";           Exe="H:\SteamLibrary\steamapps\common\Virtual Cottage\Virtual Cottage_WINDOWS.exe"; Args=$null; LogDir="Virtual Cottage_WINDOWS";   ProxyDir="H:\SteamLibrary\steamapps\common\Virtual Cottage" },
    @{ Name="Upload Labs";               Exe="H:\SteamLibrary\steamapps\common\Upload Labs\Upload Labs.exe";                 Args=$null; LogDir="Upload Labs";               ProxyDir="H:\SteamLibrary\steamapps\common\Upload Labs" },
    @{ Name="Holo X Break";              Exe="H:\SteamLibrary\steamapps\common\Holo X Break\Holo X Break.exe";               Args=$null; LogDir="Holo X Break";              ProxyDir="H:\SteamLibrary\steamapps\common\Holo X Break" },
    @{ Name="Dome Keeper";               Exe="H:\SteamLibrary\steamapps\common\Dome Keeper\domekeeper.exe";                  Args=$null; LogDir="domekeeper";                ProxyDir="H:\SteamLibrary\steamapps\common\Dome Keeper" }
)

# Select which targets to test
$targets = @()
if ($Only -eq "all" -or $Only -eq "editors") { $targets += $editors }
if ($Only -eq "all" -or $Only -eq "games")   { $targets += $games }

$results = @()

foreach ($t in $targets) {
    $exe = $t.Exe
    if (-not (Test-Path $exe)) {
        Write-Host "SKIP: $($t.Name) (exe not found: $exe)" -ForegroundColor Yellow
        continue
    }

    # Deploy proxy to the target's directory
    $proxyDest = Join-Path $t.ProxyDir "winmm.dll"
    Copy-Item $proxyPath $proxyDest -Force

    # Clear previous logs
    # Log dir name might not match exactly — search for it
    $possibleLogDirs = Get-ChildItem $logRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -eq $t.LogDir -or $_.Name -like "$($t.LogDir)*" }
    foreach ($d in $possibleLogDirs) {
        Remove-Item $d.FullName -Recurse -Force
    }

    Write-Host "`n=== Testing $($t.Name) ===" -ForegroundColor Cyan

    # Launch
    $startParams = @{
        FilePath         = $exe
        WorkingDirectory = $t.ProxyDir
        PassThru         = $true
    }
    if ($t.Args) {
        $startParams.ArgumentList = $t.Args
    }
    $p = Start-Process @startParams
    Write-Host "  PID: $($p.Id)"

    # Wait for scan
    Start-Sleep -Seconds $TimeoutSeconds

    # Kill
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1

    # Also kill any child processes with same name
    $exeName = [System.IO.Path]::GetFileNameWithoutExtension($exe)
    Get-Process -Name $exeName -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1

    # Remove proxy from game directory (clean up immediately)
    Remove-Item $proxyDest -Force -ErrorAction SilentlyContinue

    # Find the log directory (might have different capitalization or suffix)
    $logDir = $null
    $possibleLogDirs = Get-ChildItem $logRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -eq $t.LogDir -or $_.Name -like "$($t.LogDir)*" } |
        Sort-Object LastWriteTime -Descending
    if ($possibleLogDirs) { $logDir = $possibleLogDirs[0].FullName }

    # Parse results
    $result = [PSCustomObject]@{
        Name      = $t.Name
        Engine    = "-"
        ObjectDB  = "-"
        ClassDB   = "-"
        SceneTree = "-"
        GodotVer  = "-"
    }

    if ($logDir) {
        $initLog = Join-Path $logDir "init-0.log"
        if (Test-Path $initLog) {
            $content = Get-Content $initLog -Raw

            if ($content -match "Engine:\s+(\w+) at ([0-9A-F]+)") {
                $result.Engine = if ($Matches[2] -ne "0000000000000000") { "OK ($($Matches[1]))" } else { "FAIL" }
            }
            if ($content -match "ObjectDB:\s+(\w+) at ([0-9A-F]+)") {
                $result.ObjectDB = if ($Matches[2] -ne "0000000000000000") { "OK ($($Matches[1]))" } else { "FAIL" }
            }
            if ($content -match "ClassDB:\s+(\w+) at ([0-9A-F]+)") {
                $result.ClassDB = if ($Matches[2] -ne "0000000000000000") { "OK ($($Matches[1]))" } else { "FAIL" }
            }
            if ($content -match "SceneTree:\s+(\w+) at ([0-9A-F]+)") {
                $result.SceneTree = if ($Matches[2] -ne "0000000000000000") { "OK ($($Matches[1]))" } else { "FAIL" }
            }
            if ($content -match "Godot version: (.+?) \(") {
                $result.GodotVer = $Matches[1]
            }
        } else {
            Write-Host "  init-0.log not found in $logDir" -ForegroundColor Red
        }
    } else {
        Write-Host "  NO LOG DIRECTORY FOUND" -ForegroundColor Red
    }

    Write-Host "  Engine:    $($result.Engine)"
    Write-Host "  ObjectDB:  $($result.ObjectDB)"
    Write-Host "  ClassDB:   $($result.ClassDB)"
    Write-Host "  SceneTree: $($result.SceneTree)"
    Write-Host "  Version:   $($result.GodotVer)"

    $results += $result
}

# ================================================================
# Summary table
# ================================================================
Write-Host "`n`n========== COMPATIBILITY MATRIX ==========" -ForegroundColor Green
$fmt = "{0,-30} {1,-18} {2,-18} {3,-18} {4,-18} {5}"
Write-Host ($fmt -f "Target", "Engine", "ObjectDB", "ClassDB", "SceneTree", "GodotVer")
Write-Host ("-" * 120)
foreach ($r in $results) {
    $ok = @($r.Engine, $r.ObjectDB, $r.ClassDB, $r.SceneTree) | Where-Object { $_ -like "OK*" }
    $fail = @($r.Engine, $r.ObjectDB, $r.ClassDB, $r.SceneTree) | Where-Object { $_ -eq "FAIL" }
    $color = if ($ok.Count -eq 4) { "Green" } elseif ($fail.Count -eq 4 -or $r.Engine -eq "-") { "Red" } else { "Yellow" }
    Write-Host ($fmt -f $r.Name, $r.Engine, $r.ObjectDB, $r.ClassDB, $r.SceneTree, $r.GodotVer) -ForegroundColor $color
}

Write-Host "`nAll proxy DLLs have been cleaned up from game directories."
