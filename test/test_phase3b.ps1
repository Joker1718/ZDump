# test_phase3b.ps1 — Quick test for Phase 3b new commands
# Tests list_classes, search_properties, get_scan_status, trigger_scan, get_ce_pointer_info
# and validates walk_class field alignment (super_name, class_addr, properties)

param(
    [string]$GodotExe = "D:\Github\Zolt-Dump\test\godot\Godot_v4.3-stable_win64.exe"
)

$testDir   = Split-Path $GodotExe -Parent
$proxy     = "D:\Github\Zolt-Dump\build_proxy\dll\winmm.dll"
$pipeName  = "ZoltDumpBfx"

if (-not (Test-Path $proxy)) {
    Write-Host "ERROR: Proxy DLL not found. Build first!" -ForegroundColor Red
    exit 1
}
if (-not (Test-Path $GodotExe)) {
    Write-Host "ERROR: Godot exe not found at $GodotExe" -ForegroundColor Red
    exit 1
}

# Deploy proxy
Copy-Item $proxy (Join-Path $testDir "winmm.dll") -Force

# Launch
$p = Start-Process -FilePath $GodotExe -WorkingDirectory $testDir -ArgumentList "--headless" -PassThru
Write-Host "Launched PID: $($p.Id)" -ForegroundColor Cyan

# Connect
Start-Sleep -Seconds 3
$pipe = New-Object System.IO.Pipes.NamedPipeClientStream(".", $pipeName, [System.IO.Pipes.PipeDirection]::InOut)
try {
    $pipe.Connect(10000)
} catch {
    Write-Host "FAIL: Could not connect to pipe" -ForegroundColor Red
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $testDir "winmm.dll") -Force -ErrorAction SilentlyContinue
    exit 1
}

$writer = New-Object System.IO.StreamWriter($pipe)
$reader = New-Object System.IO.StreamReader($pipe)
$writer.AutoFlush = $true

function Send-Cmd {
    param([string]$json)
    $writer.WriteLine($json)
    $line = $reader.ReadLine()
    if ($null -eq $line) { return $null }
    return $line | ConvertFrom-Json
}

$passCount = 0
$failCount = 0

function Check {
    param([string]$name, [bool]$condition, [string]$detail = "")
    if ($condition) {
        Write-Host "  PASS: $name $detail" -ForegroundColor Green
        $script:passCount++
    } else {
        Write-Host "  FAIL: $name $detail" -ForegroundColor Red
        $script:failCount++
    }
}

Write-Host "`n=== Phase 3b Command Tests ===" -ForegroundColor Cyan

# 1. init (warm up)
$r = Send-Cmd '{"id":1,"cmd":"init"}'
Check "init" ($r -and $r.ok) "(version=$($r.godot_version))"

# Check init has process_name field
Check "init has process_name" ($r -and $r.PSObject.Properties['process_name']) "(process_name=$($r.process_name))"

# 2. list_classes
Write-Host "`n--- list_classes ---"
$r = Send-Cmd '{"id":10,"cmd":"list_classes","limit":5}'
Check "list_classes ok" ($r -and $r.ok)
Check "list_classes has classes array" ($r -and $r.classes -and $r.classes.Count -gt 0) "(count=$($r.classes.Count))"
if ($r -and $r.classes -and $r.classes.Count -gt 0) {
    $c = $r.classes[0]
    Check "class has class_name" ($c.PSObject.Properties['class_name'] -ne $null) "($($c.class_name))"
    Check "class has super_name" ($c.PSObject.Properties['super_name'] -ne $null)
}

# 3. search_properties
Write-Host "`n--- search_properties ---"
$r = Send-Cmd '{"id":11,"cmd":"search_properties","query":"position","limit":5}'
Check "search_properties ok" ($r -and $r.ok)
Check "search_properties has hits array" ($r -and $r.PSObject.Properties['hits'] -ne $null) "(count=$($r.count))"

# 4. get_scan_status
Write-Host "`n--- get_scan_status ---"
$r = Send-Cmd '{"id":12,"cmd":"get_scan_status"}'
Check "get_scan_status ok" ($r -and $r.ok)
Check "get_scan_status is_complete" ($r -and $r.is_complete -eq $true)

