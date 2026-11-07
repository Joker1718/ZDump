# Development Log — Zolt-Dump

## Current Status: Phase 12 — Godot 4.7 + .NET 8 self-contained (AscensionSandbox)

All DLL-side functionality is implemented and tested. UI is ~97% complete.
13/13 test targets pass (5 editors + 8 games, Godot 3.5 → 4.5); AscensionSandbox
(4.7, .NET 8) added as 14th target — statically verified, runtime test pending.
215 unit tests passing. .NET field name resolution working on Slay the Spire 2.

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

### Phase 7: Proxy & Deploy ✅
- [x] **Eisen** (DLL side) — Proxy forwarding complete
- [x] **ProxyDeployService** (UI side) — Steam library scanning, Godot game detection, deploy/undeploy with version tracking
- [x] **ZoltReflector deploy** — Auto-detects .NET games and deploys/undeploys ZoltReflector.dll alongside winmm.dll
- [x] **VdfParser** — Steam libraryfolders.vdf parsing

### Phase 8: CE Scripts ✅
- [x] **ZoltDump.CT** — Cheat Table with Lua injection
- [x] **zolt_dissect.lua** — CE Structure Dissect builder

### Phase 9: Testing — Partial
- [x] **test_all_versions.ps1** — 13 targets (5 editors + 8 games)
- [x] **test_pipe.ps1** — Pipe protocol integration tests
- [x] **test_phase3b.ps1** — Phase 3b assertion tests
- [x] **xUnit UI tests** — 215 tests across 11 test classes (AddressHelper, CeLuaScriptService, CeXmlExportService, CsxExportService, DumpServiceParse, FieldValueConverter, KnownStructLayout, NodeFilterViewModel, PropertySearchViewModel, SdkExportService, VdfParser)

### Phase 10: .NET Managed Object Support ✅
- [x] **.NET game detection** — GodotSharp/coreclr module check at AutoStart time
- [x] **CSharpInstance layout probe** — Runtime discovery of owner (+0x08/+0x10) and GCHandle (+0x20/+0x28) offsets
- [x] **CMD_FIND_MANAGED_INSTANCES** — Mailbox bulk scan with class, node name, field value, and MT filters
- [x] **Node name filtering** — Substring match on Godot node name to prevent unrelated objects from polluting auto-discover MT. Auto-generated names (`@Control@1978`) are normalized to `@Control@` for stable cross-session matching
- [x] **Auto-discover MethodTable** — Empty MT auto-locks to first match; node name filter ensures correct type. Scripts survive game restarts
- [x] **L1/L2 matching** — L1 checks wrapper directly; L2 scans wrapper references for nested .NET children with valid MethodTable
- [x] **Smart dedup** — Deduplication by managedAddr with wrapper offset tiebreaking (smaller = more direct owner)
- [x] **L2 alternatives** — `find_by_address` returns all Godot objects referencing same managed child
- [x] **CE Lua multi-result scripts** — Creates CE memory records under group headers with filter criteria tags
- [x] **Managed filter dialog** — UI dialog for selecting target/filter fields from managed sibling list

### Phase 11: Metode CLR Bridge ✅ (renamed from Denken)
- [x] **Metode** (梅特黛) — CLR hosting bridge via hostfxr NULL handle strategy for self-contained .NET games
- [x] **ZoltReflector.dll** — .NET class library loaded into game's CoreCLR, enumerates types and field layouts via reflection
- [x] **Field name resolution** — Ubel uses Metode to resolve real .NET field names (`_currentHp`, `<Side>k__BackingField`) instead of raw offsets (`+0x84`)
- [x] **FieldDesc offset extraction** — CoreCLR FieldDesc low-27-bit offset + IntPtr.Size correction for reference types
- [x] **CMD_RESOLVE_FIELD_OFFSET** — Mailbox command for CE Lua runtime field offset resolution by name
- [x] **Update-resilient CE scripts** — Generated Lua resolves field offsets by name at runtime; scripts survive game updates that change field layouts
- [x] **ProxyDeployService** — Auto-deploys/undeploys ZoltReflector.dll for .NET Godot games
- [x] **Managed filter dialog** — Shows Metode-resolved field names with C# type support (enums, System.Int32, etc.)
- [x] **L2 child ref offset** — BreadcrumbItem stores MemberOffset for Metode-resolved navigation (e.g. `_player` at 0x58)
- [x] Tested on Slay the Spire 2 (Godot 4.5.1, .NET 9 self-contained)

