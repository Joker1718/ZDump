# Zolt-Dump Phase 1 Integration Test
# Launches Godot editor with proxy DLL (version.dll), waits for scan, checks logs.
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File test\run_test.ps1

param(
    [int]$WaitSeconds = 8
)

$ErrorActionPreference = "Stop"
$godotDir  = "$PSScriptRoot\godot"
$godotExe  = "$godotDir\Godot_v4.4.1-stable_win64.exe"
$proxyDll  = "$godotDir\version.dll"
$logBase   = "$env:LOCALAPPDATA\ZoltDump\Logs"
$cacheBase = "$env:LOCALAPPDATA\ZoltDump"

Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  Zolt-Dump Phase 1 Integration Test" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan

# Verify files exist
if (-not (Test-Path $godotExe)) { Write-Host "ERROR: Godot exe not found: $godotExe" -ForegroundColor Red; exit 1 }
if (-not (Test-Path $proxyDll)) { Write-Host "ERROR: version.dll not found: $proxyDll" -ForegroundColor Red; exit 1 }

Write-Host "  Godot:  $godotExe"
Write-Host "  Proxy:  $proxyDll"
Write-Host ""

# Clean previous logs for this process
$procName = "Godot_v4.4.1-stable_win64"
$logDir = "$logBase\$procName"
if (Test-Path $logDir) {
    Remove-Item $logDir -Recurse -Force
    Write-Host "  Cleaned previous logs: $logDir" -ForegroundColor Yellow
}

# Launch Godot (it will load version.dll as proxy, which triggers DLL init)
Write-Host "  Launching Godot editor..." -ForegroundColor Green
$proc = Start-Process -FilePath $godotExe -PassThru -WindowStyle Minimized

Write-Host "  PID: $($proc.Id)"
Write-Host "  Waiting ${WaitSeconds}s for scan to complete..." -ForegroundColor Yellow

Start-Sleep -Seconds $WaitSeconds

# Kill Godot
if (-not $proc.HasExited) {
    $proc.Kill()
    Write-Host "  Godot terminated." -ForegroundColor Yellow
} else {
    Write-Host "  Godot already exited (code: $($proc.ExitCode))" -ForegroundColor Yellow
}

Start-Sleep -Seconds 1

Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  Results" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan

# Check log directory
$testPassed = $true

if (Test-Path $logDir) {
    Write-Host ""
    Write-Host "  Log directory: $logDir" -ForegroundColor Green
    $logFiles = Get-ChildItem $logDir -File
    Write-Host "  Log files: $($logFiles.Count)"
    foreach ($f in $logFiles) {
        $size = if ($f.Length -gt 1024) { "{0:N1} KB" -f ($f.Length / 1024) } else { "$($f.Length) B" }
        Write-Host "    $($f.Name)  ($size)"
    }

    # Read init log
    $initLog = "$logDir\init-0.log"
    if (Test-Path $initLog) {
        Write-Host ""
        Write-Host "  --- init-0.log ---" -ForegroundColor Cyan
        Get-Content $initLog | ForEach-Object { Write-Host "  $_" }
    }

    # Read scan log
    $scanLog = "$logDir\scan-0.log"
    if (Test-Path $scanLog) {
        Write-Host ""
        Write-Host "  --- scan-0.log ---" -ForegroundColor Cyan
        Get-Content $scanLog | ForEach-Object { Write-Host "  $_" }
    }

    # Read offsets log
    $offsLog = "$logDir\offsets-0.log"
    if (Test-Path $offsLog) {
        Write-Host ""
        Write-Host "  --- offsets-0.log ---" -ForegroundColor Cyan
        Get-Content $offsLog | ForEach-Object { Write-Host "  $_" }
    }
} else {
    Write-Host ""
    Write-Host "  ERROR: No log directory created at $logDir" -ForegroundColor Red
    $testPassed = $false
}

# Check hint cache
Write-Host ""
$cacheFiles = Get-ChildItem $cacheBase -Filter "ZoltDump.*.json" -ErrorAction SilentlyContinue
if ($cacheFiles) {
    foreach ($cf in $cacheFiles) {
        Write-Host "  Hint cache: $($cf.FullName)" -ForegroundColor Green
        $json = Get-Content $cf.FullName -Raw | ConvertFrom-Json
        if ($json.games) {
            $gameCount = ($json.games | Get-Member -MemberType NoteProperty).Count
            Write-Host "  Games cached: $gameCount"
        }
    }
} else {
    Write-Host "  WARNING: No hint cache file found" -ForegroundColor Yellow
}

# Summary
Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
if ($testPassed) {
    Write-Host "  TEST PASSED" -ForegroundColor Green
} else {
    Write-Host "  TEST FAILED" -ForegroundColor Red
}
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""
