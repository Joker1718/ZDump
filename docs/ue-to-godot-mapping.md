# UE5CEDumper → Zolt-Dump Feature Mapping

This document maps every UE5CEDumper feature to its Godot equivalent implementation.

---

## Global Pointer Mapping

| UE5CEDumper | Zolt-Dump | Module | Notes |
|---|---|---|---|
| GObjects (FUObjectArray*) | ObjectDB::object_slots | Ausetiese | Sparse array vs chunked array — simpler |
| GNames (FNamePool*) | StringName hash table | Flamme | Hash table vs indexed pool |
| GWorld (UWorld**) | SceneTree::singleton | Serie | Direct singleton vs double-deref |
| N/A | ClassDB::classes | DiAgolze | Godot-specific: class registry |
| N/A | Engine::singleton | Frieren | Godot-specific: engine singletons |

## Object System Mapping

| UE Concept | Godot Equivalent | Difficulty | Notes |
|---|---|---|---|
| UObject | Object | Easy | Both have instance IDs |
| FUObjectItem (16-24B) | ObjectSlot (16B fixed) | Easy | Simpler: 8B metadata + 8B ptr |
| Chunked/Flat array | Sparse array | Easy | Single array, no chunk logic |
| ClassPrivate (direct ptr) | ObjectID → ClassDB | Medium | No direct class ptr in Object |
| OuterPrivate | Node::parent | Easy | Direct parent pointer |
| NamePrivate (FName) | Node::name (StringName) | Medium | Different resolution path |
| ChildProperties (FField chain) | ClassInfo::property_list | Medium | List vs linked list |
| FProperty offset | PropertySetGet getter | Hard | Godot uses getter/setter, not offsets |
| FField::Next | List iteration | Easy | Standard container |

## Scene Hierarchy Mapping

| UE Concept | Godot Equivalent | Notes |
|---|---|---|
| GWorld → PersistentLevel → Actors | SceneTree::root → children | Tree vs level model |
| AActor array | Node children HashMap | Named children vs indexed |
| UActorComponent | Node | Everything is a Node |
| UWorld::PersistentLevel | SceneTree::root (Window) | Root is always Window |
| Level streaming | SceneTree::current_scene | Scene change |
| walk_world command | walk_tree command | Renamed for Godot |

## Type System Mapping

| UE FProperty | Godot Variant::Type | CE VariableType |
|---|---|---|
| BoolProperty | BOOL | Byte |
| ByteProperty | INT | Byte |
| IntProperty | INT | 4 Bytes → 8 Bytes (Godot uses int64) |
| Int64Property | INT | 8 Bytes |
| FloatProperty | FLOAT | Float → Double (Godot uses double) |
| DoubleProperty | FLOAT | Double |
| StrProperty (FString) | STRING | String |
| NameProperty (FName) | STRING_NAME | String |
| ObjectProperty | OBJECT (ObjData) | Pointer |
| StructProperty | VECTOR2/3/4, COLOR, etc. | Group (expanded) |
| ArrayProperty (TArray) | ARRAY | Group + elements |
| MapProperty (TMap) | DICTIONARY | Group + key-value |
| EnumProperty | INT + enum_map | 4 Bytes + DropDownList |
| TextProperty | STRING | String |
| SoftObjectProperty | NODE_PATH | String |

## DLL Module Mapping

