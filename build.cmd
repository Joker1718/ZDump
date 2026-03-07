@echo off
REM Zolt-Dump build wrapper - forwards to build.ps1
REM Usage: build.cmd [args...]
REM   build.cmd                     = Release build, all targets
REM   build.cmd debug               = Debug build
REM   build.cmd publish             = Optimized distribution build
REM   build.cmd -Target DLL         = Build C++ DLL only
REM   build.cmd -Target UI          = Build C# UI only
REM   build.cmd -Target Test        = Run tests only

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
