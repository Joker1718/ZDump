# Zolt-Dump

Real-time Godot Engine memory dumper and analyzer with Cheat Engine integration.

Named after **Zoltraak** (ゾルトラーク) — Frieren's signature attack magic that deconstructs its target.

## Features

- **Real-time object inspection** — Browse Godot's ObjectDB, ClassDB, and SceneTree
- **Live property values** — View and modify object properties at runtime
- **Node tree hierarchy** — Navigate the complete scene tree
- **Cheat Engine integration** — Copy CE XML, CE Field, Export CSX
- **AOBMaker integration** — Generate AA code, goto dump address
- **Proxy DLL injection** — version.dll proxy for seamless loading
- **Multi-version support** — Godot 4.0 ~ 4.7+

## Architecture

```
Game Process (Godot 4.x)          ZoltDumpUI (Avalonia)
+---------------------------+     +---------------------------+
| ZoltDumper.dll [injected] | <-> | NodeTreePanel             |
|   Frieren (OffsetFinder)  |     | LiveWalkerPanel           |
|   Ausetiese (ObjectDB)    |pipe | ClassStructPanel          |
|   DiAgolze (ClassDB)      |     | InstanceFinderPanel       |
|   Serie (SceneTree)       |     | CE XML/CSX Export         |
|   Ubel (PropertyWalker)   |     | AOBMaker Bridge           |
+---------------------------+     +---------------------------+
```

## Build

```powershell
# Build everything (DLL + UI)
.\build.ps1

# Build DLL only
.\build.ps1 -Target DLL

# Build UI only
.\build.ps1 -Target UI

# Distribution build
.\build.ps1 -Mode Publish -Clean
```

**Requirements**: Visual Studio 2022+ (C++ Desktop workload), .NET 10 SDK

## Naming Convention

All modules use names from the anime *Frieren: Beyond Journey's End*. See [docs/naming-convention.md](docs/naming-convention.md) for the full guide.

## License

TBD
