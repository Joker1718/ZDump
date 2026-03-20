# Frieren Naming Convention — Zolt-Dump

All module/namespace/class names in Zolt-Dump use terms from the anime "Frieren: Beyond Journey's End" (葬送的芙莉蓮).

**Rule**: Every Frieren-named entity MUST have a comment explaining its actual function.

---

## Project Name

**Zolt-Dump** — From **Zoltraak** (栔爾朵列), Frieren's signature attack magic. The project "deconstructs" Godot (from the play "Waiting for Godot" / 等待果陀).

---

## DLL Module Naming

| File | Frieren Term | Character/Spell | Actual Function | Why This Name |
|---|---|---|---|---|
| **Zoltraak.cpp** | 殺人魔法 | Attack magic | Core memory scanning: AOBScan, SEH reads, RIP resolution | The attack that deconstructs — core destructuring tool |
| **Solitaer.cpp** | 索莉塔 | (Original: Solitär) Mana concealment magic | Logger: 5-category file logging with rotation | Conceals traces — records hidden observations |
| **Ausetiese.cpp** | 服從之秤 | Aura's obedience scale | ObjectDB enumeration: slot scanning, validation | Weighs and sorts all objects (like Aura's power scale) |
| **Diagoldze.cpp** | 黃金化 | Mahat's golden transmutation | ClassDB scanner: immutable class registry | Immutable like gold — static class definitions |
| **Flamme.cpp** | 弗蘭梅 | Great mage, Frieren's master | StringName resolver: interned string lookup | Ancient knowledge keeper — resolves name origins |
| **Serie.cpp** | 賽莉耶 | Great mage, living history | SceneTree / Node hierarchy walker | Sees all history and hierarchy — living memory |
| **Ubel.cpp** | 尤蓓爾 | Special ops assassin | Variant value reader & property walker | Precise extraction — surgical data retrieval |
| **Frieren.cpp** | 芙莉蓮 | Protagonist, seeker | OffsetFinder: AOB signatures, version detection | The one who finds everything — eternal searcher |
| **Stark.cpp** | 修塔爾克 | Brave warrior, reliable | Export API: C ABI (~25 functions) | Strong, reliable interface — brave shield |
| **Fern.cpp** | 費倫 | Frieren's apprentice | Named Pipe server: JSON IPC dispatch | Communicator, messenger — bridges worlds |
| **Himmel.cpp** | 欣梅爾 | Hero, remembered | Hint cache: per-game AOB results | Remembered wisdom — the hero's lasting legacy |
| **Heiter.cpp** | 海塔 | Priest, healer | GameThread dispatch: MinHook queuing | Healing/restoring context — thread-safe dispatch |
| **Denken.cpp** | 頓肯 | Analytical mage | CLR hosting bridge: .NET reflection via ZoltReflector | Analytical magic — dissects managed type layouts |
| **Qual.cpp** | 庫瓦爾 | Seven Sages demon | Mailbox: CE Lua shared memory | Dark communication channel — demon's whisper |
| **Eisen.cpp** | 艾冉 | Iron warrior, defender | Proxy DLL: version.dll forwarding | Armored defense — iron shield proxy |
| **Macht.cpp** | 馬赫特 | Seven Sages (power) | CE Plugin: Type 5 interface | Raw power interface — direct force |
| **Grimoire.h** | 魔導書 | Grimoire | Constants, magic strings, DynOff namespace | Book of spells — configuration tome |

---

## Error Handling (Reserved — Not Yet Implemented)

| Term | Character | Usage | Status |
|---|---|---|---|
| **Mimic** | 寶箱怪 (chest mimic) | Error types, trap results, exception wrappers | Reserved for future use |

Currently, error handling is distributed across modules (try-catch, SEH, return `{0,0}`). Mimic namespace will be introduced when centralized error propagation is needed.

