# download_godot_editors.ps1 — Download Godot editor binaries for integration testing
# Downloads headless-capable Godot editor builds to test/godot/
#
# Usage:
#   .\download_godot_editors.ps1              # Download all configured versions
#   .\download_godot_editors.ps1 -Version 4.3 # Download a specific version only
#   .\download_godot_editors.ps1 -Force        # Re-download even if already present

param(
    [string]$Version = "",
    [switch]$Force
)

$destDir = Join-Path $PSScriptRoot "godot"

# Godot editor versions used by test_all_versions.ps1 / test_pipe.ps1
# Format: @{ Tag = GitHub release tag; Zip = zip filename; Exe = expected exe name }
$versions = @(
    @{ Tag = "4.0-stable";   Zip = "Godot_v4.0-stable_win64.exe.zip";   Exe = "Godot_v4.0-stable_win64.exe" },
    @{ Tag = "4.1-stable";   Zip = "Godot_v4.1-stable_win64.exe.zip";   Exe = "Godot_v4.1-stable_win64.exe" },
    @{ Tag = "4.2-stable";   Zip = "Godot_v4.2-stable_win64.exe.zip";   Exe = "Godot_v4.2-stable_win64.exe" },
    @{ Tag = "4.3-stable";   Zip = "Godot_v4.3-stable_win64.exe.zip";   Exe = "Godot_v4.3-stable_win64.exe" },
    @{ Tag = "4.4.1-stable"; Zip = "Godot_v4.4.1-stable_win64.exe.zip"; Exe = "Godot_v4.4.1-stable_win64.exe" }
)

# Filter to a specific version if requested
if ($Version) {
    $filtered = $versions | Where-Object { $_.Tag -like "*$Version*" }
    if (-not $filtered) {
        Write-Host "ERROR: No matching version for '$Version'" -ForegroundColor Red
        Write-Host "Available: $($versions | ForEach-Object { $_.Tag } | Join-String -Separator ', ')"
        exit 1
    }
    $versions = @($filtered)
}

# Create destination directory
if (-not (Test-Path $destDir)) {
    New-Item -ItemType Directory -Path $destDir -Force | Out-Null
    Write-Host "Created $destDir"
}

$baseUrl = "https://github.com/godotengine/godot/releases/download"
$downloaded = 0
$skipped = 0
$failed = 0

foreach ($v in $versions) {
    $exePath = Join-Path $destDir $v.Exe
    $zipPath = Join-Path $destDir $v.Zip

    if ((Test-Path $exePath) -and -not $Force) {
        Write-Host "SKIP: $($v.Exe) (already exists, use -Force to re-download)" -ForegroundColor DarkGray
        $skipped++
        continue
    }

    $url = "$baseUrl/$($v.Tag)/$($v.Zip)"
    Write-Host "Downloading $($v.Tag)..." -ForegroundColor Cyan -NoNewline

    try {
        # Download zip
        $ProgressPreference = 'SilentlyContinue'  # Speed up Invoke-WebRequest
        Invoke-WebRequest -Uri $url -OutFile $zipPath -UseBasicParsing

        # Extract — Godot zips contain the exe at root level
        Expand-Archive -Path $zipPath -DestinationPath $destDir -Force

        # Verify exe exists after extraction
        if (Test-Path $exePath) {
            Write-Host " OK ($([math]::Round((Get-Item $exePath).Length / 1MB, 1)) MB)" -ForegroundColor Green
            $downloaded++
        } else {
            # Some versions might nest in a folder — check and move
            $nested = Get-ChildItem $destDir -Recurse -Filter $v.Exe | Select-Object -First 1
            if ($nested) {
                Move-Item $nested.FullName $exePath -Force
                Write-Host " OK (moved from subfolder)" -ForegroundColor Green
                $downloaded++
            } else {
                Write-Host " WARN: zip extracted but exe not found" -ForegroundColor Yellow
                $failed++
            }
        }

        # Clean up zip
        Remove-Item $zipPath -Force -ErrorAction SilentlyContinue
    } catch {
        Write-Host " FAILED: $($_.Exception.Message)" -ForegroundColor Red
        Remove-Item $zipPath -Force -ErrorAction SilentlyContinue
        $failed++
    }
}

# Clean up any leftover console.exe files (Godot zips include them)
Get-ChildItem $destDir -Filter "*_console.exe" -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue

Write-Host "`n========================================" -ForegroundColor Green
Write-Host "Downloaded: $downloaded  Skipped: $skipped  Failed: $failed"
Write-Host "Location: $destDir"

if ($failed -gt 0) { exit 1 }
