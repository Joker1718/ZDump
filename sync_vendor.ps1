$deps = @{
    "nlohmann"    = "https://github.com/nlohmann/json.git"
    "minihook"    = "https://github.com/TsudaKageyu/minhook.git"
}

if (!(Test-Path "vendor")) { New-Item -ItemType Directory -Path "vendor" }

foreach ($item in $deps.GetEnumerator()) {
    $path = "vendor\$($item.Key)"
    if (Test-Path "$path\.git") {
        Write-Host "[更新] $($item.Key)..." -ForegroundColor Cyan
        Push-Location $path
        git pull
        Pop-Location
    } else {
        Write-Host "[下載] $($item.Key)..." -ForegroundColor Green
        git clone $($item.Value) $path
    }
}
Write-Host "同步完成！" -ForegroundColor Yellow