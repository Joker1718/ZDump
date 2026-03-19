# Quick test for Slay the Spire 2 version detection
param([int]$TimeoutSeconds = 25)

$logRoot    = "$env:LOCALAPPDATA\ZoltDump\Logs"
$proxyPath  = "D:\Github\Zolt-Dump\dist\proxy\winmm.dll"
$gameDir    = "H:\SteamLibrary\steamapps\common\Slay the Spire 2"
$gameExe    = "$gameDir\SlayTheSpire2.exe"
$logDir     = "$logRoot\SlayTheSpire2"

if (-not (Test-Path $proxyPath)) {
    Write-Host "ERROR: Proxy DLL not found" -ForegroundColor Red; exit 1
}
if (-not (Test-Path $gameExe)) {
    Write-Host "ERROR: Game not found at $gameExe" -ForegroundColor Red; exit 1
}

# Deploy proxy
$proxyDest = Join-Path $gameDir "winmm.dll"
Copy-Item $proxyPath $proxyDest -Force

# Clear old logs
if (Test-Path $logDir) { Remove-Item $logDir -Recurse -Force }

Write-Host "Launching Slay the Spire 2..." -ForegroundColor Cyan
$p = Start-Process -FilePath $gameExe -WorkingDirectory $gameDir -PassThru
Write-Host "  PID: $($p.Id)"

Start-Sleep -Seconds $TimeoutSeconds

Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Get-Process -Name "SlayTheSpire2" -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1

# Cleanup proxy
Remove-Item $proxyDest -Force -ErrorAction SilentlyContinue

# Read results
Write-Host "`n=== INIT LOG ===" -ForegroundColor Green
$initLog = "$logDir\init-0.log"
if (Test-Path $initLog) {
    $content = Get-Content $initLog -Raw
    # Show version-related lines
    $content -split "`n" | Where-Object { $_ -match "version|Version|SCAN:Ver|Phase 8|Godot|ClassDB heuristic" } | ForEach-Object {
        Write-Host $_
    }
    Write-Host ""
    # Show summary
    $content -split "`n" | Where-Object { $_ -match "SUMMARY" } | ForEach-Object {
        Write-Host $_
    }
} else {
    Write-Host "init-0.log not found!" -ForegroundColor Red
}

# Also check scan log for version details
$scanLog = "$logDir\scan-0.log"
if (Test-Path $scanLog) {
    Write-Host "`n=== VERSION SCAN DETAILS ===" -ForegroundColor Green
    Get-Content $scanLog | Where-Object { $_ -match "SCAN:Ver|version|Version|ClassDB heuristic" } | ForEach-Object {
        Write-Host $_
    }
}
