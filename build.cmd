@echo off
REM Zolt-Dump build wrapper - forwards to build.ps1

setlocal enabledelayedexpansion

REM No arguments — default Release build
if "%~1"=="" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
    goto :eof
)

REM Map shorthand positional arguments to PowerShell named parameters.
set "FIRST=%~1"

REM Help
if /i "%FIRST%"=="/?"      goto :usage
if /i "%FIRST%"=="-h"      goto :usage
if /i "%FIRST%"=="--help"  goto :usage
if /i "%FIRST%"=="help"    goto :usage

REM Shorthands
if /i "%FIRST%"=="debug"    ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Mode Debug   & goto :eof )
if /i "%FIRST%"=="publish"  ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Mode Publish & goto :eof )
if /i "%FIRST%"=="release"  ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Mode Release & goto :eof )
if /i "%FIRST%"=="dll"      ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Target DLL   & goto :eof )
if /i "%FIRST%"=="ui"       ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Target UI    & goto :eof )
if /i "%FIRST%"=="test"     ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Target Test  & goto :eof )
if /i "%FIRST%"=="proxydll" ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Target ProxyDLL & goto :eof )
if /i "%FIRST%"=="clean"    ( powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Clean        & goto :eof )

REM If first arg starts with '-', pass through as PowerShell named params
set "CHECK=%FIRST:~0,1%"
if "%CHECK%"=="-" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
    goto :eof
)

REM Unknown positional argument
echo [ERROR] Unknown argument: %FIRST%
echo.
goto :usage

:usage
echo Usage: build.cmd [command] [options]
echo.
echo Commands (shorthand):
echo   (none)       Release build, all targets
echo   debug        Debug build, all targets
echo   release      Release build, all targets
echo   publish      Optimized distribution build (AOT)
echo   dll          Build C++ DLL only
echo   ui           Build C# UI only
echo   test         Run tests only
echo   proxydll     Build proxy DLL only
echo   clean        Clean + Release build
echo   help         Show this help
echo.
echo PowerShell-style (advanced):
echo   build.cmd -Mode Publish -Clean
echo   build.cmd -Target DLL -Mode Debug
echo   build.cmd -Target ProxyDLL -ProxyTarget winmm
echo.
echo First time after clone? Run:
echo   .\tools\vendor-update.ps1
exit /b 1

:eof
