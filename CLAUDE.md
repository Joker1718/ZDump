# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> For detailed specs, implementation history, and debugging notes, see the **[docs/](docs/)** directory.

-----

## Build & Dev Commands

### Unified Build Script (PREFERRED — always use this)

The `build.ps1` script handles VS DevShell setup, CMake, dotnet, and test execution automatically. **Always use this for building** — bare `cmake` / `dotnet build` commands will fail without the VS DevShell environment.

```bash
# Build everything (DLL + UI + Tests) — Release
powershell -NoProfile -ExecutionPolicy Bypass -File "D:\Github\Zolt-Dump\build.ps1"

# Build DLL only
powershell -NoProfile -ExecutionPolicy Bypass -File "D:\Github\Zolt-Dump\build.ps1" -Target DLL

# Build UI only
powershell -NoProfile -ExecutionPolicy Bypass -File "D:\Github\Zolt-Dump\build.ps1" -Target UI

# Run tests only
powershell -NoProfile -ExecutionPolicy Bypass -File "D:\Github\Zolt-Dump\build.ps1" -Target Test

# Debug build
powershell -NoProfile -ExecutionPolicy Bypass -File "D:\Github\Zolt-Dump\build.ps1" -Mode Debug

# Clean + publish (distribution)
powershell -NoProfile -ExecutionPolicy Bypass -File "D:\Github\Zolt-Dump\build.ps1" -Mode Publish -Clean
```

> **Why not bare cmake/dotnet?** The C++ DLL requires MSVC x64 environment (include paths, linker). `build.ps1` loads this via `Enter-VsDevShell` automatically.

### Manual Commands (reference only — prefer build.ps1)

```bash
# C++ DLL (requires VS DevShell loaded first)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# C# UI
dotnet build "ui/ZoltDumpUI/ZoltDumpUI.csproj" -c Release
dotnet test "ui/ZoltDumpUI.Tests/ZoltDumpUI.Tests.csproj"
```

### Vendor Dependencies (NO git submodules)

```bash
# Clone/update all vendor dependencies
powershell -NoProfile -ExecutionPolicy Bypass -File "D:\Github\Zolt-Dump\tools\vendor-update.ps1"
```

-----

## Rules (MUST follow)

- **Language**: Code comments and UI strings in English
- **Frieren naming**: All module/namespace/class names use Frieren anime terms. Every Frieren-named entity MUST have a comment explaining its actual function (e.g. `// Zoltraak — 栔爾朵列 (攻撃魔法) — Core memory scanning`)
- **Single Instance**: UI app uses Mutex to ensure only one instance runs
- **Async everywhere**: All I/O, pipe operations, and alert actions must be async
- **Platform Abstraction**: Any system/OS-dependent call (P/Invoke, Registry, OS commands) MUST go through an interface in `Core` project. `Core` must NEVER contain direct platform-specific code
- **Log output**: All logs go to `%LOCALAPPDATA%\ZoltDump\Logs`, split by category into per-process subfolders. DLL: 5 files (init, scan, offsets, pipe, walk). UI: 3 files (init, pipe, view). 4-file rotation, 8MB max per file. Root `Logs/` has no loose files — only subfolders
- **Magic words management**: All magic strings kept in `Grimoire.h` (DLL) and `Grimoire.cs` (UI) with proper comments
- **UI Strings**: English only. All UI strings in `Resources/Strings/en.axaml`, referenced via `StaticResource` bindings
- **Godot offsets**: All Object/Node/ClassDB offsets must be dynamically verified via OffsetFinder (Frieren.cpp), never hardcoded
- **AOT compatible**: All C# code must be Native AOT / trimming compatible. No reflection-based APIs — use source generators instead (e.g. `[JsonSerializable]` context for `System.Text.Json`, `[ObservableProperty]` for MVVM)
- **Vendor deps**: Clone into `vendor/`, NEVER use git submodules. Use `tools/vendor-update.ps1` for updates

-----

## Project Goal

Develop a **C++ DLL + Cheat Engine integration** Godot Engine Dumper.
Target: Support Godot 4.x (4.0 ~ 4.7+), find global structures (ObjectDB / ClassDB / SceneTree), build complete object/node/property hierarchy, integrate with CE and AOBMaker.

-----

## Architecture Overview

