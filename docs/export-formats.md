# Export Formats — CE XML, CSX, SDK Header

This document describes how LiveWalker exports field data to Cheat Engine and C++ formats.

Mirrors UE5CEDumper's export rules, adapted for Godot's node/property model.

---

## CE XML Export

**Output**: Clipboard (paste into CE address list)
**Service**: `CeXmlExportService.cs`
**Entry points**: `ExportCeXmlAsync`, `ExportCeFieldXmlAsync` (single field)

### Hierarchical Pointer Chain Model

CE XML uses nested `<CheatEntry>` nodes where each child's address is **relative to its parent's resolved address**.

```
SceneTree → root → Node → child → property
  │           │      │       │         └── Address=+{offset}  (leaf, no Offsets)
  │           │      │       └── Address=+{childOffset}, Offsets=[0]  (pointer deref)
  │           │      └── Address=+{nodeOffset}, Offsets=[0]
  │           └── Address=+{rootOffset}, Offsets=[0]
  └── Address="game.exe"+RVA  (root, absolute)
```

### Address Rules

| Node Type | Address Tag | Offsets Tag | CE Resolution |
|-----------|-------------|-------------|---------------|
| **Root** | `"module.exe"+RVA` or hex | (none) | Absolute address |
| **Pointer breadcrumb** | `+{fieldOffset}` | `<Offset>0</Offset>` | `*(parent + offset)` then children offset from result |
| **Inline breadcrumb** (struct) | `+{fieldOffset}` | (none) | `parent + offset` |
| **Leaf field** (scalar) | `+{fieldOffset}` | (none) | `parent + offset` |
| **Array group** | `+{fieldOffset}` | `<Offset>0</Offset>` | Dereferences Array.data pointer |
| **Array element** | `+{N * elemSize}` | (none) | Offset from data pointer |
| **Dictionary group** | `+{fieldOffset}` | `<Offset>0</Offset>` | Dereferences internal storage |

> **Critical rule**: Never emit `Address=+0, Offsets=[fieldOffset]` — this causes a double-dereference bug. Always use `Address=+{offset}` with `Offsets=[0]` for pointer deref.

### Breadcrumb → XML Mapping

1. **Root** (breadcrumbs[0]): Absolute address, `GroupHeader=1`
2. **Intermediate** (breadcrumbs[1..n-1]):
   - `Address=+{FieldOffset}`, `GroupHeader=1`
   - If `IsPointerDeref` or `IsContainerView` → add `Offsets=[0]`
3. **Leaf fields** (current Fields list): `EmitFields()` handles per-type

### CleanBreadcrumbs (Cycle Removal)

Before XML generation, breadcrumbs are cleaned to remove navigation cycles:
```
[A, B, C, A, B] → detects A at [0] and [3] → removes [0..3] → [A, B]
```

### Variant Type → CE VariableType Mapping

| Godot Variant::Type | CE VariableType | Notes |
|---|---|---|
| BOOL | Byte | 0 or 1 |
| INT | 8 Bytes | Godot uses int64_t |
| FLOAT | Double | Godot uses double |
| STRING | String | UTF-8 (CE String type) |
| VECTOR2 | Group | 2 Float children (x, y) |
| VECTOR3 | Group | 3 Float children (x, y, z) |
| VECTOR4 | Group | 4 Float children (x, y, z, w) |
| COLOR | Group | 4 Float children (r, g, b, a) |
| OBJECT | Pointer | 8 bytes, deref to child entries |
| ARRAY | Group + elements | Deref data pointer |
| DICTIONARY | Group + pairs | Key-value expansion |
| STRING_NAME | String | Resolve interned string |
| NODE_PATH | String | Path string |
| RECT2 | Group | 4 Float children (x, y, w, h) |
| TRANSFORM2D | Group | 6 Float children |
| TRANSFORM3D | Group | 12 Float children (3x3 basis + origin) |
| QUATERNION | Group | 4 Float children (x, y, z, w) |
| AABB | Group | 6 Float children (pos + size) |

### Inline Struct Expansion

For composite types (Vector3, Color, Transform3D, etc.):
- Struct group: `Address=+{structOffset}`, NO Offsets (inline, not pointer)
- Children: `Address=+{child.Offset}` with Float/Double type

Example (Vector3):
```xml
<CheatEntry>
  <Description>position (Vector3)</Description>
  <Address>+0x120</Address>
  <GroupHeader>1</GroupHeader>
  <CheatEntries>
    <CheatEntry>
      <Description>x</Description>
      <Address>+0</Address>
      <VariableType>Float</VariableType>
    </CheatEntry>
    <CheatEntry>
      <Description>y</Description>
      <Address>+4</Address>
      <VariableType>Float</VariableType>
    </CheatEntry>
    <CheatEntry>
      <Description>z</Description>
      <Address>+8</Address>
      <VariableType>Float</VariableType>
    </CheatEntry>
  </CheatEntries>
</CheatEntry>
```

---

## CSX Export (CE Structure Dissect)

**Format**: CE Structure Dissect flat field list
**Service**: `CsxExportService.cs`

### Structure

```xml
<Structures>
  <Structure Name="CharacterBody3D" Size="1024">
    <Element Offset="0" Vartype="Pointer" Bytesize="8" Description="vtable" />
    <Element Offset="24" Vartype="8 Bytes" Bytesize="8" Description="instance_id" />
    <Element Offset="720" Vartype="Float" Bytesize="4" Description="position.x" />
    <Element Offset="724" Vartype="Float" Bytesize="4" Description="position.y" />
    <Element Offset="728" Vartype="Float" Bytesize="4" Description="position.z" />
    <Element Offset="756" Vartype="Double" Bytesize="8" Description="health" />
  </Structure>
</Structures>
```

### Features

- Pointer fields: native `Vartype="Pointer"`
- Optional drilldown: resolve Object pointers to nested structures
- Cycle detection to prevent infinite nesting
- Struct flattening with dot-notation (e.g., `position.x`)

---

## SDK Header Export (.h)

**Format**: C++ struct header
**Service**: `SdkExportService.cs`

### Example Output

```cpp
// Auto-generated by Zolt-Dump
// Godot 4.3.0 — CharacterBody3D

// Zoltraak — 栔爾朵列 — Structure definition for CharacterBody3D
struct CharacterBody3D : public PhysicsBody3D {
    // Offset 0x2D0
    Vector3 velocity;        // +0x2D0 (12 bytes)
    // Offset 0x2DC
    bool is_on_floor;        // +0x2DC (1 byte)
    uint8_t _pad_2DD[3];     // padding
    // Offset 0x2E0
    float floor_max_angle;   // +0x2E0 (4 bytes)
}; // Size: 0x300
```

### Features

- Inheritance chain preservation
- Field offsets, sizes, types
- Padding insertion for gaps
- Comment annotations with hex offsets
