# Pipe Protocol — JSON IPC Specification

Named pipe: `\\.\pipe\ZoltDumpBfx`
Format: JSON, newline-delimited (one message per `\n`)
Direction: bidirectional — Request/Response + async push Events

-----

## General Rules

- Every request carries an `"id"` (integer, caller-assigned).
- Every response echoes the same `"id"` and includes `"ok": true|false`.
- On failure: `"ok": false, "error": "message"`.
- On partial success: `"ok": true, "error": "message"` — check for `"error"` even when `ok` is true.
- All addresses are hex strings with no prefix (e.g. `"7FF600A12340"`) unless noted.
- Pagination advances by `"scanned"` (indices iterated), **not** by `objects.length` — null slots are skipped but still counted.

-----

## Commands (UI → DLL)

### Initialization & Info

```jsonc
// Initialize — returns Godot version; DLL runs AOB scans internally on startup
{ "id": 1, "cmd": "init" }

// Get global pointer addresses
{ "id": 2, "cmd": "get_pointers" }

// Get total object count
{ "id": 3, "cmd": "get_object_count" }

// Get dynamically-detected DynOff values (diagnostics)
{ "id": 4, "cmd": "get_offsets" }
```

### Object Enumeration

```jsonc
// Paginated object list — advance by "scanned", not objects.length
{ "id": 5, "cmd": "get_object_list", "offset": 0, "limit": 200 }

// Single object detail
{ "id": 6, "cmd": "get_object", "addr": "7FF123456789" }

// Find object by node path (e.g. "/root/World3D/Player")
{ "id": 7, "cmd": "find_object", "path": "/root/World3D/Player" }

// Reverse address lookup: given any address, find the containing Object
// For .NET managed addresses, L2 scan returns all Godot objects that reference
// the same managed child (alternatives array in response)
{ "id": 8, "cmd": "find_by_address", "addr": "7FF123456789" }
```

### Class & Instance Walking

```jsonc
// Walk all properties of a class from ClassDB (static schema)
{ "id": 9, "cmd": "walk_class", "class_name": "CharacterBody3D" }

// Walk live property values of an Object instance
{ "id": 10, "cmd": "walk_instance", "addr": "7FF123456789" }
{ "id": 10, "cmd": "walk_instance", "addr": "7FF123456789", "preview_limit": 2 }

// Walk SceneTree root → children hierarchy
{ "id": 11, "cmd": "walk_tree" }
{ "id": 11, "cmd": "walk_tree", "addr": "7FF123456789", "depth": 3 }

// Find all instances of a class by name
{ "id": 12, "cmd": "find_instances", "class_name": "CharacterBody3D", "limit": 100 }
```

### Memory Access

```jsonc
// Read raw bytes from memory
{ "id": 13, "cmd": "read_mem", "addr": "7FF123456789", "size": 64 }

// Write raw bytes to memory
{ "id": 14, "cmd": "write_mem", "addr": "7FF123456789", "hex": "0000C842" }
```

### Watch (Live Monitoring)

```jsonc
// Subscribe to value changes
{ "id": 15, "cmd": "watch", "addr": "7FF123456789", "size": 4, "interval_ms": 100 }

// Unsubscribe
{ "id": 16, "cmd": "unwatch", "addr": "7FF123456789" }
```

### CE Integration

```jsonc
// Get CE pointer chain info for XML export
{ "id": 17, "cmd": "get_ce_pointer_info", "addr": "7FF123456789", "breadcrumbs": [...] }
```

### .NET / Managed Object Support

```jsonc
// Trigger full scan (re-runs .NET detection + CSharpInstance layout probe)
{ "id": 18, "cmd": "trigger_scan" }

// Get managed object info via GCHandle chain
{ "id": 19, "cmd": "get_managed_object", "addr": "7FF123456789" }
```

-----

## Responses

### init

```jsonc
{
  "id": 1, "ok": true,
  "godot_version": "4.3.0",
  "godot_version_major": 4,
  "godot_version_minor": 3,
  "module_name": "godot.windows.opt.tools.x86_64.exe",
  "module_base": "7FF600000000",
  "pid": 12345
}
```

### get_pointers

