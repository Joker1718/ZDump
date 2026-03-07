# Development Log — Zolt-Dump

## Status: Phase 0 — Scaffolding

### Completed

- [x] Directory structure created
- [x] CLAUDE.md written with build commands, rules, architecture diagram
- [x] docs/ planning documents created:
  - architecture.md — directory structure, build env, dependencies
  - godot-internals.md — ObjectDB, ClassDB, Variant, Node, SceneTree layouts
  - pipe-protocol.md — JSON IPC protocol (15 commands)
  - export-formats.md — CE XML, CSX, SDK header rules
  - ue-to-godot-mapping.md — feature mapping reference
  - naming-convention.md — Frieren naming guide
  - dll-spec.md — C++ DLL interface specification
  - dev-log.md — this file
  - technical-notes.md — Godot version notes

### In Progress

- [ ] build.ps1 / build.cmd
- [ ] tools/vendor-update.ps1
- [ ] Vendor dependency cloning
- [ ] CMakeLists.txt (dll/)
- [ ] ZoltDumpUI.csproj (ui/)
- [ ] Grimoire.h / Grimoire.cs constants

### Next Steps

1. Phase 1: Core Memory Layer (Zoltraak, Solliter, Frieren, Himmel)
2. Phase 2: Object System (Ausetiese, Flamme, DiAgolze, Ubel)
3. Phase 3: Scene & Tree (Serie)
4. Phase 4: IPC Layer (Fern, Stark, Heiter, Qual)
5. Phase 5: UI Foundation (PipeClient, DumpService, MainWindow)
6. Phase 6: UI Panels (9 panels)
7. Phase 7: Export Services (CE XML, CSX, SDK)
8. Phase 8: Proxy & Deploy (Eisen, ProxyDeploy)
9. Phase 9: CE Scripts (.CT, Lua)
10. Phase 10: Testing (xUnit)

---

## Known Challenges

### High Priority

1. **Property offsets**: Godot uses getter/setter pattern, not direct memory offsets like UE's FProperty::Offset_Internal. Must determine offset strategy:
   - Option A: Call getter via MethodBind vtable (reliable but complex)
   - Option B: Reverse-engineer memory layout per class (fragile but fast)
   - Option C: GDExtension API calls (reliable but requires extension loading)

2. **ClassDB HashMap traversal**: Need to reverse-engineer Godot's HashMap memory layout for external reading (bucket array, hash functions, entry layout).

3. **AOB signature development**: No existing community patterns for Godot. Must build from scratch by analyzing Godot binaries.

### Medium Priority

4. **Variant value reading**: 38 Variant types with complex union layout. Heap-allocated types (Transform, Basis) require pointer following.

5. **Script instance properties**: GDScript/C# script properties are stored in ScriptInstance, separate from native ClassDB properties. Need special handling.

6. **Godot version differences**: Layout may change between 4.0 and 4.7. Dynamic offset validation essential.

### Low Priority

7. **String resolution performance**: StringName uses hash table lookup, may be slower than UE's indexed FNamePool.

8. **Godot release builds**: May strip debug symbols, making MSVC symbol-based discovery fail. AOB patterns are fallback.

---

## Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Architecture | C++ DLL + C# Avalonia | Align with UE5CEDumper for consistency |
| IPC | Named Pipe (JSON) | Same as UE5CEDumper, proven pattern |
| Build system | build.ps1/build.cmd | Same as UE5CEDumper, human+AI usable |
| .NET version | 10.0-windows | Align with UE5CEDumper |
| Avalonia version | 11.3.12 | Align with UE5CEDumper |
| C++ standard | C++23 | Align with UE5CEDumper |
| Vendor deps | Clone (no submodule) | Per project requirement |
| Naming | Frieren anime terms | Per project requirement |

---

## Reference Projects

| Project | Path | Used For |
|---|---|---|
| UE5CEDumper | D:\Github\UE5CEDumper | Architecture template |
| AOBMaker | D:\Github\AOBMaker | CE integration, AA scripts |
| Godot Engine | D:\Github\godot | Memory layout reference |
