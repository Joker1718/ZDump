<#
  Zolt-Dump - Real-time Godot Engine memory dumper.
  Copyright (C) 2026  bbfox0703

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
#>
# test_pipe.ps1
# Zolt-Dump — PowerShell Named Pipe Test Client
# Tests communication with ZoltDumper.dll pipe server
#
# Usage: .\test_pipe.ps1 [-Command <cmd>] [-Payload <json>]

param(
    [string]$Command = "init",
    [string]$Payload = "",
    [string]$PipeName = "ZoltDumpBfx",
    [int]$TimeoutMs = 5000
)

$ErrorActionPreference = "Stop"

function Send-PipeCommand {
    param(
        [string]$Cmd,
        [string]$PayloadJson,
        [string]$Pipe,
        [int]$Timeout
    )

    Write-Host "[Zolt-Dump Pipe Test]" -ForegroundColor Cyan
    Write-Host "  Pipe: \\.\pipe\$Pipe"
    Write-Host "  Command: $Cmd"

    # Build JSON request
    $id = Get-Random -Minimum 1 -Maximum 999999
    $request = @{ id = $id; cmd = $Cmd }

    if ($PayloadJson -ne "") {
        $extra = $PayloadJson | ConvertFrom-Json
        foreach ($prop in $extra.PSObject.Properties) {
            $request[$prop.Name] = $prop.Value
        }
    }

    $json = $request | ConvertTo-Json -Compress
    Write-Host "  Request: $json" -ForegroundColor DarkGray

    # Connect to pipe
    try {
        $pipeClient = New-Object System.IO.Pipes.NamedPipeClientStream(".", $Pipe, [System.IO.Pipes.PipeDirection]::InOut)
        $pipeClient.Connect($Timeout)

        $writer = New-Object System.IO.StreamWriter($pipeClient, [System.Text.Encoding]::UTF8)
        $writer.AutoFlush = $true
        $reader = New-Object System.IO.StreamReader($pipeClient, [System.Text.Encoding]::UTF8)

        # Send request
        $writer.WriteLine($json)

        # Read response
        $response = $reader.ReadLine()
        Write-Host ""
        Write-Host "  Response:" -ForegroundColor Green
        $response | ConvertFrom-Json | ConvertTo-Json -Depth 10 | Write-Host

        $reader.Close()
        $writer.Close()
        $pipeClient.Close()
    }
    catch {
        Write-Host "  ERROR: $($_.Exception.Message)" -ForegroundColor Red
        Write-Host "  Is ZoltDumper.dll injected and running?" -ForegroundColor Yellow
    }
}

# Preset commands
$presets = @{
    "init"          = ""
    "pointers"      = ""
    "count"         = ""
    "objects"        = '{"offset":0,"limit":10}'
    "tree"          = '{"depth":2}'
    "classes"       = '{"game_only":false}'
}

if ($Command -eq "help") {
    Write-Host "Available commands:" -ForegroundColor Cyan
    foreach ($key in $presets.Keys | Sort-Object) {
        $p = if ($presets[$key] -ne "") { " (payload: $($presets[$key]))" } else { "" }
        Write-Host "  $key$p"
    }
    Write-Host ""
    Write-Host "Custom usage:"
    Write-Host '  .\test_pipe.ps1 -Command "walk_instance" -Payload ''{"address":"1A2B3C4D"}'''
    exit
}

# Use preset payload if available
if ($Payload -eq "" -and $presets.ContainsKey($Command)) {
    $Payload = $presets[$Command]
}

# Map short names to pipe commands
$cmdMap = @{
    "pointers" = "get_pointers"
    "count"    = "get_object_count"
    "objects"  = "get_object_list"
    "tree"     = "walk_tree"
    "classes"  = "list_classes"
}

$actualCmd = if ($cmdMap.ContainsKey($Command)) { $cmdMap[$Command] } else { $Command }

Send-PipeCommand -Cmd $actualCmd -PayloadJson $Payload -Pipe $PipeName -Timeout $TimeoutMs
