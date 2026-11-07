<#
  Zolt-Dump - Real-time Godot Engine memory dumper.
  Copyright (C) 2026  bbfox0703

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
#>
<#
.SYNOPSIS
    Clone or update vendor dependencies for Zolt-Dump.
    Uses git clone (NOT submodules) per project requirements.

.DESCRIPTION
    Manages external dependencies in the vendor/ directory:
      - minhook: Inline hooking library (MIT)
      - nlohmann/json: Header-only C++ JSON library
      - godot-headers: Extracted reference headers from Godot Engine source

.EXAMPLE
    .\tools\vendor-update.ps1
#>

$ErrorActionPreference = "Stop"
$ROOT_DIR = Split-Path $PSScriptRoot -Parent
$VENDOR_DIR = Join-Path $ROOT_DIR "vendor"

# Ensure vendor directory exists
if (-not (Test-Path $VENDOR_DIR)) {
    New-Item -ItemType Directory -Path $VENDOR_DIR -Force | Out-Null
}

function Update-VendorRepo([string]$Name, [string]$Url, [string]$Branch = "master") {
    $repoDir = Join-Path $VENDOR_DIR $Name

    if (Test-Path $repoDir) {
        Write-Host "Updating $Name..." -ForegroundColor Yellow
        Push-Location $repoDir
        try {
            $env:GIT_REDIRECT_STDERR = '2>&1'
            git pull --ff-only origin $Branch 2>&1 | ForEach-Object { "$_" } | Write-Host
            if ($LASTEXITCODE -ne 0) {
                Write-Host "  [WARN] Pull failed, trying fetch + reset..." -ForegroundColor Yellow
                git fetch origin $Branch 2>&1 | ForEach-Object { "$_" } | Write-Host
                git reset --hard "origin/$Branch" 2>&1 | ForEach-Object { "$_" } | Write-Host
            }
            Write-Host "  [OK] $Name updated" -ForegroundColor Green
        }
        finally {
            Pop-Location
        }
    }
    else {
        Write-Host "Cloning $Name..." -ForegroundColor Yellow
        $env:GIT_REDIRECT_STDERR = '2>&1'
        git clone --depth 1 --branch $Branch $Url $repoDir 2>&1 | ForEach-Object { "$_" } | Write-Host
        if ($LASTEXITCODE -ne 0) {
            Write-Host "  [FAIL] Clone failed for $Name" -ForegroundColor Red
            return
        }
        Write-Host "  [OK] $Name cloned" -ForegroundColor Green
    }
}

function Update-GodotHeaders() {
    $godotSrc = "D:\Github\godot"
    $headersDir = Join-Path $VENDOR_DIR "godot-headers"

    if (-not (Test-Path $godotSrc)) {
        Write-Host "[WARN] Godot source not found at $godotSrc — skipping header extraction" -ForegroundColor Yellow
        return
    }

    Write-Host "Extracting Godot headers..." -ForegroundColor Yellow

    if (-not (Test-Path $headersDir)) {
        New-Item -ItemType Directory -Path $headersDir -Force | Out-Null
    }

    # Extract key header files for reference
    $headerFiles = @(
        "core/object/object.h",
        "core/object/object_id.h",
        "core/object/class_db.h",
        "core/object/ref_counted.h",
        "core/object/method_bind.h",
        "core/variant/variant.h",
        "core/string/string_name.h",
        "core/string/ustring.h",
        "core/config/engine.h",
        "scene/main/scene_tree.h",
        "scene/main/node.h",
        "scene/main/window.h",
        "core/extension/gdextension.h",
        "core/templates/hash_map.h",
        "core/templates/a_hash_map.h",
        "core/os/spin_lock.h"
    )

    foreach ($hdr in $headerFiles) {
        $src = Join-Path $godotSrc $hdr
        $dst = Join-Path $headersDir $hdr

        if (Test-Path $src) {
            $dstDir = Split-Path $dst -Parent
            if (-not (Test-Path $dstDir)) {
                New-Item -ItemType Directory -Path $dstDir -Force | Out-Null
            }
            Copy-Item $src $dst -Force
        }
        else {
            Write-Host "  [WARN] Not found: $hdr" -ForegroundColor Yellow
        }
    }

    Write-Host "  [OK] Godot headers extracted" -ForegroundColor Green
}

# ============================================================
# Clone/update vendor repos
# ============================================================

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  Zolt-Dump Vendor Dependency Update" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""

Update-VendorRepo -Name "minhook" -Url "https://github.com/TsudaKageyu/minhook.git" -Branch "master"
Update-VendorRepo -Name "nlohmann" -Url "https://github.com/nlohmann/json.git" -Branch "develop"
Update-GodotHeaders

Write-Host ""
Write-Host "  [DONE] All vendor dependencies updated" -ForegroundColor Green
Write-Host ""
