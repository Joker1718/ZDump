# Godot Engine Internals — Memory Layout Reference

This document describes Godot 4.x internal data structures relevant to memory dumping.

Source reference: `D:\Github\godot` (Godot 4.7 dev)

---

## 1. ObjectDB — Global Object Registry

**Source**: `core/object/object.h`, `core/object/object.cpp`

### ObjectSlot (16 bytes)

```cpp
struct ObjectSlot {
    uint64_t validator : 39;      // OBJECTDB_VALIDATOR_BITS — prevents use-after-free
    uint64_t next_free : 24;      // OBJECTDB_SLOT_MAX_COUNT_BITS — free list chain
    uint64_t is_ref_counted : 1;  // 1 = RefCounted, 0 = regular Object
    Object *object = nullptr;     // 8 bytes — direct pointer to Object instance
};
// Total: 16 bytes per slot (8 metadata + 8 pointer)
```

### ObjectID (8 bytes)

```cpp
// 64-bit encoded identifier
// Bit 63:     is_ref_counted flag
// Bits 0-23:  slot index (max 16,777,216 objects)
// Bits 24-62: validator counter
```

### Global Variables

```cpp
static ObjectSlot *object_slots;  // Sparse array of all objects
static uint32_t slot_count;       // Current number of allocated slots
static uint32_t slot_max;         // Maximum slot index ever used
static SpinLock spin_lock;        // Thread safety
```

### Key Operations

- `ObjectDB::add_instance(Object*)` — Assigns slot, stores pointer
- `ObjectDB::remove_instance(Object*)` — Clears slot, adds to free list
- `ObjectDB::get_instance(ObjectID)` — Validates + returns Object*

---

## 2. ClassDB — Class Registration System

**Source**: `core/object/class_db.h`

### ClassInfo

```cpp
struct ClassInfo {
    APIType api;                                    // Core, Editor, Extension
    ClassInfo *inherits_ptr;                        // Parent class pointer
    void *class_ptr;                                // Static class data
    const GDType *gdtype;                           // Type metadata
    ObjectGDExtension *gdextension;                 // GDExtension info

    HashMap<StringName, MethodBind*> method_map;    // All methods
    AHashMap<StringName, int64_t> constant_map;     // Integer constants
    HashMap<StringName, EnumInfo> enum_map;          // Enum definitions
    AHashMap<StringName, MethodInfo> signal_map;     // Signal definitions
    List<PropertyInfo> property_list;                // Property list (ordered)
    HashMap<StringName, PropertyInfo> property_map;  // Property lookup
    AHashMap<StringName, PropertySetGet> property_setget; // Getter/setter bindings

    StringName inherits;                            // Parent class name
    StringName name;                                // This class name
    bool exposed;                                   // Visible to scripts
    bool reloadable;
    Object *(*creation_func)(bool) = nullptr;       // Factory function
};
```

### Global Variables

```cpp
static HashMap<StringName, ClassInfo> classes;          // ALL registered classes
static HashMap<StringName, StringName> compat_classes;  // Compatibility aliases
```

---

## 3. Object Base Class

**Source**: `core/object/object.h`

### Memory Layout (approximate, version-dependent)

```
Object instance layout:
+0x00  vtable pointer (8 bytes)
+0x08  ObjectGDExtension *_extension
+0x10  GDExtensionClassInstancePtr _extension_instance
+0x18  ObjectID _instance_id (8 bytes)
+0x20  Mutex *signal_mutex
+0x28  HashMap<StringName, SignalData> signal_map
+0x??  List<Connection> connections
+0x??  HashMap<StringName, Variant> metadata
+0x??  ScriptInstance *script_instance
+0x??  InstanceBinding *_instance_bindings
+0x??  uint32_t _instance_binding_count
+0x??  bitfield flags (ancestry, block_signals, emitting, etc.)
```

> **WARNING**: Exact offsets vary by Godot version and compiler. Must be validated dynamically.