# 5. trigger_scan
Write-Host "`n--- trigger_scan ---"
$r = Send-Cmd '{"id":13,"cmd":"trigger_scan"}'
Check "trigger_scan ok" ($r -and $r.ok)

# 6. get_ce_pointer_info
Write-Host "`n--- get_ce_pointer_info ---"
$r = Send-Cmd '{"id":14,"cmd":"get_ce_pointer_info","address":"0x12345"}'
Check "get_ce_pointer_info ok" ($r -and $r.ok)

# 7. walk_class field alignment
Write-Host "`n--- walk_class (field alignment) ---"
$r = Send-Cmd '{"id":15,"cmd":"walk_class","class_name":"Node"}'
Check "walk_class ok" ($r -and $r.ok)
Check "walk_class has super_name (not parent_name)" ($r -and $r.PSObject.Properties['super_name'] -ne $null) "(super=$($r.super_name))"
Check "walk_class has class_addr (not native_ptr)" ($r -and $r.PSObject.Properties['class_addr'] -ne $null)
Check "walk_class has properties (not fields)" ($r -and $r.PSObject.Properties['properties'] -ne $null) "(count=$($r.properties.Count))"
if ($r -and $r.properties -and $r.properties.Count -gt 0) {
    $prop = $r.properties[0]
    Check "property type is string" ($prop.type -is [string]) "(type=$($prop.type))"
}

# 8. get_object_list field alignment
Write-Host "`n--- get_object_list (field alignment) ---"
$r = Send-Cmd '{"id":16,"cmd":"get_object_list","offset":0,"limit":2}'
Check "get_object_list ok" ($r -and $r.ok)
if ($r -and $r.objects -and $r.objects.Count -gt 0) {
    $obj = $r.objects[0]
    Check "object uses 'address' (not 'addr')" ($obj.PSObject.Properties['address'] -ne $null) "(addr=$($obj.address))"
    Check "object_id is number (not string)" ($obj.object_id -is [int64] -or $obj.object_id -is [int32] -or $obj.object_id -is [decimal]) "(id=$($obj.object_id))"
}

# 9. get_pointers field alignment (flat, no nested scan_stats)
Write-Host "`n--- get_pointers (field alignment) ---"
$r = Send-Cmd '{"id":17,"cmd":"get_pointers"}'
Check "get_pointers ok" ($r -and $r.ok)
Check "get_pointers has slot_max" ($r -and $r.PSObject.Properties['slot_max'] -ne $null) "(slot_max=$($r.slot_max))"
Check "get_pointers has object_count" ($r -and $r.PSObject.Properties['object_count'] -ne $null) "(object_count=$($r.object_count))"

# 10. read_mem uses 'data' (not 'hex')
Write-Host "`n--- read_mem (field alignment) ---"
$addrHex = "0x0"  # Will fail to read but should return data field
if ($r -and $r.object_db -and $r.object_db -ne "0") {
    $addrHex = $r.object_db
}
$r = Send-Cmd "{`"id`":18,`"cmd`":`"read_mem`",`"address`":`"$addrHex`",`"size`":16}"
if ($r -and $r.ok) {
    Check "read_mem uses 'data' (not 'hex')" ($r.PSObject.Properties['data'] -ne $null)
} else {
    Write-Host "  SKIP: read_mem (addr unreadable)" -ForegroundColor Yellow
}

# Cleanup
$writer.Close()
$reader.Close()
$pipe.Close()

Start-Sleep -Seconds 1
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
$exeName = [System.IO.Path]::GetFileNameWithoutExtension($GodotExe)
Get-Process -Name $exeName -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $testDir "winmm.dll") -Force -ErrorAction SilentlyContinue

# Summary
Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "  Phase 3b Test Results: $passCount PASS, $failCount FAIL" -ForegroundColor $(if ($failCount -eq 0) { "Green" } else { "Yellow" })
Write-Host "========================================" -ForegroundColor Cyan
