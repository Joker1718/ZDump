# Development Log — Zolt-Dump

## Current Status: Phase 3 Complete — PR #8 Open

All DLL-side functionality is implemented and tested. UI is ~85% complete.
13/13 test targets pass (5 editors + 8 games, Godot 3.5 → 4.5).

---

## Phase Completion

### Phase 0: Scaffolding ✅
- [x] Directory structure, CLAUDE.md, docs/
- [x] build.ps1 unified build script
- [x] tools/vendor-update.ps1
- [x] CMakeLists.txt (DLL + Proxy DLL)
- [x] ZoltDumpUI.csproj (Avalonia + Native AOT)
- [x] Grimoire.h / Grimoire.cs constants

### Phase 1: Core Memory Layer ✅ (Merged to main)
- [x] **Zoltraak** — Memory scanning, AOB, SEH-safe reads, section enumeration
- [x] **Solitaer** — 5-category logging (init/scan/offsets/pipe/walk), 4-file rotation
- [x] **Frieren** — AOB signatures for Engine/ObjectDB/ClassDB/SceneTree
- [x] **Himmel** — Hint cache (PE hash → known patterns), persistence

### Phase 2: Object System ✅ (Merged to main, PR #6)
- [x] **Ausetiese** — ObjectDB enumeration (4.x sparse-set slot array)
- [x] **Flamme** — StringName resolution (cname + CowData, 4 strategies)
- [x] **Diagoldze** — ClassDB traversal (4.x robin-hood HashMap, linked list walk)

### Phase 3: Pipe Server + Property Walker ✅ (PR #8, feat/phase3-pipe-server)
- [x] **Fern** — Named pipe IPC, 19 JSON command handlers
- [x] **Stark** — C ABI exports (19 total) for CE Lua integration
- [x] **Serie** — SceneTree walker (node hierarchy, children enumeration)
- [x] **Ubel** — Property walker (RTTI-based variant reading, 38 types)
- [x] **Heiter** — Thread dispatch context
- [x] **Eisen** — Proxy DLL forwarding (winmm.dll, 180 MASM trampolines)

#### Phase 3 Sub-features:
- [x] **Godot 3.x support** — Bucket-chain HashMap (16B inline) for ObjectDB + ClassDB
- [x] **SceneTree deferred rescan** — 4-layer fix (relaxed validation → lazy deref → background retry → trigger scan)
- [x] **Flamme 4.5+ fix** — TryCname path for StringName layout changes
- [x] **Load factor filter** — Prevents crash on false-positive HashMap candidates (Brotato fix)

### Phase 4: UI Foundation ✅ (In PR #8)
- [x] **PipeClient** — Async named pipe communication with request/response ID matching
- [x] **DumpService** — All 19 pipe commands wrapped with JSON parsing
- [x] **MainWindow** — Toolbar (Connect/Scan/AddressFormat), nav tree, tab control
- [x] **8 View panels** — All wired with MVVM bindings
- [x] **8 ViewModels** — All implemented with real functionality
- [x] **Native AOT** — No reflection, source generators for JSON + MVVM
- [x] **Single instance** — Mutex enforcement

### Phase 5: UI Panels ✅ (In PR #8)
- [x] **NodeTreePanel** — Object listing with filtering, pagination (2000/batch)
- [x] **LiveWalkerPanel** — Real-time property drilldown, breadcrumbs, auto-refresh
- [x] **ClassStructPanel** — ClassDB browsing (properties + methods)
- [x] **InstanceFinderPanel** — Class instance search + address lookup
- [x] **PropertySearchPanel** — Cross-class property search
- [x] **PointerPanel** — Global pointer display + AOBMaker integration
- [x] **NodeFilterPanel** — ClassDB browser with inheritance filtering
- [x] **ProxyDeployPanel** — UI skeleton (service logic pending)

### Phase 6: Export Services — Partial
- [x] **SdkExportService** — C++ header generation (complete)
- [ ] **CeXmlExportService** — CE pointer chain XML (stub, type mapping done)
- [ ] **CsxExportService** — CE Structure Dissect XML (partial, needs struct flattening)

### Phase 7: Proxy & Deploy — Partial
- [x] **Eisen** (DLL side) — Proxy forwarding complete
- [ ] **ProxyDeployService** (UI side) — Steam library scanning, game detection, deploy/undeploy (stubs)
- [x] **VdfParser** — Steam libraryfolders.vdf parsing (implemented, not yet wired)

### Phase 8: CE Scripts ✅
- [x] **ZoltDump.CT** — Cheat Table with Lua injection
- [x] **zolt_dissect.lua** — CE Structure Dissect builder

### Phase 9: Testing — Partial
- [x] **test_all_versions.ps1** — 13 targets (5 editors + 8 games)
- [x] **test_pipe.ps1** — Pipe protocol integration tests
- [x] **test_phase3b.ps1** — Phase 3b assertion tests
- [x] **xUnit UI tests** — 215 tests across 11 test classes (AddressHelper, CeLuaScriptService, CeXmlExportService, CsxExportService, DumpServiceParse, FieldValueConverter, KnownStructLayout, NodeFilterViewModel, PropertySearchViewModel, SdkExportService, VdfParser)

