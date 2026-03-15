# Build #99 Status — Bug Fixes & Testing Notes

> Cross-PC handoff document. Written 2026-03-09.

---

## Changes in This Build (PR #10)

### Builds #96-97: Phase 10+11 (Live Values + GDScript)
- **Phase 10**: ptrcall offset extraction (9 x86-64 instruction patterns), `ReadNativeValue()`, live value reading in `WalkInstance()`
- **Phase 11**: `ProbeScriptInstanceOffset()`, `ReadGDScriptMemberNames()`, `ReadGDScriptMembers()`, GDScript variable reading via Vector<Variant>
- `s_isItaniumABI` auto-detected in `ScanObjectVtables()` based on RTTI hit count

### Build #99: Bug Fixes
Five bugs fixed based on testing against Brotato (3.x) and OnePotionPlease (4.3):

1. **3.x wchar_t String reading** (`Zoltraak.cpp`)
   - Root cause: `ReadGodotString()` read `CowData<char32_t>` (4B/char) but Godot 3.x uses `CowData<wchar_t>` (2B/char on Windows)
   - Symptom: All 3.x property names garbled to single characters ('_', 'r', 'ȎȎ')
   - Fix: `ReadGodotString()` now checks `GodotVer::IsGodot3()` and reads wchar_t first. Added `ConvertWchar16ToUtf8()`, `ReadWcharString()`.
   - Impact: Fixes Brotato name display, property search, class struct, node filter

2. **ProbePropertyInfoLayout hint/usage unreachable** (`Diagoldze.cpp`)
   - Root cause: `return true` on successful chain validation exited before hint/usage derivation code
   - Symptom: `hintOff=0xFFFFFFFF usageOff=0xFFFFFFFF` in init log
   - Fix: Moved hint/usage derivation inside the chain validation success block, before `return true`

3. **ScriptInstance probe one-shot on wrong object** (`Ubel.cpp`)
   - Root cause: `bScriptInstanceProbed.exchange(true)` at function start blocked all retries after first call
   - Symptom: First WalkInstance on Engine (no GDScript) → probe fails → never retries on script-attached objects
   - Fix: Only set `bScriptInstanceProbed=true` on SUCCESS. Allow up to 10 retry attempts on different objects.

4. **DataGrid column resize** (`PropertySearchPanel.axaml`, `ProxyDeployPanel.axaml`)
   - Root cause: Missing `CanUserResizeColumns="True"` attribute
   - Fix: Added the attribute to both panels

5. **3.x ClassDB enumeration diagnostics** (`Diagoldze.cpp`)
   - Issue: Only 347 of 648 classes found in Brotato. Root cause unclear (element layout?).
   - Added: Hex dump of first HashMap element + multi-offset StringName probing in `BuildClassCache3x()` for next test run analysis.

---

## Known Issues (Need Investigation)

### 1. 3.x ClassDB: 347/648 Classes (Brotato)
- `power=7` (128 buckets), `elements=648`, but only 347 resolved (0 empty names)
- Hypothesis: `ELEM_3X_NEXT_OFF` may be wrong → chain walking stops after first element per bucket
- Diagnostics added in build #99: hex dump of first element + StringName probe at offsets 0x08/0x10/0x18/0x20
- **Action**: Check `init-0.log` after running Brotato with build #99. The hex dump will reveal the actual element layout.
- Grimoire.h offsets to potentially fix: `ELEM_3X_HASH_OFF`, `ELEM_3X_NEXT_OFF`, `ELEM_3X_KEY_OFF`, `ELEM_3X_VALUE_OFF`

### 2. SceneTree Root Not Found
- **OnePotionPlease (4.3)**: 0 Object candidates in SceneTree probe range 0x40-0x2000
- **Brotato (3.x)**: Picked `_child_minsize_changed` (wrong) — these are StringName entries, not the root Window/Node
- The SceneTree probe scans for pointers that resolve to Object instances with a "root" name. Godot 4.3 may store root at offset > 0x2000, or the probe criteria may be too strict.
- **Action**: May need to increase probe range further, or use a different strategy (e.g., look for Node with no parent)

### 3. Property Values Still Empty (OnePotionPlease)
- `WalkInstance` couldn't determine class for some objects (RTTI mismatch?)
- Many classes show "no properties" — these are engine singletons (WorkerThreadPool, InputMap, ClassDB, etc.) which genuinely have no user-facing properties
- Game-specific data (HP, Money, Gold, Weapon) lives in GDScript variables, which requires successful ScriptInstance probing on the right objects
- **Action**: After fixing SceneTree root, navigate to game nodes (Player, GameManager, etc.) and WalkInstance should find GDScript vars

### 4. Brotato property_setget First Key = 'r'
- Log shows: `Found 3.x 'property_setget' at ClassInfo+0x60 (power=3, num=11, first='r')`
- The 'r' is a truncated wchar_t read of a longer property name
- **Should be fixed** by the wchar_t ReadGodotString change, but needs verification

---

## File Change Summary

| File | Changes |
|------|---------|
| `Zoltraak.h` | Added `ReadWcharString()` declaration |
| `Zoltraak.cpp` | Added `ConvertWchar16ToUtf8()`, wchar_t path in `ReadGodotString()`, `ReadWcharString()`. Included `Grimoire.h` for `GodotVer`. |
| `Diagoldze.h` | Added `nativeSize` to `PropertyInfo` struct |
| `Diagoldze.cpp` | Fixed `ProbePropertyInfoLayout` hint/usage early return. Added 3.x element hex dump diagnostics. Integrated ptrcall offset extraction into `ReadPropertyListRaw()`. |
| `Ubel.h` | Added `offset`, `nativeSize` to `FieldEntry` struct |
| `Ubel.cpp` | Added `ReadNativeValue()`, `ProbeScriptInstanceOffset()` (with retry), `ReadGDScriptMemberNames()`, `ReadGDScriptMembers()`. Cleaned up STRING reading fallbacks. |
| `Grimoire.h` | Added GDScriptInstance DynOff constants, `STREE_PROBE_MAX` |
| `Fern.cpp` | Updated pipe JSON serialization for offset/size/hint fields |
| `PropertySearchPanel.axaml` | Added `CanUserResizeColumns="True"` |
| `ProxyDeployPanel.axaml` | Added `CanUserResizeColumns="True"` |

---

## Testing Checklist for Next Session

1. [ ] Clear logs: `rm -rf "$LOCALAPPDATA/ZoltDump/Logs"/*`
2. [ ] Run Brotato with proxy DLL (build #99)
3. [ ] Check `init-0.log`:
   - [ ] 3x Element hex dump — determine correct KEY_OFF/NEXT_OFF
   - [ ] PropertyInfo name probe — should show full property names (not single chars)
   - [ ] hint/usage offsets — should be valid (not 0xFFFFFFFF)
   - [ ] property_setget first key — should be full name (not 'r')
4. [ ] Check UI:
   - [ ] Object Tree — names should be full (not single chars)
   - [ ] Node Filter — class names correct
   - [ ] Class Struct — properties visible with types
   - [ ] Property Search — columns resizable
   - [ ] Proxy Deploy — columns resizable
5. [ ] Run OnePotionPlease with proxy DLL
6. [ ] Check `walk-0.log`:
   - [ ] ScriptInstance probe — should find offset on a script-attached object
   - [ ] GDScript variables — should show member names and values
7. [ ] Test LiveWalker on game objects (Player, etc.) for HP/Money/Gold
