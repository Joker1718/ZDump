<#
  Zolt-Dump - Real-time Godot Engine memory dumper.
  Copyright (C) 2026  bbfox0703

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
#>
# test_pipe.ps1 — Test Zolt-Dump pipe server (Fern) against multiple Godot versions & games
# Launches each target with winmm.dll proxy, connects to the named pipe,
# sends test commands, validates responses, and produces a compatibility matrix.
#
# Usage:
#   .\test_pipe.ps1                  # Test all targets
#   .\test_pipe.ps1 -Only editors    # Only Godot editor builds
#   .\test_pipe.ps1 -Only games      # Only real games
#   .\test_pipe.ps1 -TimeoutSeconds 30  # Increase wait time

param(
    [int]$TimeoutSeconds = 25,
    [int]$PipeConnectTimeout = 15,
    [ValidateSet("all","editors","games")]
    [string]$Only = "all"
)

$testDir    = "D:\Github\Zolt-Dump\test\godot"
$logRoot    = "$env:LOCALAPPDATA\ZoltDump\Logs"
$proxyPath  = "D:\Github\Zolt-Dump\dist\proxy\winmm.dll"
$pipeName   = "ZoltDumpBfx"

if (-not (Test-Path $proxyPath)) {
    Write-Host "ERROR: Proxy DLL not found at $proxyPath. Build first!" -ForegroundColor Red
    exit 1
}

# ================================================================
# Test targets
# ================================================================
$editors = @(
    @{ Name="Godot 4.0";   Exe="$testDir\Godot_v4.0-stable_win64.exe";   Args="--headless"; LogDir="Godot_v4.0-stable_win64";   ProxyDir=$testDir },
    @{ Name="Godot 4.1";   Exe="$testDir\Godot_v4.1-stable_win64.exe";   Args="--headless"; LogDir="Godot_v4.1-stable_win64";   ProxyDir=$testDir },
    @{ Name="Godot 4.2";   Exe="$testDir\Godot_v4.2-stable_win64.exe";   Args="--headless"; LogDir="Godot_v4.2-stable_win64";   ProxyDir=$testDir },
    @{ Name="Godot 4.3";   Exe="$testDir\Godot_v4.3-stable_win64.exe";   Args="--headless"; LogDir="Godot_v4.3-stable_win64";   ProxyDir=$testDir },
    @{ Name="Godot 4.4.1"; Exe="$testDir\Godot_v4.4.1-stable_win64.exe"; Args="--headless"; LogDir="Godot_v4.4.1-stable_win64"; ProxyDir=$testDir },
    @{ Name="Godot 4.5.1"; Exe="$testDir\Godot_v4.5.1-stable_win64.exe"; Args="--headless"; LogDir="Godot_v4.5.1-stable_win64"; ProxyDir=$testDir },
    @{ Name="Godot 4.6.1"; Exe="$testDir\Godot_v4.6.1-stable_win64.exe"; Args="--headless"; LogDir="Godot_v4.6.1-stable_win64"; ProxyDir=$testDir }
)

$games = @(
    @{ Name="Brotato (3.7dev)";          Exe="H:\SteamLibrary\steamapps\common\Brotato\Brotato.exe";                         Args=$null; LogDir="Brotato";                   ProxyDir="H:\SteamLibrary\steamapps\common\Brotato" },
    @{ Name="20 Small Mazes (3.5.3)";    Exe="H:\SteamLibrary\steamapps\common\20 Small Mazes\20SM.exe";                     Args=$null; LogDir="20SM";                      ProxyDir="H:\SteamLibrary\steamapps\common\20 Small Mazes" },
    @{ Name="Magic Archery (4.2.2)";     Exe="H:\SteamLibrary\steamapps\common\Magic Archery\Magic Archery.exe";             Args=$null; LogDir="Magic Archery";             ProxyDir="H:\SteamLibrary\steamapps\common\Magic Archery" },
    @{ Name="One Potion Please (4.3)";   Exe="H:\SteamLibrary\steamapps\common\One Potion Please\OnePotionPlease.exe";       Args=$null; LogDir="OnePotionPlease";           ProxyDir="H:\SteamLibrary\steamapps\common\One Potion Please" },
    # Virtual Cottage excluded: can't auto-close, ClassDB FAIL (special case)
    # @{ Name="Virtual Cottage";           Exe="H:\SteamLibrary\steamapps\common\Virtual Cottage\Virtual Cottage_WINDOWS.exe"; Args=$null; LogDir="Virtual Cottage_WINDOWS";   ProxyDir="H:\SteamLibrary\steamapps\common\Virtual Cottage" },
    @{ Name="Upload Labs";               Exe="H:\SteamLibrary\steamapps\common\Upload Labs\Upload Labs.exe";                 Args=$null; LogDir="Upload Labs";               ProxyDir="H:\SteamLibrary\steamapps\common\Upload Labs" },
    @{ Name="Holo X Break";              Exe="H:\SteamLibrary\steamapps\common\Holo X Break\Holo X Break.exe";               Args=$null; LogDir="Holo X Break";              ProxyDir="H:\SteamLibrary\steamapps\common\Holo X Break" },
    @{ Name="Dome Keeper";               Exe="H:\SteamLibrary\steamapps\common\Dome Keeper\domekeeper.exe";                  Args=$null; LogDir="domekeeper";                ProxyDir="H:\SteamLibrary\steamapps\common\Dome Keeper" }
)