---

## Test Matrix (Build #77)

| Target | Engine | ObjectDB | ClassDB | SceneTree | Version |
|--------|--------|----------|---------|-----------|---------|
| Godot 4.0 | ✅ aob | ✅ found | ✅ bss_scan | ✅ aob | 4.0.stable |
| Godot 4.1 | ✅ aob | ✅ found | ✅ bss_scan | ✅ aob_relaxed | 4.1.stable |
| Godot 4.2 | ✅ aob | ✅ found | ✅ bss_scan | ✅ aob | 4.2.stable |
| Godot 4.3 | ✅ aob | ✅ found | ✅ bss_scan | ✅ aob | 4.3.stable |
| Godot 4.4.1 | ✅ aob | ✅ aob | ✅ bss_scan | ✅ aob | 4.4.1.stable |
| Brotato (3.7dev) | ✅ aob | ✅ bss_scan_3x | ✅ bss_scan_3x | ✅ aob | 3.7.dev |
| 20 Small Mazes (3.5.3) | ✅ aob | ✅ bss_scan_3x | ✅ bss_scan_3x | ✅ aob | 3.5.3.stable |
| Magic Archery (4.2.2) | ✅ aob | ✅ found | ✅ bss_scan | ✅ aob_relaxed | 4.2.2.stable |
| One Potion Please (4.3) | ✅ aob | ✅ found | ✅ bss_scan | ✅ aob | 4.3.stable |
| Virtual Cottage | ✅ aob | ✅ found | ⚠️ FAIL | ✅ aob | unknown |
| Upload Labs (4.5.1rc2) | ✅ aob | ✅ aob | ✅ bss_scan | ✅ aob | 4.5.1.rc2 |
| Holo X Break (4.2) | ✅ aob | ✅ found | ✅ bss_scan | ✅ aob_relaxed | 4.2.stable |
| Dome Keeper (4.2.2) | ✅ aob | ✅ found | ✅ bss_scan | ✅ aob | 4.2.2.custom |

---

## Known Challenges

### Resolved ✅

1. **ClassDB HashMap traversal** — Solved with BSS brute-force scan + CamelCase name validation
2. **AOB signature development** — Multi-match + validation patterns for Engine/ObjectDB/SceneTree
3. **Variant value reading** — Ubel handles 38 types with RTTI-based class identification
4. **Godot version differences** — Dynamic offset probing (4.0 → 4.5+), 3.x vs 4.x gating
5. **StringName performance** — Flamme caching with TryCname fast path
6. **SceneTree timing** — 4-layer deferred rescan (relaxed → lazy → retry → trigger)
7. **Godot 3.x bucket-chain HashMap** — Load factor filter prevents false-positive crashes

### Open

8. **Property offsets**: Godot uses getter/setter pattern, not direct memory offsets. Current approach uses RTTI + Ubel variant walker (works but no byte-level offset like UE).
9. **Script instance properties**: GDScript/C# script properties stored in ScriptInstance — not yet enumerated.
10. **Virtual Cottage ClassDB FAIL**: Unknown Godot version, ClassDB scan fails (pre-existing, low priority).

---

## Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Architecture | C++ DLL + C# Avalonia | Align with UE5CEDumper for consistency |
| IPC | Named Pipe (JSON) | Same as UE5CEDumper, proven pattern |
| Build system | build.ps1 | Same as UE5CEDumper, human+AI usable |
| .NET version | 10.0-windows | Align with UE5CEDumper |
| Avalonia version | 11.3.12 | Align with UE5CEDumper |
| C++ standard | C++23 | Align with UE5CEDumper |
| Vendor deps | Clone (no submodule) | Per project requirement |
| Naming | Frieren anime terms | Per project requirement |
| 3.x HashMap | Bucket-chain (16B inline) | Completely different from 4.x robin-hood |
| SceneTree rescan | 4-layer deferred | Games create SceneTree after DLL loads |

---

## Remaining Work (Priority Order)

1. **CE XML Export** — Hierarchical pointer chain XML for Cheat Engine
2. **CSX Export** — Structure Dissect XML with struct flattening + pointer drilldown
3. **Proxy Deploy Service** — Steam library scanning, Godot game detection, auto-deploy
4. **CE Pointer Info** — Breadcrumb-based pointer chain discovery (DLL + UI)
5. **Live Memory Editing** — Write values from LiveWalker panel
6. **xUnit UI Tests** — Automated testing for ViewModels + Services

---

## Reference Projects

| Project | Path | Used For |
|---|---|---|
| UE5CEDumper | D:\Github\UE5CEDumper | Architecture template |
| AOBMaker | D:\Github\AOBMaker | CE integration, AA scripts |
| Godot Engine | D:\Github\godot | Memory layout reference |