### Phase 12: Godot 4.7 + .NET 8 self-contained (AscensionSandbox) ✅ (static) / ⏳ (runtime)

Target: `AscensionSandbox.exe` — Godot v4.7.stable.mono.official, .NET 8
self-contained (coreclr/hostfxr in `data_SandboxCultivation_windows_x86_64/`),
117 MB, exports stripped (3 names, no Godot symbols).

- [x] **Static AOB verification** — all 31 GodotSig patterns scanned against the
      shipped exe (offline, no runtime):
  - ObjectDB: `OBJDB_AOB2` exactly 1 hit → `.bss 0x146946DA8`; cross-confirmed by
    an `OBJDB_AOB3` hit on the same code region resolving to the same variable.
    `OBJDB_AOB1` (tuned on 4.4.1) has 0 hits on 4.7 — expected, AOB2 covers it.
  - ClassDB: `CLASSDB_AOB2` 2 hits → same `.bss 0x14693CDA0` (two code sites
    referencing one static HashMap). `CLASSDB_AOB1/3` produce only .rdata false
    positives → rejected by MultiMatch layout validation.
  - SceneTree/Engine: generic singleton-getter patterns hit (27/549/1295 raw);
    MultiMatch + vtable validation picks, same situation as passing 4.x targets.
  - Writable-section gate (`TryPattern` RipDirect) satisfied: both hard hits
    resolve into .bss.
- [x] **Variant::Type enum unchanged in 4.7-stable** — OBJECT=24, DICTIONARY=27,
      VARIANT_MAX=39; verified against godotengine/godot tag `4.7-stable`
      `core/variant/variant.h`. Zolt's hardcoded 4.x constants remain correct.
      **Trap**: the "Nil/bool/int/..." string blob order in the binary is
      `Variant::get_type_name` switch-case emission order, NOT enum order — do
      not "fix" type constants from string order.
- [x] **Version detection** — exe contains "Godot Engine v4.7.stable.mono.official".
      `VERSION_RDATA2` ("4.x.y.stable", 3 components) has 0 hits on the
      2-component "4.7.stable" — dead pattern on 4.7 but harmless:
      `DetectVersionFromRdata` Try-1a covers it, and `ParseVersion` handles
      "4.7.stable" (patch stays 0 when the 3rd component is non-numeric).
- [x] **Metode on .NET 8 self-contained** — same strategy as STS2 (.NET 9):
      hostfxr NULL-handle (active context) primary; `FindRuntimeConfig` finds
      `data_*/SandboxCultivation.runtimeconfig.json` as fallback. Init runs after
      ObjectDB/ClassDB scan → hostfxr already loaded by the apphost.
- [x] **ZoltReflector TFM** — targets net8.0: loads into .NET 8 (this game) and
      rolls forward into .NET 9 (STS2). No multi-target needed.
- [x] **ProxyDeployService.IsDotNetGame** — covers `data_*/GodotSharp.dll` and
      `data_*/coreclr.dll` layouts → AscensionSandbox detected, ZoltReflector
      deploy/undeploy works from the UI.
- [x] **test_all_versions.ps1** — AscensionSandbox added as game target
      (`--headless`); script paths made checkout-relative (was hardcoded
      D:\Github\Zolt-Dump); .NET targets now auto-deploy/undeploy
      `dist\proxy\ZoltReflector.dll` alongside winmm.dll.
- [ ] Runtime test (user compiles; agent does not build).

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
| AscensionSandbox (4.7) | ⏳ pending build | ⏳ pending | ⏳ pending | ⏳ pending | 4.7.stable.mono |

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
9. ~~**Script instance properties**: GDScript/C# script properties stored in ScriptInstance — not yet enumerated.~~ **Resolved** — GDScript members enumerated via Ubel, .NET managed objects via GCHandle chain + CSharpInstance probe.
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
3. **CE Pointer Info** — Breadcrumb-based pointer chain discovery (DLL + UI)
4. **Live Memory Editing** — Write values from LiveWalker panel
5. **Godot version display fix** — Shows "8.J" instead of "4.5.1" for some games

---

## Reference Projects

| Project | Path | Used For |
|---|---|---|
| UE5CEDumper | D:\Github\UE5CEDumper | Architecture template |
| AOBMaker | D:\Github\AOBMaker | CE integration, AA scripts |
| Godot Engine | D:\Github\godot | Memory layout reference |
