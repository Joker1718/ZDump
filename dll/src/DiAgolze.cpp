// DiAgolze — 黃金化 (Mahat's Gold — ClassDB Scanner)
// Scans the Godot ClassDB registry to extract class metadata.
//
// ClassDB is a HashMap<StringName, ClassInfo> with layout:
//   [PagedAllocator (CLASSDB_ALLOC_OFFSET bytes)]
//   +off+0x00: HashMapElement** elements     (bucket array)
//   +off+0x08: uint32_t*       hashes        (hash values array)
//   +off+0x10: HashMapElement*  head_element  (linked list head)
//   +off+0x18: HashMapElement*  tail_element  (linked list tail)
//   +off+0x20: uint32_t         capacity_index
//   +off+0x24: uint32_t         num_elements
//
// HashMapElement layout:
//   +0x00: HashMapElement* next
//   +0x08: HashMapElement* prev
//   +0x10: KeyValue { StringName key (8B), ClassInfo value }
//
// StringName is 8 bytes (pointer to _Data), resolved via Flamme.

#include "DiAgolze.h"
#include "Zoltraak.h"
#include "Flamme.h"
#include "Solliter.h"
#include "Grimoire.h"

#include <atomic>
#include <unordered_map>

namespace Zolt::DiAgolze {

// Cached state
static uint64_t s_classDBAddr   = 0;
static int      s_allocOffset   = 0;
static uint64_t s_headElement   = 0;
static uint64_t s_tailElement   = 0;
static uint32_t s_numElements   = 0;
static uint32_t s_capacityIndex = 0;

// Class name → (HashMapElement address, ClassInfo address) cache
struct ClassEntry {
    uint64_t    elementAddr;  // HashMapElement*
    uint64_t    infoAddr;     // ClassInfo* (value within element)
    std::string parentName;
};
static std::unordered_map<std::string, ClassEntry> s_classMap;

// vtable → class name cache for fast object class lookup
static std::unordered_map<uint64_t, std::string> s_vtableMap;

// Module range for vtable validation
static uint64_t s_moduleBase = 0;
static uint64_t s_moduleSize = 0;

static void EnsureModuleInfo() {
    if (!s_moduleBase) {
        s_moduleBase = Zoltraak::GetModuleBase();
        s_moduleSize = Zoltraak::GetModuleSize();
    }
}

// Read HashMap core fields from the ClassDB address
static bool ReadHashMapFields() {
    if (!s_classDBAddr) return false;

    int off = s_allocOffset;
    uint64_t elements = Zoltraak::ReadSafe<uint64_t>(s_classDBAddr + off, 0);
    uint64_t hashes   = Zoltraak::ReadSafe<uint64_t>(s_classDBAddr + off + 0x08, 0);
    s_headElement     = Zoltraak::ReadSafe<uint64_t>(s_classDBAddr + off + 0x10, 0);
    s_tailElement     = Zoltraak::ReadSafe<uint64_t>(s_classDBAddr + off + 0x18, 0);
    s_capacityIndex   = Zoltraak::ReadSafe<uint32_t>(s_classDBAddr + off + 0x20, 0);
    s_numElements     = Zoltraak::ReadSafe<uint32_t>(s_classDBAddr + off + 0x24, 0);

    if (!elements || !hashes || !s_headElement || !s_tailElement) {
        Solliter::Warn("CLSDB", "HashMap fields invalid (head=%p tail=%p num=%u)",
            reinterpret_cast<void*>(s_headElement),
            reinterpret_cast<void*>(s_tailElement),
            s_numElements);
        return false;
    }

    Solliter::Info("CLSDB", "HashMap: head=%p tail=%p capIdx=%u num=%u allocOff=%d",
        reinterpret_cast<void*>(s_headElement),
        reinterpret_cast<void*>(s_tailElement),
        s_capacityIndex, s_numElements, s_allocOffset);

    return true;
}

// Walk the linked list to build s_classMap
static void BuildClassCache() {
    s_classMap.clear();
    if (!s_headElement) return;

    uint64_t current = s_headElement;
    int count = 0;
    int maxIter = static_cast<int>(s_numElements) + 100; // Safety limit
    int emptyNames = 0;

    while (current && count < maxIter) {
        if (!Zoltraak::IsReadable(current)) {
            Solliter::Warn("CLSDB", "Linked list element at %p not readable, stopping",
                reinterpret_cast<void*>(current));
            break;
        }

        // HashMapElement layout:
        //   +0x00: next (8B)
        //   +0x08: prev (8B)
        //   +0x10: KeyValue { StringName key (8B), ClassInfo value }
        uint64_t keyAddr = current + 0x10;       // StringName (8 bytes = _Data* pointer)
        uint64_t valueAddr = current + 0x10 + 8; // ClassInfo starts after StringName key

        // Resolve the class name from the StringName key
        std::string className = Flamme::Resolve(keyAddr);

        // Diagnostic: log first 3 elements if name is empty
        if (className.empty() && emptyNames < 3) {
            uint64_t dataPtr = Zoltraak::ReadSafe<uint64_t>(keyAddr, 0);
            Solliter::Debug("CLSDB", "  elem[%d] at %p: _Data*=%p",
                count, reinterpret_cast<void*>(current), reinterpret_cast<void*>(dataPtr));

            // Hex dump the ELEMENT itself to verify layout
            {
                uint8_t ebuf[64] = {};
                Zoltraak::ReadSafe(current, ebuf, sizeof(ebuf));
                char line[200];
                Solliter::Debug("CLSDB", "  Element hex dump:");
                for (int i = 0; i < 64; i += 16) {
                    snprintf(line, sizeof(line),
                        "    elem+0x%02X: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X",
                        i, ebuf[i+0], ebuf[i+1], ebuf[i+2], ebuf[i+3],
                        ebuf[i+4], ebuf[i+5], ebuf[i+6], ebuf[i+7],
                        ebuf[i+8], ebuf[i+9], ebuf[i+10], ebuf[i+11],
                        ebuf[i+12], ebuf[i+13], ebuf[i+14], ebuf[i+15]);
                    Solliter::Debug("CLSDB", "%s", line);
                }
            }

            if (dataPtr && Zoltraak::IsReadable(dataPtr)) {
                uint8_t buf[48] = {};
                Zoltraak::ReadSafe(dataPtr, buf, sizeof(buf));
                char line[200];
                Solliter::Debug("CLSDB", "  _Data hex dump:");
                for (int i = 0; i < 48; i += 16) {
                    snprintf(line, sizeof(line),
                        "    +0x%02X: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X",
                        i, buf[i+0], buf[i+1], buf[i+2], buf[i+3],
                        buf[i+4], buf[i+5], buf[i+6], buf[i+7],
                        buf[i+8], buf[i+9], buf[i+10], buf[i+11],
                        buf[i+12], buf[i+13], buf[i+14], buf[i+15]);
                    Solliter::Debug("CLSDB", "%s", line);
                }
                // Try reading string from _Data+0x00 directly as char32_t
                std::string direct = Zoltraak::ReadChar32String(dataPtr, 64);
                Solliter::Debug("CLSDB", "  direct char32 @_Data+0: '%s'", direct.c_str());
            }

            // Also try alternate key offsets: +0x18, +0x20 (in case element has hash field)
            for (int altOff : {0x18, 0x20}) {
                uint64_t altKey = current + altOff;
                std::string altName = Flamme::Resolve(altKey);
                if (!altName.empty()) {
                    Solliter::Debug("CLSDB", "  alt key at elem+0x%X: '%s'", altOff, altName.c_str());
                }
            }

            emptyNames++;
        }

        if (!className.empty()) {
            ClassEntry entry;
            entry.elementAddr = current;
            entry.infoAddr    = valueAddr;
            entry.parentName  = ""; // Will be resolved on demand

            s_classMap[className] = entry;
        }

        // Move to next element
        uint64_t next = Zoltraak::ReadSafe<uint64_t>(current, 0);
        if (next == current) break; // Self-loop protection
        current = next;
        count++;
    }

    Solliter::Info("CLSDB", "Built class cache: %d classes found (%d expected, %d empty names)",
        static_cast<int>(s_classMap.size()), static_cast<int>(s_numElements), emptyNames);
}

// Try to resolve the parent class name from a ClassInfo at infoAddr.
// ClassInfo layout (partial): the inherits_ptr (ClassInfo*) is typically
// one of the first fields. We probe nearby for a pointer that leads to
// another ClassInfo we've already cached.
static std::string ResolveParentName(uint64_t infoAddr) {
    if (!infoAddr) return "";

    // ClassInfo has many fields. The inherits_ptr is typically early:
    // Try reading pointers at various offsets to find one that points
    // to another ClassInfo we recognize.
    //
    // ClassInfo layout (from godot-internals.md):
    //   APIType api;                       // 4B enum + padding = 8B
    //   ClassInfo* inherits_ptr;           // 8B — OFFSET 0x08
    //   void* class_ptr;                   // 8B
    //   const GDType* gdtype;              // 8B
    //   ObjectGDExtension* gdextension;    // 8B
    //   HashMap method_map;                // ~big
    //   ...
    //   StringName inherits;              // parent name as StringName
    //   StringName name;                  // class name as StringName
    //
    // The inherits_ptr at offset 0x08 is the most reliable.
    // But we can also look for the 'inherits' StringName field.

    // Strategy 1: Read ClassInfo* inherits_ptr at +0x08
    static const int PARENT_PTR_OFFSETS[] = { 0x08, 0x10, 0x00, 0x18 };

    for (int off : PARENT_PTR_OFFSETS) {
        uint64_t parentInfoPtr = Zoltraak::ReadSafe<uint64_t>(infoAddr + off, 0);
        if (!parentInfoPtr || !Zoltraak::IsReadable(parentInfoPtr)) continue;

        // Check if this parentInfoPtr matches any known ClassInfo address
        for (auto& [name, entry] : s_classMap) {
            if (entry.infoAddr == parentInfoPtr) {
                return name;
            }
        }
    }

    // Strategy 2: Scan for StringName fields that contain 'inherits' and 'name'
    // These are typically near the end of ClassInfo struct, but offset varies.
    // Try reading StringName fields at various offsets (expensive, do sparingly)
    // Skip for now — parent resolution via pointer is more reliable.

    return "";
}

void Init(uint64_t classDBAddr) {
    s_classDBAddr = classDBAddr;
    s_allocOffset = DynOff::CLASSDB_ALLOC_OFFSET;
    s_classMap.clear();
    s_vtableMap.clear();
    EnsureModuleInfo();

    if (!classDBAddr) {
        Solliter::Warn("CLSDB", "Init called with null address");
        return;
    }

    // Read HashMap fields
    if (!ReadHashMapFields()) {
        Solliter::Warn("CLSDB", "Failed to read ClassDB HashMap fields");
        return;
    }

    // Build the class cache by walking the linked list
    BuildClassCache();

    // Now resolve parent names for each class (second pass)
    int resolvedParents = 0;
    for (auto& [name, entry] : s_classMap) {
        entry.parentName = ResolveParentName(entry.infoAddr);
        if (!entry.parentName.empty()) resolvedParents++;
    }

    Solliter::Info("CLSDB", "Init complete: %d classes, %d parent links resolved",
        static_cast<int>(s_classMap.size()), resolvedParents);
}

int GetClassCount() {
    return static_cast<int>(s_classMap.size());
}

void EnumerateClasses(const std::function<void(const std::string& name, uint64_t infoAddr)>& callback) {
    // Walk the linked list in order for deterministic output
    if (!s_headElement) return;

    uint64_t current = s_headElement;
    int maxIter = static_cast<int>(s_numElements) + 100;
    int count = 0;

    while (current && count < maxIter) {
        if (!Zoltraak::IsReadable(current)) break;

        uint64_t keyAddr = current + 0x10;
        uint64_t valueAddr = current + 0x10 + 8;

        std::string className = Flamme::Resolve(keyAddr);
        if (!className.empty()) {
            callback(className, valueAddr);
        }

        uint64_t next = Zoltraak::ReadSafe<uint64_t>(current, 0);
        if (next == current) break;
        current = next;
        count++;
    }
}

ClassInfo GetClassInfo(const char* className) {
    ClassInfo info;
    if (!className) return info;

    auto it = s_classMap.find(className);
    if (it == s_classMap.end()) return info;

    info.name       = className;
    info.parentName  = it->second.parentName;
    info.nativePtr   = it->second.infoAddr;

    // Properties and methods are resolved on demand via GetPropertyList/GetMethodList
    return info;
}

std::vector<PropertyInfo> GetPropertyList(const char* className) {
    // TODO: Walk ClassInfo::property_list linked list
    // This requires knowing the exact offset of property_list within ClassInfo,
    // which varies by Godot version. Will be implemented when we can probe it.
    (void)className;
    Solliter::Debug("CLSDB", "GetPropertyList: not yet implemented (needs ClassInfo layout probing)");
    return {};
}

std::vector<MethodInfo> GetMethodList(const char* className) {
    // TODO: Walk ClassInfo::method_map HashMap
    // Similar to GetPropertyList, needs ClassInfo layout probing.
    (void)className;
    Solliter::Debug("CLSDB", "GetMethodList: not yet implemented (needs ClassInfo layout probing)");
    return {};
}

std::string GetObjectClass(uint64_t objectAddr) {
    if (!objectAddr) return "";
    EnsureModuleInfo();

    // Read vtable from object
    uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(objectAddr, 0);
    if (!vtable || vtable < s_moduleBase || vtable >= s_moduleBase + s_moduleSize) {
        return "";
    }

    // Check vtable cache first
    auto it = s_vtableMap.find(vtable);
    if (it != s_vtableMap.end()) {
        return it->second;
    }

    // vtable not cached — we need a different approach
    // Since Godot objects don't store their class name directly,
    // we'd need to match the vtable against known class vtables.
    // Build vtable map lazily from ObjectDB objects.
    //
    // For now, return empty and let callers handle it.
    // The vtable map gets populated as objects are enumerated.
    return "";
}

void LogDiagnostics() {
    static std::atomic<bool> s_done{false};
    if (s_done.exchange(true)) return; // One-shot

    Solliter::Info("CLSDB", "=== DiAgolze Diagnostics ===");
    Solliter::Info("CLSDB", "ClassDB addr:   %p", reinterpret_cast<void*>(s_classDBAddr));
    Solliter::Info("CLSDB", "Alloc offset:   %d (0x%X)", s_allocOffset, s_allocOffset);
    Solliter::Info("CLSDB", "HashMap:        head=%p tail=%p capIdx=%u num=%u",
        reinterpret_cast<void*>(s_headElement),
        reinterpret_cast<void*>(s_tailElement),
        s_capacityIndex, s_numElements);
    Solliter::Info("CLSDB", "Classes cached: %d", static_cast<int>(s_classMap.size()));

    // Show first 20 class names with their parent
    int shown = 0;
    EnumerateClasses([&](const std::string& name, uint64_t infoAddr) {
        if (shown >= 20) return;

        auto it = s_classMap.find(name);
        std::string parent = (it != s_classMap.end()) ? it->second.parentName : "?";

        Solliter::Info("CLSDB", "  [%d] %s : %s (info=%p)",
            shown, name.c_str(), parent.c_str(),
            reinterpret_cast<void*>(infoAddr));
        shown++;
    });

    // Check for known expected classes
    static const char* EXPECTED[] = { "Node", "Node2D", "Node3D", "Sprite2D", "Object",
                                       "RefCounted", "Resource", "Control", "Label" };
    int foundExpected = 0;
    for (const char* name : EXPECTED) {
        if (s_classMap.count(name)) foundExpected++;
    }
    Solliter::Info("CLSDB", "Known class check: %d/%d found",
        foundExpected, static_cast<int>(sizeof(EXPECTED) / sizeof(EXPECTED[0])));
}

} // namespace Zolt::DiAgolze
