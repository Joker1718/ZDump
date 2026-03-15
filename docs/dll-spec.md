# DLL Specification — ZoltDumper.dll

## Overview

ZoltDumper.dll is a C++ DLL injected into Godot 4.x game processes. It discovers Godot's internal data structures via AOB scanning, provides object enumeration and property reading, and exposes results via Named Pipe IPC and C ABI exports.

---

## Module Architecture

```
ZoltDumper.dll
  ├── Zoltraak (Memory)        ← AOBScan, SEH reads, module info
  ├── Solitaer (Logger)        ← 5-category file logging
  ├── Frieren (OffsetFinder)   ← AOB sigs, version detection, DynOff
  ├── Himmel (HintCache)       ← Per-game pattern caching
  ├── Ausetiese (ObjectDB)     ← ObjectSlot sparse array enumeration
  ├── Flamme (StringName)      ← Interned string resolution
  ├── Diagoldze (ClassDB)       ← Class registry traversal
  ├── Serie (SceneTree)        ← Node hierarchy walking
  ├── Ubel (PropertyWalker)    ← Variant value reading
  ├── Fern (PipeServer)        ← Named pipe JSON IPC
  ├── Stark (ExportAPI)        ← C ABI exports for CE Lua
  ├── Heiter (GameThread)      ← MinHook-based dispatch
  ├── Qual (Mailbox)           ← CE Lua shared memory
  ├── Macht (CEPlugin)         ← CE Type 5 plugin interface
  ├── Eisen (ProxyVersion)     ← version.dll forwarding
  └── Grimoire (Constants)     ← DynOff namespace, magic strings
```

---

## C ABI Export Functions (Stark)

All prefixed `Zolt_`, usable from CE Lua via `callFunction()`.

### Initialization

| Export | Signature | Description |
|---|---|---|
| `Zolt_Init` | `int Zolt_Init()` | Scan ObjectDB/ClassDB/SceneTree, init subsystems |
| `Zolt_AutoStart` | `int Zolt_AutoStart()` | Init + start pipe server |
| `Zolt_Shutdown` | `void Zolt_Shutdown()` | Stop servers, cleanup |
| `Zolt_GetVersion` | `int Zolt_GetVersion()` | Return Godot version (e.g. 430 = 4.3.0) |

### Global Pointers

| Export | Signature | Description |
|---|---|---|
| `Zolt_GetObjectDBAddr` | `uint64_t Zolt_GetObjectDBAddr()` | ObjectDB::object_slots address |
| `Zolt_GetClassDBAddr` | `uint64_t Zolt_GetClassDBAddr()` | ClassDB::classes address |
| `Zolt_GetSceneTreeAddr` | `uint64_t Zolt_GetSceneTreeAddr()` | SceneTree::singleton address |

### Object Queries

| Export | Signature | Description |
|---|---|---|
| `Zolt_GetObjectCount` | `int Zolt_GetObjectCount()` | Total valid objects |
| `Zolt_GetObjectByIndex` | `uint64_t Zolt_GetObjectByIndex(int idx)` | Object* at slot index |
| `Zolt_GetObjectName` | `int Zolt_GetObjectName(uint64_t addr, char* buf, int bufSize)` | Object name → buffer |
| `Zolt_GetObjectClassName` | `int Zolt_GetObjectClassName(uint64_t addr, char* buf, int bufSize)` | Class name → buffer |
| `Zolt_FindObject` | `uint64_t Zolt_FindObject(const char* path)` | Find by node path |
| `Zolt_FindByAddress` | `uint64_t Zolt_FindByAddress(uint64_t addr)` | Reverse lookup |

### Class Walking (Batch Mode)

| Export | Signature | Description |
|---|---|---|
| `Zolt_WalkClassBegin` | `int Zolt_WalkClassBegin(const char* className)` | Start class walk → field count |
| `Zolt_WalkClassGetField` | `int Zolt_WalkClassGetField(int idx, ...)` | Get field details |
| `Zolt_WalkClassEnd` | `void Zolt_WalkClassEnd()` | Cleanup |

### Pipe Control

| Export | Signature | Description |
|---|---|---|
| `Zolt_StartPipeServer` | `int Zolt_StartPipeServer()` | Start named pipe |
| `Zolt_StopPipeServer` | `void Zolt_StopPipeServer()` | Stop named pipe |
| `Zolt_IsPipeConnected` | `int Zolt_IsPipeConnected()` | Check connection |