---

## 4. Node — Scene Hierarchy

**Source**: `scene/main/node.h`

### Node::Data (internal struct)

```cpp
struct Data {
    String scene_file_path;
    Node *parent;                               // Parent node
    Node *owner;                                // Scene owner
    HashMap<StringName, Node*> children;         // Named children
    StringName name;                             // Node name
    SceneTree *tree;                             // Owning tree
    List<Node*>::Element *OW;                    // Owned element link
    List<Node*> owned;                           // Nodes owned by this
    Node *process_owner;
    ProcessThreadGroup process_thread_group;
    ProcessMode process_mode;
    bool physics_process;
    bool process;
    bool input;
    // ... more fields
};
```

---

## 5. SceneTree — Main Loop

**Source**: `scene/main/scene_tree.h`

```cpp
class SceneTree : public MainLoop {
    Window *root;                               // Root Window node
    Node *current_scene;                         // Active scene root
    HashMap<StringName, Group> group_map;         // Named groups
    List<Ref<SceneTreeTimer>> timers;
    List<Ref<Tween>> tweens;
    static SceneTree *singleton;                 // Global singleton pointer
};
```

**Entry point**: `SceneTree::singleton` → `.root` → children recursion

---

## 6. Variant Type System

**Source**: `core/variant/variant.h`

### Type Enum (38 types)

```cpp
enum Type {
    NIL = 0,
    BOOL, INT, FLOAT, STRING,                    // Primitives
    VECTOR2, VECTOR2I, RECT2, RECT2I,            // 2D types
    VECTOR3, VECTOR3I, TRANSFORM2D,              // 3D types
    VECTOR4, VECTOR4I,                           // 4D types
    PLANE, QUATERNION, AABB, BASIS,              // Geometry
    TRANSFORM3D, PROJECTION,                     // Transforms
    COLOR,                                       // Color
    STRING_NAME, NODE_PATH, RID,                 // Engine types
    OBJECT,                                      // Object reference
    CALLABLE, SIGNAL,                            // Function types
    DICTIONARY, ARRAY,                           // Containers
    PACKED_BYTE_ARRAY, PACKED_INT32_ARRAY,        // Packed arrays
    PACKED_INT64_ARRAY, PACKED_FLOAT32_ARRAY,
    PACKED_FLOAT64_ARRAY, PACKED_STRING_ARRAY,
    PACKED_VECTOR2_ARRAY, PACKED_VECTOR3_ARRAY,
    PACKED_COLOR_ARRAY, PACKED_VECTOR4_ARRAY,
    VARIANT_MAX
};
```

### Memory Layout

```
Variant (32-48 bytes, platform-dependent):
+0x00  Type type        (4 bytes, enum)
+0x04  (padding)        (4 bytes, x64 alignment)
+0x08  union _data      (24 or 40 bytes)
       ├── bool _bool
       ├── int64_t _int
       ├── double _float
       ├── Vector2 (2 floats, 8 bytes inline)
       ├── Vector3 (3 floats, 12 bytes inline)
       ├── Vector4 (4 floats, 16 bytes inline)
       ├── Color (4 floats, 16 bytes inline)
       ├── Transform2D* (heap pointer)
       ├── Basis* (heap pointer)
       ├── Transform3D* (heap pointer)
       ├── Projection* (heap pointer)
       ├── ObjData { ObjectID id; Object* obj; } (16 bytes inline)
       ├── PackedArrayRefBase* (ref-counted array pointer)
       └── void* _ptr (generic pointer for String, StringName, etc.)
```

### ObjData (for OBJECT type)

```cpp
struct ObjData {
    ObjectID id;    // 8 bytes — for safe validation
    Object *obj;    // 8 bytes — direct pointer
};
```

---

## 7. StringName — Interned Strings

**Source**: `core/string/string_name.h`