$targets = @()
if ($Only -eq "all" -or $Only -eq "editors") { $targets += $editors }
if ($Only -eq "all" -or $Only -eq "games")   { $targets += $games }

# ================================================================
# Pipe client helper
# ================================================================
function Test-PipeCommands {
    param(
        [string]$PipeName,
        [int]$ConnectTimeoutSec
    )

    $result = [PSCustomObject]@{
        Connected    = $false
        InitOk       = $false
        GodotVer     = "-"
        PID          = "-"
        PointersOk   = $false
        ObjectDB     = "-"
        ClassDB      = "-"
        SceneTree    = "-"
        Engine       = "-"
        ObjCountOk   = $false
        ObjCount     = 0
        SlotMax      = 0
        ObjListOk    = $false
        ObjListCount = 0
        ClassCount   = 0
        OffsetsOk    = $false
        TreeOk       = $false
        TreeRoot     = "-"
        WalkOk       = $false
        WalkFields   = 0
        WalkValues   = 0
        WalkPtrcall  = 0
        Error        = ""
    }

    try {
        $pipe = New-Object System.IO.Pipes.NamedPipeClientStream(".", $PipeName, [System.IO.Pipes.PipeDirection]::InOut)

        # Poll for pipe availability
        $deadline = (Get-Date).AddSeconds($ConnectTimeoutSec)
        $connected = $false
        while ((Get-Date) -lt $deadline) {
            try {
                $pipe.Connect(1000)
                $connected = $true
                break
            } catch {
                Start-Sleep -Milliseconds 500
            }
        }

        if (-not $connected) {
            $result.Error = "Pipe connect timeout (${ConnectTimeoutSec}s)"
            return $result
        }

        $result.Connected = $true

        $writer = New-Object System.IO.StreamWriter($pipe)
        $reader = New-Object System.IO.StreamReader($pipe)
        $writer.AutoFlush = $true

        # Helper to send command and read response
        function Send-PipeCmd {
            param([string]$json)
            $writer.WriteLine($json)
            $line = $reader.ReadLine()
            if ($null -eq $line) { return $null }
            return $line | ConvertFrom-Json
        }

        # --- cmd: init ---
        $resp = Send-PipeCmd '{"id":1,"cmd":"init"}'
        if ($resp -and $resp.ok) {
            $result.InitOk   = $true
            $result.GodotVer = if ($resp.godot_version) { $resp.godot_version } else { "?" }
            $result.PID      = if ($resp.pid) { $resp.pid } else { "?" }
        }

        # --- cmd: get_pointers ---
        $resp = Send-PipeCmd '{"id":2,"cmd":"get_pointers"}'
        if ($resp -and $resp.ok) {
            $result.PointersOk = $true
            $result.ObjectDB   = if ($resp.object_db -and $resp.object_db -ne "0") { "OK" } else { "FAIL" }
            $result.ClassDB    = if ($resp.class_db -and $resp.class_db -ne "0") { "OK" } else { "FAIL" }
            $result.SceneTree  = if ($resp.scene_tree -and $resp.scene_tree -ne "0") { "OK" } else { "FAIL" }
            $result.Engine     = if ($resp.engine -and $resp.engine -ne "0") { "OK" } else { "FAIL" }
        }

        # --- cmd: get_object_count ---
        $resp = Send-PipeCmd '{"id":3,"cmd":"get_object_count"}'
        if ($resp -and $resp.ok) {
            $result.ObjCountOk = $true
            $result.ObjCount   = [int]$resp.count
            $result.SlotMax    = [int]$resp.slot_max
        }

        # --- cmd: get_offsets ---
        $resp = Send-PipeCmd '{"id":4,"cmd":"get_offsets"}'
        if ($resp -and $resp.ok) {
            $result.OffsetsOk = $true
        }

        # --- cmd: get_object_list (first 5 objects) ---
        $objListResp = Send-PipeCmd '{"id":5,"cmd":"get_object_list","offset":0,"limit":5}'
        if ($objListResp -and $objListResp.ok) {
            $result.ObjListOk    = $true
            $result.ObjListCount = if ($objListResp.objects) { $objListResp.objects.Count } else { 0 }
        }

        # --- cmd: walk_tree (depth 1) ---
        $treeResp = Send-PipeCmd '{"id":6,"cmd":"walk_tree","depth":1}'
        if ($treeResp -and $treeResp.ok -and $treeResp.root) {
            $result.TreeOk   = $true
            $result.TreeRoot = if ($treeResp.root.name) { $treeResp.root.name } else { "?" }
        }

        # --- cmd: find_instances (Node class) ---
        $findResp = Send-PipeCmd '{"id":7,"cmd":"find_instances","class_name":"Node","limit":3}'
        if ($findResp -and $findResp.ok) {
            $result.ClassCount = [int]$findResp.count
        }

        # --- cmd: walk_instance ---
        # Strategy: pick best target object for walk_instance
        #   1. Root node from walk_tree (known Node-derived, has properties)
        #   2. First Node from find_instances
        #   3. Fallback: first object from get_object_list
        $walkAddr = $null
        if ($result.TreeOk -and $treeResp.root.address) {
            $walkAddr = $treeResp.root.address
        } elseif ($findResp -and $findResp.ok -and $findResp.instances -and $findResp.instances.Count -gt 0) {
            $walkAddr = $findResp.instances[0].address
        } elseif ($result.ObjListOk -and $objListResp.objects -and $objListResp.objects.Count -gt 0) {
            $walkAddr = $objListResp.objects[0].address
        }

        if ($walkAddr) {
            $walkResp = Send-PipeCmd "{`"id`":8,`"cmd`":`"walk_instance`",`"address`":`"$walkAddr`"}"
            if ($walkResp -and $walkResp.ok -and $walkResp.fields) {
                $result.WalkOk      = $true
                $result.WalkFields  = $walkResp.fields.Count
                $result.WalkValues  = ($walkResp.fields | Where-Object { $_.value -ne "" }).Count
                $result.WalkPtrcall = ($walkResp.fields | Where-Object { $_.from_ptrcall -eq $true }).Count
            }
        }

        $writer.Close()
        $reader.Close()
        $pipe.Close()
    } catch {
        $result.Error = $_.Exception.Message
    }

    return $result
}