---

## Dynamic Offsets (DynOff)

All offsets in `Grimoire.h` `DynOff` namespace are mutable `inline int`, adjusted at runtime.

### Offset Validation Strategy

1. Find `Vector3` in ClassDB
2. Walk properties: expect X(float,+0), Y(float,+4), Z(float,+8)
3. If offsets differ → adjust all base offsets
4. Find `Color`: expect R,G,B,A (float fields)
5. Set `DynOff::bOffsetsValidated = true`

### DynOff Fields

```cpp
namespace DynOff {
    // Object layout (version-dependent)
    inline int OBJECT_VTABLE = 0x00;
    inline int OBJECT_EXTENSION = 0x08;
    inline int OBJECT_INSTANCE_ID = 0x18;
    inline int OBJECT_SCRIPT_INSTANCE = 0x??;

    // Node layout
    inline int NODE_DATA = 0x??;           // Offset to Node::Data struct
    inline int NODE_DATA_PARENT = 0x??;
    inline int NODE_DATA_CHILDREN = 0x??;
    inline int NODE_DATA_NAME = 0x??;
    inline int NODE_DATA_TREE = 0x??;

    // ObjectSlot layout (fixed)
    inline int SLOT_STRIDE = 16;
    inline int SLOT_OBJECT_OFFSET = 8;

    // Variant layout
    inline int VARIANT_TYPE = 0x00;
    inline int VARIANT_DATA = 0x08;

    // Validation state
    inline std::atomic<bool> bOffsetsValidated{false};
}
```

---

## AOB Signature Database

### Pattern Metadata

```cpp
struct AobSignature {
    const char* id;           // e.g. "OBJDB_SYM1"
    const char* pattern;      // Byte pattern or MSVC symbol
    enum Target { ObjectDB, ClassDB, SceneTree, Engine };
    Target target;
    enum Resolve { RipDirect, RipDeref, SymbolExport, RipBoth };
    Resolve resolve;
    int instrOffset;          // RIP instruction offset within match
    int opcodeLen;            // Opcode bytes before displacement
    int totalLen;             // Total instruction length
    int adjustment;           // Post-resolution tweak
    int priority;             // Lower = tried first
    const char* source;       // Attribution
    const char* notes;
};
```

### Initial Patterns

Priority 0 (MSVC symbol exports):
- `OBJDB_SYM1`: `?object_slots@ObjectDB@@` → ObjectDB
- `CLASSDB_SYM1`: `?classes@ClassDB@@` → ClassDB
- `STREE_SYM1`: `?singleton@SceneTree@@` → SceneTree
- `ENGINE_SYM1`: `?singleton_ptr@Engine@@` → Engine

Priority 50+ (AOB patterns): To be built from Godot binary analysis.

---

## Logging (Solitaer)

**Output**: `%LOCALAPPDATA%\ZoltDump\Logs\{ProcessName}\`

| Category | File Prefix | Content |
|---|---|---|
| Init | `init-*.log` | DLL load, version detection, module info |
| Scan | `scan-*.log` | AOB results, pointer discovery |
| Offsets | `offsets-*.log` | DynOff detection, validation results |
| Pipe | `pipe-*.log` | JSON commands, responses, errors |
| Walk | `walk-*.log` | Property reads, Variant values, tree walks |

4-file rotation, 8 MB cap per file.

---

## Hint Cache (Himmel)

**File**: `ZoltDump.{ComputerName}.json` in `%LOCALAPPDATA%\ZoltDump\`

```json
{
  "5F3A1B2CCDD40000": {
    "object_db_pattern_id": "OBJDB_SYM1",
    "class_db_pattern_id": "CLASSDB_AOB1",
    "scene_tree_pattern_id": "STREE_SYM1",
    "godot_version": "4.3.0"
  }
}
```

Key: PE image hash (base address + size + timestamp).

---

## Proxy DLL (Eisen)

**Output**: `version.dll` (placed in game folder)

Forwards all 17 version.dll API exports to `System32\version.dll`:
- GetFileVersionInfoA/W
- GetFileVersionInfoSizeA/W
- GetFileVersionInfoExA/W
- GetFileVersionInfoSizeExA/W
- VerFindFileA/W
- VerInstallFileA/W
- VerLanguageNameA/W
- VerQueryValueA/W

Uses `/DELAYLOAD:version.dll` to avoid circular dependency.