```
+------------------------------------------------------+
|  Game Process (Godot 4.x)                            |
|                                                      |
|  CE Lua                                              |
|    zolt_dump.lua     <- DLL inject + pipe start      |
|    zolt_dissect.lua  <- CE Structure Dissect builder  |
|      |  loadLibrary / callFunction (C ABI exports)   |
|      v                                               |
|  ZoltDumper.dll  [injected]                          |
|      +-- Stark (ExportAPI)    <- C ABI for CE Lua    |
|      +-- Fern (PipeServer)    <- Named Pipe IPC      |
|      +-- Frieren (OffsetFinder) <- ObjectDB/ClassDB  |
|      +-- Ausetiese (ObjectDB) <- Sparse array enum   |
|      +-- Diagoldze (ClassDB)   <- Class registry      |
|      +-- Flamme (StringName)  <- String resolution   |
|      +-- Serie (SceneTree)    <- Node hierarchy      |
|      +-- Ubel (PropertyWalker)<- Variant values      |
|      +-- Zoltraak (Memory)    <- AOBScan + SEH R/W   |
|      +-- Denken (CLR Bridge)  <- .NET reflection     |
|      +-- Solitaer (Logger)    <- 5-category logging  |
+----------------------+-------------------------------+
                       | Named Pipe: \\.\pipe\ZoltDumpBfx
                       | JSON over pipe (newline-delimited)
+----------------------v-------------------------------+
|  ZoltDumpUI  (Avalonia + CommunityToolkit.MVVM)      |
|      +-- PipeClient           <- async pipe mgmt    |
|      +-- NodeTreePanel        <- Node hierarchy      |
|      +-- ClassStructPanel     <- Property Grid       |
|      +-- PointerPanel         <- ObjectDB/ClassDB    |
|      +-- LiveWalkerPanel      <- Instance drill-down |
|      +-- InstanceFinderPanel  <- Class instance srch |
|      +-- ProxyDeployPanel     <- Proxy DLL mgmt     |
|      +-- CeXmlExport          <- CE XML ptr chains   |
|      +-- CsxExport            <- CE Structure Dissect|
+------------------------------------------------------+
```

-----

## Frieren Naming Convention

| Category | Frieren Term | Meaning | Usage |
|----------|-------------|---------|-------|
| Core operations | **Zoltraak** (栔爾朵列) | Attack magic | Memory scanning, AOB, SEH reads |
| Resource management | **Ausetiese** (服從之秤) | Aura's scales | Object enumeration, DB scanning |
| Constants & statics | **Diagoldze** (黃金化) | Mahat's gold | ClassDB, immutable registry |
| Obfuscation & security | **Solitaer** (索莉緹爾) | Mana concealment | Logging, trace hiding |
| Documentation & libs | **Grimoire** (魔導書) | Grimoire | Constants, magic strings |
| Error handling | **Mimic** (寶箱怪) | Chest mimic | Error types, trap results |
| String resolution | **Flamme** (芙拉梅) | Great mage master | StringName resolver |
| Scene tree | **Serie** (賽莉耶) | Living history mage | Node hierarchy walker |
| Property reader | **Ubel** (優貝爾) | Assassin specialist | Variant/property extraction |
| Offset finder | **Frieren** (芙莉蓮) | Protagonist | AOB sigs, version detection |
| Export API | **Stark** (修塔爾克) | Brave warrior | C ABI, reliable interface |
| Pipe server | **Fern** (費倫) | Apprentice | IPC, communication |
| Hint cache | **Himmel** (欣梅爾) | Hero | Remembered patterns |
| Game thread | **Heiter** (海塔) | Priest | Thread dispatch, context |
| Proxy DLL | **Eisen** (艾乍) | Iron warrior | Armored forwarding |
| CLR bridge | **Denken** (頓肯) | Analytical mage | .NET reflection via ZoltReflector |

-----

## Documentation Index

| Document | Contents |
|----------|----------|
| [docs/architecture.md](docs/architecture.md) | Directory structure, vendor deps, build environment |
| [docs/dll-spec.md](docs/dll-spec.md) | C++ DLL interface, offset tables, CE Lua exports |
| [docs/pipe-protocol.md](docs/pipe-protocol.md) | Named Pipe JSON IPC protocol (15 commands) |
| [docs/export-formats.md](docs/export-formats.md) | CE XML, CSX, SDK Header export rules |
| [docs/godot-internals.md](docs/godot-internals.md) | Godot memory layout: ObjectDB, ClassDB, Variant, Node |
| [docs/ue-to-godot-mapping.md](docs/ue-to-godot-mapping.md) | UE5CEDumper → Zolt-Dump feature mapping reference |
| [docs/naming-convention.md](docs/naming-convention.md) | Frieren naming guide with examples |
| [docs/dev-log.md](docs/dev-log.md) | Implementation status, challenges, next steps |
| [docs/technical-notes.md](docs/technical-notes.md) | Godot version differences, offset detection |