```cpp
class StringName {
    struct _Data {
        SafeRefCount refcount;        // Atomic ref count
        String name;                  // Actual string content
        uint32_t hash;                // Cached hash value
        _Data *prev, *next;           // Hash bucket linked list
    };
    _Data *_data;                     // Pointer to interned data
};
```

**Resolution**: Read `_data` pointer → dereference → read `name` field (String)

**String (CoW)**: `core/string/ustring.h`
```cpp
class String {
    CowData<char32_t> _cowdata;  // Copy-on-write UTF-32 buffer
    // _cowdata contains: pointer to { refcount, size, char32_t[] }
};
```

---

## 8. PropertyInfo

**Source**: `core/object/object.h`

```cpp
struct PropertyInfo {
    Variant::Type type;           // Property value type
    String name;                  // Property name
    StringName class_name;        // For OBJECT type: expected class
    PropertyHint hint;            // Editor hint (Range, Enum, File, etc.)
    String hint_string;           // Hint parameters
    uint32_t usage;               // PROPERTY_USAGE_* flags
};
```

### PropertySetGet (getter/setter binding)

```cpp
struct PropertySetGet {
    int index;
    StringName setter;            // Setter method name
    StringName getter;            // Getter method name
    MethodBind *_setptr;          // Direct setter pointer
    MethodBind *_getptr;          // Direct getter pointer
    Variant::Type type;
};
```

---

## 9. MethodBind

**Source**: `core/object/method_bind.h`

```cpp
class MethodBind {
    StringName name;              // Method name
    StringName instance_class;    // Owning class
    int argument_count;           // Number of arguments
    Variant::Type *argument_types; // Type array for arguments
    Vector<Variant> default_arguments;
    bool _const;                  // Is const method
    bool _static;                 // Is static method
    bool _returns;                // Has return value
    Variant::Type return_type;
};
```

---

## 10. Engine Singletons

**Source**: `core/config/engine.h`

```cpp
class Engine {
    struct Singleton {
        StringName name;
        Object *ptr;
        StringName class_name;
        bool user_created;
        bool editor_only;
    };

    List<Singleton> singletons;
    HashMap<StringName, Object*> singleton_ptrs;
    static Engine *singleton_ptr;
};
```

Common singletons: `OS`, `Input`, `ResourceLoader`, `SceneTree`, `Engine`, `ProjectSettings`

---

## 11. RefCounted

**Source**: `core/object/ref_counted.h`

```cpp
class RefCounted : public Object {
    SafeRefCount refcount;        // Reference count
    SafeRefCount refcount_init;   // Initial reference count
};
```

ObjectID bit 63 = 1 indicates RefCounted object.

---

## 12. GDExtension Interface

**Source**: `core/extension/gdextension.h`

```cpp
struct ObjectGDExtension {
    GDExtension *library;
    StringName class_name;
    StringName parent_class_name;
    bool editor_class, is_virtual, is_abstract;

    // Function pointers for property access
    GDExtensionClassSet set;
    GDExtensionClassGet get;
    GDExtensionClassGetPropertyList get_property_list;
    GDExtensionClassNotification2 notification2;
    GDExtensionClassCreateInstance2 create_instance2;
    GDExtensionClassFreeInstance free_instance;
};
```

---

## Key Symbols for AOB Scanning

| Symbol | Mangled (MSVC) | Purpose |
|--------|---------------|---------|
| ObjectDB::object_slots | `?object_slots@ObjectDB@@` | Object array pointer |
| ObjectDB::slot_max | `?slot_max@ObjectDB@@` | Max slot index |
| ClassDB::classes | `?classes@ClassDB@@` | Class registry |
| SceneTree::singleton | `?singleton@SceneTree@@` | Scene tree pointer |
| Engine::singleton_ptr | `?singleton_ptr@Engine@@` | Engine singleton |

> **Note**: Release builds may strip symbols. AOB pattern fallbacks required.