| UE5CEDumper File | Zolt-Dump File | Frieren Name | Port Difficulty |
|---|---|---|---|
| Memory.cpp | Zoltraak.cpp | 栔爾朵列 | **Direct port** — identical functionality |
| Logger.cpp | Solliter.cpp | 索莉緹爾 | **Direct port** — same design |
| ObjectArray.cpp | Ausetiese.cpp | 服從之秤 | **Adapt** — sparse array vs chunked |
| FNamePool.cpp | Flamme.cpp | 芙拉梅 | **Rewrite** — hash table vs indexed pool |
| UStructWalker.cpp | Ubel.cpp | 優貝爾 | **Major rewrite** — Variant vs FProperty |
| OffsetFinder.cpp | Frieren.cpp | 芙莉蓮 | **Rewrite** — new AOB patterns |
| (walk_world) | Serie.cpp | 賽莉耶 | **Rewrite** — tree model vs level model |
| (ClassDB) | DiAgolze.cpp | 黃金化 | **New** — Godot-specific |
| ExportAPI.cpp | Stark.cpp | 修塔爾克 | **Adapt** — same pattern, new functions |
| PipeServer.cpp | Fern.cpp | 費倫 | **Adapt** — same protocol, new commands |
| HintCache.cpp | Himmel.cpp | 欣梅爾 | **Direct port** — same concept |
| GameThreadDispatch.cpp | Heiter.cpp | 海塔 | **Direct port** — same MinHook approach |
| Mailbox.cpp | Qual.cpp | 全 | **Direct port** — same CE Lua bridge |
| ProxyVersion.cpp | Eisen.cpp | 艾乍 | **Direct port** — identical mechanism |
| CEPlugin.cpp | Macht.cpp | 馬赫特 | **Direct port** — same Type 5 interface |
| Constants.h | Grimoire.h | 魔導書 | **Adapt** — Godot constants |
| dllmain.cpp | dllmain.cpp | - | **Adapt** — same pattern |

## UI Panel Mapping

| UE5CEDumper Panel | Zolt-Dump Panel | Changes |
|---|---|---|
| ObjectTreePanel | NodeTreePanel | Rename; show Node tree instead of UObject tree |
| LiveWalkerPanel | LiveWalkerPanel | Adapt for Variant types |
| ClassStructPanel | ClassStructPanel | Show ClassDB info |
| InstanceFinderPanel | InstanceFinderPanel | Same concept |
| PointerPanel | PointerPanel | Show ObjectDB/ClassDB/SceneTree |
| ProxyDeployPanel | ProxyDeployPanel | Same mechanism |
| PropertySearchPanel | PropertySearchPanel | Same concept |
| GameClassFilterPanel | NodeFilterPanel | Rename for Godot |

## UI Service Mapping

| UE5CEDumper Service | Zolt-Dump Service | Changes |
|---|---|---|
| PipeClient | PipeClient | Change pipe name only |
| DumpService | DumpService | Adapt commands |
| CeXmlExportService | CeXmlExportService | Adapt pointer model |
| CsxExportService | CsxExportService | Adapt field types |
| SdkExportService | SdkExportService | Adapt struct format |
| ProxyDeployService | ProxyDeployService | Same mechanism |
| LoggingService | LoggingService | Change app name |
| AobMakerBridgeService | AobMakerBridgeService | Same IPC |
| WindowsPlatformService | WindowsPlatformService | Same P/Invoke |
| AobUsageService | AobUsageService | Same concept |
| VdfParser | VdfParser | Same Steam detection |

## Export Format Mapping

| Feature | UE5CEDumper | Zolt-Dump | Notes |
|---|---|---|---|
| CE XML root | "game.exe"+GWorldRVA | "game.exe"+SceneTreeRVA | Different root pointer |
| CE XML pointer chain | UObject→Outer→Field | Node→parent→property | Different chain model |
| CE XML StructProperty | FField-based expansion | Variant type expansion | Different inline types |
| CE XML ArrayProperty | TArray deref + elements | Array.data deref + elements | Similar pattern |
| CSX fields | FProperty offsets | Variant/property offsets | Different offset source |
| SDK headers | UClass inheritance | ClassDB inheritance | Different metadata |

## Features NOT Applicable

| UE Feature | Why N/A |
|---|---|
| FField vs UProperty fallback | Godot has single property system |
| CasePreservingName detection | StringName has no CPN variation |
| Chunked vs Flat GObjects | Godot uses single sparse array |
| Object pointer encryption | Godot doesn't encrypt pointers |
| FUObjectItem stride detection | Fixed 16-byte stride |
| Hash-prefixed FNamePool | StringName uses different model |
| UE version 4.x vs 5.x splits | Godot 4.x has fewer breaking changes |