```cpp
// Mimic — 寶箱怪 — Error handling and trap detection (reserved design)
namespace Mimic {
    enum class TrapType {
        NullSlot,          // Empty object slot (the chest is empty)
        InvalidValidator,  // ObjectID validation failed (fake treasure)
        BadPointer,        // Unreadable memory address (trap triggered)
        TypeMismatch,      // Variant type != expected (wrong key)
        PipeDisconnect,    // IPC connection lost (chest slammed shut)
        ScanFailed,        // AOB pattern not found (hidden trap)
        VersionUnknown,    // Godot version unrecognized (unknown dungeon)
    };

    struct TrapResult {
        bool sprung;         // Did the trap trigger?
        TrapType type;       // What kind of trap
        std::string message; // Human-readable description
    };
}
```

---

## UI Naming

The UI uses standard English names for panels/services (NodeTreePanel, LiveWalkerPanel) but Frieren terms for internal constants and service identifiers:

```csharp
// Grimoire — 魔導書 — Application constants and magic strings
public static class Grimoire
{
    public const string PipeName = @"\\.\pipe\ZoltDumpBfx";
    public const string AppName = "Zolt-Dump";
    public const string LogDir = "ZoltDump";
    public const string HintCachePrefix = "ZoltDump";
    public const string MutexName = "Global\\ZoltDump_SingleInstance";
}
```

---

## Namespace Structure

### C++ DLL
```
Zolt::                      // Root namespace
  Zoltraak::                // Memory operations
  Solitaer::                // Logging
  Ausetiese::               // Object enumeration
  Diagoldze::                // Class registry
  Flamme::                  // String resolution
  Serie::                   // Scene tree
  Ubel::                    // Property/Variant reading
  Frieren::                 // Offset finding
  Fern::                    // Pipe server
  Himmel::                  // Hint cache
  Heiter::                  // Game thread
  Qual::                    // Mailbox
  Denken::                  // CLR hosting bridge
  Mimic::                   // Error handling (reserved)
  DynOff::                  // Dynamic offsets (in Grimoire.h)
```

### C# UI
```
ZoltDumpUI                  // Root namespace
  ZoltDumpUI.Models         // Data models
  ZoltDumpUI.Services       // Business logic
  ZoltDumpUI.ViewModels     // MVVM ViewModels
  ZoltDumpUI.Views          // AXAML Views
  ZoltDumpUI.Core           // Platform interfaces
```

---

## Additional Frieren Terms Available

These can be used for future features or internal naming:

| Term | Character | Suggested Use |
|---|---|---|
| **Linie** (リーニエ) | Demon, replication | Object cloning, deep copy |
| **Draht** (ドラート) | Demon, binding | Memory watch bindings |
| **Lugner** (リュグナー) | Demon, deception | Anti-detection, stealth |
| **Aura** (アウラ) | Demon, domination | Process control, injection |
| ~~**Denken**~~ | ~~Mage, analytical~~ | ~~Now in use — see DLL Module Naming~~ |
| **Wirbel** (ヴィアベル) | Mage, tactical | Strategy/optimization |
| **Richter** (リヒター) | Mage, earthquake | Memory region scanning |
| **Lawine** (ラヴィーネ) | Mage, ice | Memory freeze/snapshot |
| **Kanne** (カンネ) | Mage, plants | Growth/tree operations |
| **Sense** (ゼンゼ) | Death's scythe | Object destruction/cleanup |
| **Spiegel** (シュピーゲル) | Mirror clone | Reflection/introspection |

---

## Comment Format

Every Frieren-named file/class/namespace must include:

```
// {EnglishName} — {日本語/中文} ({meaning})
// {Actual function description}
```

Example:
```cpp
// Zoltraak — 栔爾朵列 (攻撃魔法 — Attack Magic)
// Core memory scanning: AOB pattern matching, SEH-protected reads, RIP-relative resolution
namespace Zolt::Zoltraak {
    // ...
}
```

```csharp
/// <summary>
/// Fern — 費倫 (弟子 — Apprentice)
/// Named pipe client for async bidirectional JSON IPC with ZoltDumper.dll
/// </summary>
public class PipeClient : IPipeClient
{
    // ...
}
```