# ================================================================
# Main test loop
# ================================================================
$results = @()

foreach ($t in $targets) {
    $exe = $t.Exe
    if (-not (Test-Path $exe)) {
        Write-Host "SKIP: $($t.Name) (exe not found)" -ForegroundColor Yellow
        continue
    }

    # Deploy proxy
    $proxyDest = Join-Path $t.ProxyDir "winmm.dll"
    Copy-Item $proxyPath $proxyDest -Force

    # Clear previous logs
    $possibleLogDirs = Get-ChildItem $logRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -eq $t.LogDir -or $_.Name -like "$($t.LogDir)*" }
    foreach ($d in $possibleLogDirs) {
        Remove-Item $d.FullName -Recurse -Force
    }

    Write-Host "`n=== Testing: $($t.Name) ===" -ForegroundColor Cyan

    # Launch process
    $startParams = @{
        FilePath         = $exe
        WorkingDirectory = $t.ProxyDir
        PassThru         = $true
    }
    if ($t.Args) { $startParams.ArgumentList = $t.Args }
    $p = Start-Process @startParams
    Write-Host "  PID: $($p.Id)"

    # Test pipe commands
    Write-Host "  Connecting to pipe (timeout: ${PipeConnectTimeout}s)..."
    $pipeResult = Test-PipeCommands -PipeName $pipeName -ConnectTimeoutSec $PipeConnectTimeout

    # Wait a bit more for logs to flush
    Start-Sleep -Seconds 2

    # Kill process
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
    $exeName = [System.IO.Path]::GetFileNameWithoutExtension($exe)
    Get-Process -Name $exeName -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1

    # Remove proxy
    Remove-Item $proxyDest -Force -ErrorAction SilentlyContinue

    # Check pipe log exists
    $pipeLogExists = $false
    $logDir = $null
    $possibleLogDirs = Get-ChildItem $logRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -eq $t.LogDir -or $_.Name -like "$($t.LogDir)*" } |
        Sort-Object LastWriteTime -Descending
    if ($possibleLogDirs) {
        $logDir = $possibleLogDirs[0].FullName
        $pipeLog = Join-Path $logDir "pipe-0.log"
        $pipeLogExists = Test-Path $pipeLog
    }

    # Display results
    $connColor = if ($pipeResult.Connected) { "Green" } else { "Red" }
    Write-Host "  Pipe Connected: $($pipeResult.Connected)" -ForegroundColor $connColor

    if ($pipeResult.Connected) {
        $initColor = if ($pipeResult.InitOk) { "Green" } else { "Red" }
        Write-Host "  init:           $($pipeResult.InitOk) (version=$($pipeResult.GodotVer))" -ForegroundColor $initColor

        $ptrColor = if ($pipeResult.PointersOk) { "Green" } else { "Red" }
        Write-Host "  get_pointers:   ObjDB=$($pipeResult.ObjectDB) ClassDB=$($pipeResult.ClassDB) STree=$($pipeResult.SceneTree) Engine=$($pipeResult.Engine)" -ForegroundColor $ptrColor

        $countColor = if ($pipeResult.ObjCountOk -and $pipeResult.ObjCount -gt 0) { "Green" } else { "Yellow" }
        Write-Host "  get_obj_count:  $($pipeResult.ObjCount) objects, $($pipeResult.SlotMax) slots" -ForegroundColor $countColor

        $listColor = if ($pipeResult.ObjListOk -and $pipeResult.ObjListCount -gt 0) { "Green" } else { "Yellow" }
        Write-Host "  get_obj_list:   $($pipeResult.ObjListCount) returned" -ForegroundColor $listColor

        Write-Host "  get_offsets:    $($pipeResult.OffsetsOk)"

        $treeColor = if ($pipeResult.TreeOk) { "Green" } else { "Yellow" }
        Write-Host "  walk_tree:      root=$($pipeResult.TreeRoot)" -ForegroundColor $treeColor

        Write-Host "  find_instances: $($pipeResult.ClassCount) Node instances"

        $walkColor = if ($pipeResult.WalkOk) { "Green" } else { "Yellow" }
        Write-Host "  walk_instance:  $($pipeResult.WalkValues)/$($pipeResult.WalkFields) values ($($pipeResult.WalkPtrcall) via ptrcall)" -ForegroundColor $walkColor
    } elseif ($pipeResult.Error) {
        Write-Host "  Error: $($pipeResult.Error)" -ForegroundColor Red
    }

    Write-Host "  Pipe log:       $(if ($pipeLogExists) { 'Yes' } else { 'No' })"

    # Store result
    $results += [PSCustomObject]@{
        Name       = $t.Name
        Pipe       = if ($pipeResult.Connected) { "OK" } else { "FAIL" }
        Init       = if ($pipeResult.InitOk) { "OK" } else { "FAIL" }
        Pointers   = if ($pipeResult.PointersOk) { "OK" } else { "FAIL" }
        ObjDB      = $pipeResult.ObjectDB
        ClassDB    = $pipeResult.ClassDB
        STree      = $pipeResult.SceneTree
        ObjCount   = $pipeResult.ObjCount
        ObjList    = if ($pipeResult.ObjListOk) { "$($pipeResult.ObjListCount)" } else { "FAIL" }
        Tree       = if ($pipeResult.TreeOk) { $pipeResult.TreeRoot } else { "FAIL" }
        Walk       = if ($pipeResult.WalkOk) { "$($pipeResult.WalkValues)/$($pipeResult.WalkFields)" } else { "FAIL" }
        Ptrcall    = $pipeResult.WalkPtrcall
        GodotVer   = $pipeResult.GodotVer
        Error      = $pipeResult.Error
    }
}

