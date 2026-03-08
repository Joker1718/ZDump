# test_all_versions.ps1 — Test Zolt-Dump scanner against multiple Godot versions
# Uses winmm.dll proxy placed in the test/godot/ directory

param(
    [int]$TimeoutSeconds = 15
)

$testDir = "D:\Github\Zolt-Dump\test\godot"
$logRoot = "$env:LOCALAPPDATA\ZoltDump\Logs"
$proxySource = "D:\Github\Zolt-Dump\build\dll\winmm.dll"

# Copy proxy DLL to test directory
Copy-Item $proxySource (Join-Path $testDir "winmm.dll") -Force

# Godot versions to test (in order)
$versions = @(
    "Godot_v4.0-stable_win64",
    "Godot_v4.1-stable_win64",
    "Godot_v4.2-stable_win64",
    "Godot_v4.3-stable_win64",
    "Godot_v4.4.1-stable_win64"
)

$results = @()

foreach ($ver in $versions) {
    $exePath = Join-Path $testDir "$ver.exe"
    if (-not (Test-Path $exePath)) {
        Write-Host "SKIP: $ver (exe not found)" -ForegroundColor Yellow
        continue
    }

    # Clear previous logs for this version
    $logDir = Join-Path $logRoot $ver
    if (Test-Path $logDir) {
        Remove-Item $logDir -Recurse -Force
    }

    Write-Host "`n=== Testing $ver ===" -ForegroundColor Cyan

    # Launch with --headless (no window, stays alive)
    $p = Start-Process -FilePath $exePath -ArgumentList "--headless" -WorkingDirectory $testDir -PassThru
    Write-Host "  PID: $($p.Id)"

    # Wait for scan to complete
    Start-Sleep -Seconds $TimeoutSeconds

    # Kill the process
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1

    # Check results
    $initLog = Join-Path $logDir "init-0.log"
    $scanLog = Join-Path $logDir "scan-0.log"

    $result = [PSCustomObject]@{
        Version   = $ver
        Engine    = "?"
        ObjectDB  = "?"
        ClassDB   = "?"
        SceneTree = "?"
        GodotVer  = "?"
    }

    if (Test-Path $initLog) {
        $initContent = Get-Content $initLog -Raw

        # Parse results from init log
        if ($initContent -match "Engine:\s+(\w+) at ([0-9A-F]+)") {
            $result.Engine = if ($Matches[2] -ne "0000000000000000") { "OK ($($Matches[1]))" } else { "FAIL" }
        }
        if ($initContent -match "ObjectDB:\s+(\w+) at ([0-9A-F]+)") {
            $result.ObjectDB = if ($Matches[2] -ne "0000000000000000") { "OK ($($Matches[1]))" } else { "FAIL" }
        }
        if ($initContent -match "ClassDB:\s+(\w+) at ([0-9A-F]+)") {
            $result.ClassDB = if ($Matches[2] -ne "0000000000000000") { "OK ($($Matches[1]))" } else { "FAIL" }
        }
        if ($initContent -match "SceneTree:\s+(\w+) at ([0-9A-F]+)") {
            $result.SceneTree = if ($Matches[2] -ne "0000000000000000") { "OK ($($Matches[1]))" } else { "FAIL" }
        }
        if ($initContent -match "Godot version: (.+?) \(") {
            $result.GodotVer = $Matches[1]
        }

        Write-Host "  Engine:    $($result.Engine)"
        Write-Host "  ObjectDB:  $($result.ObjectDB)"
        Write-Host "  ClassDB:   $($result.ClassDB)"
        Write-Host "  SceneTree: $($result.SceneTree)"
        Write-Host "  Version:   $($result.GodotVer)"
    } else {
        Write-Host "  NO LOGS FOUND" -ForegroundColor Red
    }

    $results += $result
}

# Summary table
Write-Host "`n`n========== COMPATIBILITY MATRIX ==========" -ForegroundColor Green
Write-Host ("{0,-35} {1,-20} {2,-20} {3,-20} {4,-20} {5}" -f "Version", "Engine", "ObjectDB", "ClassDB", "SceneTree", "GodotVer")
Write-Host ("-" * 130)
foreach ($r in $results) {
    $color = if ($r.Engine -like "OK*" -and $r.ObjectDB -like "OK*" -and $r.ClassDB -like "OK*" -and $r.SceneTree -like "OK*") { "Green" } elseif ($r.Engine -eq "?") { "Red" } else { "Yellow" }
    Write-Host ("{0,-35} {1,-20} {2,-20} {3,-20} {4,-20} {5}" -f $r.Version, $r.Engine, $r.ObjectDB, $r.ClassDB, $r.SceneTree, $r.GodotVer) -ForegroundColor $color
}

# Clean up proxy
Remove-Item (Join-Path $testDir "winmm.dll") -Force -ErrorAction SilentlyContinue
Write-Host "`nProxy DLL cleaned up from test directory."
