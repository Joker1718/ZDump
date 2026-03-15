# Architecture — Zolt-Dump

## Directory Structure

```
Zolt-Dump/
├── build.ps1 / build.cmd       # Unified build (MSVC + dotnet)
├── build_number.txt             # Shared 4th version digit
├── CMakeLists.txt               # Root CMake → dll/
├── CLAUDE.md                    # Dev rules & quick reference
│
├── dll/                         # C++ injected DLL (ZoltDumper.dll)
│   ├── CMakeLists.txt           # C++23, MSVC, Ninja, static CRT
│   └── src/                     # 17 source modules (Frieren-named)
│       ├── Zoltraak.cpp/.h      # Memory: AOBScan, SEH reads
│       ├── Solitaer.cpp/.h      # Logger: 5-category, rotation
│       ├── Ausetiese.cpp/.h     # ObjectDB: slot enumeration
│       ├── Diagoldze.cpp/.h      # ClassDB: class registry
│       ├── Flamme.cpp/.h        # StringName: interned strings
│       ├── Serie.cpp/.h         # SceneTree: node hierarchy
│       ├── Ubel.cpp/.h          # Variant: property values
│       ├── Frieren.cpp/.h       # OffsetFinder: AOB sigs
│       ├── Stark.cpp/.h         # ExportAPI: C ABI (~25 fns)
│       ├── Fern.cpp/.h          # PipeServer: JSON IPC
│       ├── Himmel.cpp/.h        # HintCache: per-game patterns
│       ├── Heiter.cpp/.h        # GameThread: MinHook dispatch
│       ├── Qual.cpp/.h          # Mailbox: CE Lua bridge
│       ├── Eisen.cpp/.h         # ProxyVersion: version.dll
│       ├── Macht.cpp/.h         # CEPlugin: Type 5 plugin
│       ├── Grimoire.h           # Constants & DynOff namespace
│       └── dllmain.cpp          # Entry point, auto-start
│
├── ui/                          # C# Avalonia UI (ZoltDumpUI)
│   ├── ZoltDumpUI.sln
│   ├── ZoltDumpUI/              # Main project (.NET 10)
│   │   ├── Models/              # JSON response types
│   │   ├── Services/            # Business logic (16+ services)
│   │   ├── ViewModels/          # MVVM (CommunityToolkit)
│   │   ├── Views/               # AXAML panels (9 views)
│   │   ├── Core/                # Platform abstraction interfaces
│   │   └── Resources/Strings/   # UI strings (en.axaml)
│   └── ZoltDumpUI.Tests/        # xUnit test project
│
├── scripts/                     # Cheat Engine Lua + .CT files
├── docs/                        # This documentation folder
├── vendor/                      # Cloned dependencies (NO submodules)
│   ├── minhook/                 # Inline hooking (MIT)
│   ├── nlohmann/                # JSON for Modern C++ (header-only)
│   └── godot-headers/           # Extracted Godot reference headers
└── tools/
    └── vendor-update.ps1        # Clone/update vendor repos
```

## Build Environment

| Component | Technology | Version |
|-----------|-----------|---------|
| C++ Compiler | MSVC (Visual Studio 2022+) | C++23 (/std:c++latest) |
| C++ Build | CMake + Ninja | CMake 3.25+ |
| C++ CRT | Static (/MT Release, /MTd Debug) | - |
| C++ Flags | /utf-8 /W4 /permissive- /EHa /arch:AVX2 | - |
| C# Framework | .NET 10.0-windows | win-x64 |
| UI Framework | Avalonia | 11.3.12 |
| MVVM | CommunityToolkit.MVVM | 8.x |
| Logging | Serilog | 4.x |
| Tests | xUnit | v3 |

## Component Communication

```
+------------------+     Named Pipe      +------------------+
| ZoltDumper.dll   | <--- JSON/newline -->| ZoltDumpUI.exe   |
| (in-process DLL) |  \\.\pipe\ZoltDumpBfx | (standalone app) |
+------------------+                      +------------------+
        ^                                         |
        |  C ABI exports                          | AOBMaker pipe
        v                                         v
+------------------+                      +------------------+
| CE Lua scripts   |                      | AOBMaker         |
| (zolt_dump.lua)  |                      | (\\.\pipe\AOBMaker)|
+------------------+                      +------------------+
```

## Vendor Dependencies

All cloned (not submoduled) via `tools/vendor-update.ps1`:

| Dependency | Source | Purpose |
|-----------|--------|---------|
| minhook | TsudaKageworker/minhook | Inline hooking for GameThread dispatch |
| nlohmann/json | nlohmann/json | Header-only C++ JSON (pipe protocol) |
| godot-headers | Extracted from D:\Github\godot | Reference struct definitions |

## Version Management

- `build_number.txt`: Single integer, auto-incremented by `build.ps1`
- DLL version: `1.0.0.{build}` via CMakeLists.txt + version.rc
- UI version: `1.0.0.{build}` via ZoltDumpUI.csproj MSBuild properties
- Git commit hash embedded in DLL via `BuildInfo.h.in` → `BuildInfo.h`

## Reference Projects

| Project | Location | Role |
|---------|----------|------|
| UE5CEDumper | D:\Github\UE5CEDumper | Architecture template (direct port) |
| AOBMaker | D:\Github\AOBMaker | AA code generation, CE integration |
| Godot Engine | D:\Github\godot | Source reference for memory layouts |