# ================================================================
# Summary table
# ================================================================
Write-Host "`n`n========== PIPE SERVER COMPATIBILITY MATRIX ==========" -ForegroundColor Green
$fmt = "{0,-28} {1,-6} {2,-6} {3,-6} {4,-6} {5,-6} {6,-8} {7,-8} {8,-10} {9,-12} {10,-8} {11}"
Write-Host ($fmt -f "Target", "Pipe", "Init", "ObjDB", "ClsDB", "STree", "Objects", "ObjList", "Tree", "Walk(v/f)", "Ptrcall", "GodotVer")
Write-Host ("-" * 130)
foreach ($r in $results) {
    $allOk = ($r.Pipe -eq "OK" -and $r.Init -eq "OK" -and $r.ObjDB -eq "OK" -and $r.ClassDB -eq "OK")
    $color = if ($allOk) { "Green" } elseif ($r.Pipe -eq "FAIL") { "Red" } else { "Yellow" }
    Write-Host ($fmt -f $r.Name, $r.Pipe, $r.Init, $r.ObjDB, $r.ClassDB, $r.STree, $r.ObjCount, $r.ObjList, $r.Tree, $r.Walk, $r.Ptrcall, $r.GodotVer) -ForegroundColor $color
}

Write-Host "`nAll proxy DLLs have been cleaned up from game directories."