```jsonc
{
  "id": 2, "ok": true,
  "object_db": "7FF600A12340",
  "object_db_slot_max": 8192,
  "class_db": "7FF600B56780",
  "scene_tree": "7FF600C89ABC",
  "engine": "7FF600D00000",
  "scan_stats": {
    "object_db_method": "symbol",
    "class_db_method": "aob",
    "scene_tree_method": "symbol",
    "patterns_tried": 12,
    "patterns_hit": 3
  }
}
```

### get_object_list

```jsonc
{
  "id": 5, "ok": true,
  "total": 4096,
  "scanned": 200,
  "objects": [
    {
      "addr": "7FF6AA000000",
      "name": "Player",
      "class_name": "CharacterBody3D",
      "parent_addr": "7FF6AA100000",
      "parent_name": "World3D",
      "object_id": "123456789",
      "is_ref_counted": false,
      "slot_index": 42
    }
  ]
}
```

### walk_instance

```jsonc
{
  "id": 10, "ok": true,
  "addr": "7FF6AA000000",
  "name": "Player",
  "class_name": "CharacterBody3D",
  "fields": [
    {
      "name": "position",
      "type": "Vector3",
      "variant_type": 10,
      "offset": 720,
      "size": 12,
      "hex": "0000803F0000003F0000C03F",
      "value": "(1.0, 0.5, 1.5)"
    },
    {
      "name": "health",
      "type": "float",
      "variant_type": 3,
      "offset": 756,
      "size": 8,
      "hex": "0300000000000000000059400000000000000000",
      "value": "100.0"
    },
    {
      "name": "inventory",
      "type": "Array",
      "variant_type": 28,
      "offset": 800,
      "count": 5,
      "elements": [
        { "i": 0, "type": "String", "v": "Sword" },
        { "i": 1, "type": "String", "v": "Shield" }
      ]
    }
  ]
}
```

### walk_container

Drill into Array/Dictionary/PackedArray elements with pagination.

**Request**:
```jsonc
{ "cmd": "walk_container", "id": 12,
  "address": "1E2D4C23170",  // Variant data address (from walk_instance field)
  "variant_type": 28,         // Raw Variant::Type (28=Array, 27=Dictionary, 29-38=Packed)
  "offset": 0,                // Pagination start (0-based)
  "limit": 100                // Max elements to return (capped at 1000)
}
```

**Response**:
```jsonc
{
  "id": 12, "ok": true,
  "address": "1E2D4C23170",
  "container_type": "Array",
  "element_count": 5,
  "element_type": "",
  "key_type": "",
  "fields": [
    {
      "name": "[0]",
      "type": "Object",
      "variant_type": 24,
      "value": "Object@1E2D4C50000 (id=42)",
      "address": "1E2D4C40010",
      "offset": -1,
      "is_pointer": true,
      "is_container": false,
      "ptr_value": "1E2D4C50000",
      "ptr_class": "Weapon"
    },
    {
      "name": "[1]",
      "type": "String",
      "variant_type": 4,
      "value": "\"Iron Sword\"",
      "address": "1E2D4C40028",
      "offset": -1,
      "is_pointer": false,
      "is_container": false
    }
  ]
}
```

### walk_tree

```jsonc
{
  "id": 11, "ok": true,
  "root": {
    "addr": "7FF6AA000000",
    "name": "root",
    "class_name": "Window",
    "children": [
      {
        "addr": "7FF6AA100000",
        "name": "World3D",
        "class_name": "Node3D",
        "child_count": 5,
        "children": [
          {
            "addr": "7FF6AA200000",
            "name": "Player",
            "class_name": "CharacterBody3D",
            "child_count": 3
          }
        ]
      }
    ]
  }
}
```

-----

## Push Events (DLL → UI)

### watch event

```jsonc
{
  "event": "watch",
  "addr": "7FF123456789",
  "hex": "0000C842",
  "value": "100.0",
  "timestamp": 1234567890
}
```

-----

## Error Handling

```jsonc
// Command not found
{ "id": 99, "ok": false, "error": "Unknown command: foobar" }

// Address unreadable
{ "id": 10, "ok": false, "error": "Failed to read memory at 7FF000000000" }

// Not initialized
{ "id": 5, "ok": false, "error": "DLL not initialized. Send 'init' first." }
```
