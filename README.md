# Zolt-Dump

Real-time Godot Engine memory dumper and analyzer with Cheat Engine integration.

Named after **Zoltraak** (ゾルトラーク) — Frieren's signature attack magic that deconstructs its target.

## Features

- **Real-time object inspection** — Browse Godot's ObjectDB, ClassDB, and SceneTree
- **Live property values** — Read object properties at runtime via direct memory access and ptrcall
- **Node tree hierarchy** — Navigate the complete scene tree with drilldown
- **Cheat Engine integration** — Copy CE XML, CE Field, Export CSX
- **AOBMaker integration** — Generate AA code, goto dump address
- **Proxy DLL injection** — winmm.dll / version.dll proxy for seamless loading
- **Multi-version support** — Godot 3.x (3.5+) and Godot 4.x (4.0 ~ 4.7+)

## Compatibility

| Godot Version | ObjectDB | ClassDB | SceneTree | Property Walk |
|---------------|----------|---------|-----------|---------------|
| 4.0 (GCC)     | OK       | OK      | OK        | 68/96 values  |
| 4.1 (GCC)     | OK       | OK      | OK        | 72/101 values |
| 4.2 (MSVC)    | OK       | OK      | OK        | 86/104 values |
| 4.3 (MSVC)    | OK       | OK      | OK        | 92/111 values |
| 4.4.x         | OK       | OK      | OK        | OK            |
| 4.5.x         | OK       | OK      | OK        | OK            |
| 4.6.x         | OK       | OK (AHashMap) | OK   | OK            |
| 3.5+ (GCC)    | OK       | OK      | OK        | 41/52 values  |

Tested with official editors (4.0-4.6.1) and real games including Brotato (3.7dev), Magic Archery (4.2.2), One Potion Please (4.3), Upload Labs (4.5.1), Holo X Break (4.2).

## Architecture

```
Game Process (Godot 3.x / 4.x)      ZoltDumpUI (Avalonia)
+-------------------------------+    +---------------------------+
| ZoltDumper.dll [injected]     |    | NodeTreePanel             |
|   Frieren  (OffsetFinder)     |    | LiveWalkerPanel           |
|   Ausetiese (ObjectDB)        |    | ClassStructPanel          |
|   Diagoldze (ClassDB)         |pipe| InstanceFinderPanel       |
|   Serie    (SceneTree)        |<-->| PointerPanel              |
|   Ubel     (PropertyWalker)   |    | ProxyDeployPanel          |
|   Flamme   (StringName)       |    | CE XML/CSX Export         |
|   Heiter   (GameThread)       |    | AOBMaker Bridge           |
+-------------------------------+    +---------------------------+
```

## Build

```powershell
# Build everything (DLL + UI)
.\build.ps1

# Build DLL only
.\build.ps1 -Target DLL

# Build proxy DLL (winmm.dll for game injection)
.\build.ps1 -Target ProxyDLL

# Build UI only
.\build.ps1 -Target UI

# Run tests
.\build.ps1 -Target Test

# Distribution build
.\build.ps1 -Mode Publish -Clean
```

**Requirements**: Visual Studio 2022+ (C++ Desktop workload), .NET 10 SDK

## Naming Convention

All modules use names from the anime *Frieren: Beyond Journey's End*. See [docs/naming-convention.md](docs/naming-convention.md) for the full guide.

## Documentation

| Document | Contents |
|----------|----------|
| [docs/architecture.md](docs/architecture.md) | Directory structure, vendor deps, build environment |
| [docs/dll-spec.md](docs/dll-spec.md) | C++ DLL interface, offset tables, CE Lua exports |
| [docs/pipe-protocol.md](docs/pipe-protocol.md) | Named Pipe JSON IPC protocol |
| [docs/godot-internals.md](docs/godot-internals.md) | Godot memory layout: ObjectDB, ClassDB, Variant, Node |
| [docs/naming-convention.md](docs/naming-convention.md) | Frieren naming guide with examples |

## License

TBD
