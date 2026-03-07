# Technical Notes — Godot Version Differences & Offset Detection

---

## Godot Version Detection

### Strategy 1: PE VERSIONINFO

Read PE VERSIONINFO from the game executable:
- `ProductVersion` or `FileVersion` fields
- Example: `"4.3.0.stable"`, `"4.2.1.stable"`

### Strategy 2: String Scan

Scan `.rdata` section for version strings:
- Pattern: `"Godot Engine v4."` followed by version digits
- Pattern: `"4.x.x.stable"` or `"4.x.x.rc"`

### Strategy 3: Export Symbol

Check for Godot-specific exports:
- `godot_gdextension_init` or similar GDExtension entry points
- Module name pattern: `godot.windows.opt.*.x86_64.exe`

---

## Godot 4.x Version Differences

### Godot 4.0 → 4.1

- Minor internal changes
- ObjectDB layout stable
- ClassDB API additions (no breaking changes)

### Godot 4.1 → 4.2

- Threading improvements (SpinLock → Mutex in some cases)
- HashMap implementation changes (may affect memory layout)
- New Variant types possible

### Godot 4.2 → 4.3

- GDExtension API v2 (ObjectGDExtension layout changes)
- New Node features
- Additional ClassDB methods

### Godot 4.3 → 4.4

- Performance optimizations
- Possible Object memory layout changes
- New engine features

### Godot 4.5+ (Future)

- Potential breaking changes
- New Variant types
- Object layout evolution

---

## Dynamic Offset Validation (DynOff)

### Why Dynamic Offsets?

Godot's Object/Node memory layout is not ABI-stable between versions. Compiler flags, #ifdef blocks, and struct member additions can shift field offsets.

### Validation Approach

Like UE5CEDumper's DynOff, probe known-layout structures:

#### Probe 1: Vector3

```
Find "Vector3" in ClassDB
Walk properties:
  Expected: X (float, offset 0), Y (float, offset 4), Z (float, offset 8)
  If actual offsets differ → calculate offset delta
```

#### Probe 2: Color

```
Find "Color" in ClassDB
Walk properties:
  Expected: R (float, offset 0), G (float, offset 4), B (float, offset 8), A (float, offset 12)
  Validate against known layout
```

#### Probe 3: Object base

```
Find any Object instance in ObjectDB
Read vtable pointer at offset 0 (should be in .text section)
Read ObjectID at expected offset (should match slot validator)
If ObjectID validation fails → scan nearby offsets for valid ObjectID
```

#### Probe 4: Node::Data

```
Find SceneTree::root (Window)
Try reading Node::Data fields at expected offsets
Validate: parent should be null (root has no parent)
Validate: name should be "root" (StringName)
If name doesn't match → scan nearby offsets for StringName pointer
```

---

## Godot HashMap Memory Layout

Understanding Godot's HashMap is critical for reading ClassDB.

### HashMap<K, V> (core/templates/hash_map.h)

```cpp
template <class TKey, class TValue>
class HashMap {
    uint32_t capacity;           // Bucket count (power of 2)
    uint32_t num_elements;       // Current element count

    struct Entry {
        TKey key;
        TValue value;
        Entry *next;             // Chain for collisions
        uint32_t hash;
    };

    Entry **buckets;             // Array of Entry pointers
};
```

### AHashMap<K, V> (core/templates/a_hash_map.h)

Flat (open-addressing) hash map, different from HashMap:

```cpp
template <class TKey, class TValue>
class AHashMap {
    uint32_t capacity;
    uint32_t num_elements;

    struct Entry {
        TKey key;
        TValue value;
    };

    Entry *entries;              // Flat array
    uint32_t *hashes;           // Parallel hash array
};
```

### Traversal Strategy

For HashMap (ClassDB::classes uses HashMap<StringName, ClassInfo>):
1. Read `capacity` and `buckets` pointer
2. For each bucket [0..capacity-1]:
   - Read Entry* from bucket
   - While Entry != null:
     - Read key (StringName), value (ClassInfo)
     - Follow `next` pointer for chain

---

## Variant Union Size

The Variant union `_data` size depends on build configuration:

| Build | `_data` size | Total Variant size |
|---|---|---|
| 32-bit | 24 bytes | 28 bytes |
| 64-bit (standard) | 24 bytes | 32 bytes |
| 64-bit (some builds) | 40 bytes | 48 bytes |

Detection: Read Variant for a known object, check if type/value align at offset 0x08 or 0x10.

---

## String (CoW) Memory Layout

Godot's String uses Copy-on-Write:

```
String object:
  +0x00  CowData<char32_t>
    +0x00  char32_t *_ptr  → points to data after header

Header (before data):
  -0x08  SafeRefCount refcount
  -0x04  uint32_t size (element count)

Data:
  +0x00  char32_t[] (UTF-32 characters, null-terminated)
```

To read a String:
1. Read `_ptr` (8 bytes)
2. Read `size` at `_ptr - 4` (4 bytes)
3. Read `size * 4` bytes at `_ptr` (UTF-32)
4. Convert UTF-32 → UTF-8

---

## Comparison: UE vs Godot Complexity

| Aspect | UE5CEDumper | Zolt-Dump |
|---|---|---|
| AOB patterns | 51+ (community-contributed) | Start from scratch |
| Object array | Complex (chunked + flat + stride detection) | Simple (sparse array, fixed 16B stride) |
| String resolution | Complex (3 modes, hash-prefixed variant) | Medium (hash table, pointer chase) |
| Property offsets | Direct (FProperty::Offset_Internal) | Indirect (getter/setter or reverse-engineer) |
| Version spread | UE 4.18 ~ 5.7 (huge spread) | Godot 4.0 ~ 4.7 (smaller spread) |
| Class hierarchy | Deep UObject/UStruct chain | Simpler Object → Node hierarchy |
| Container types | TArray/TMap/TSet (complex) | Array/Dictionary (simpler Variant wrappers) |
| Encryption | Object pointer encryption possible | No encryption |
