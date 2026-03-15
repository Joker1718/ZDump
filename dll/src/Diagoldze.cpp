// Diagoldze — 黃金化 (Mahat's Gold — ClassDB Scanner)
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

#include "Diagoldze.h"
#include "Zoltraak.h"
#include "Flamme.h"
#include "Solitaer.h"
#include "Grimoire.h"

#include <atomic>
#include <unordered_map>

namespace Zolt::Diagoldze {

// Cached state (4.x robin-hood HashMap)
static uint64_t s_classDBAddr   = 0;
static int      s_allocOffset   = 0;
static uint64_t s_headElement   = 0;
static uint64_t s_tailElement   = 0;
static uint32_t s_numElements   = 0;
static uint32_t s_capacityIndex = 0;

// 3.x bucket-chain HashMap state
static bool     s_is3x          = false;
static uint64_t s_hashTable3x   = 0;  // Element** hash_table
static uint8_t  s_power3x       = 0;  // hash_table_power
static uint32_t s_elements3x    = 0;  // element count

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
        Solitaer::Warn("CLSDB", "HashMap fields invalid (head=%p tail=%p num=%u)",
            reinterpret_cast<void*>(s_headElement),
            reinterpret_cast<void*>(s_tailElement),
            s_numElements);
        return false;
    }

    Solitaer::Info("CLSDB", "HashMap: head=%p tail=%p capIdx=%u num=%u allocOff=%d",
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
            Solitaer::Warn("CLSDB", "Linked list element at %p not readable, stopping",
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
            Solitaer::Debug("CLSDB", "  elem[%d] at %p: _Data*=%p",
                count, reinterpret_cast<void*>(current), reinterpret_cast<void*>(dataPtr));

            // Hex dump the ELEMENT itself to verify layout
            {
                uint8_t ebuf[64] = {};
                Zoltraak::ReadSafe(current, ebuf, sizeof(ebuf));
                char line[200];
                Solitaer::Debug("CLSDB", "  Element hex dump:");
                for (int i = 0; i < 64; i += 16) {
                    snprintf(line, sizeof(line),
                        "    elem+0x%02X: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X",
                        i, ebuf[i+0], ebuf[i+1], ebuf[i+2], ebuf[i+3],
                        ebuf[i+4], ebuf[i+5], ebuf[i+6], ebuf[i+7],
                        ebuf[i+8], ebuf[i+9], ebuf[i+10], ebuf[i+11],
                        ebuf[i+12], ebuf[i+13], ebuf[i+14], ebuf[i+15]);
                    Solitaer::Debug("CLSDB", "%s", line);
                }
            }

            if (dataPtr && Zoltraak::IsReadable(dataPtr)) {
                uint8_t buf[48] = {};
                Zoltraak::ReadSafe(dataPtr, buf, sizeof(buf));
                char line[200];
                Solitaer::Debug("CLSDB", "  _Data hex dump:");
                for (int i = 0; i < 48; i += 16) {
                    snprintf(line, sizeof(line),
                        "    +0x%02X: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X",
                        i, buf[i+0], buf[i+1], buf[i+2], buf[i+3],
                        buf[i+4], buf[i+5], buf[i+6], buf[i+7],
                        buf[i+8], buf[i+9], buf[i+10], buf[i+11],
                        buf[i+12], buf[i+13], buf[i+14], buf[i+15]);
                    Solitaer::Debug("CLSDB", "%s", line);
                }
                // Try reading string from _Data+0x00 directly as char32_t
                std::string direct = Zoltraak::ReadChar32String(dataPtr, 64);
                Solitaer::Debug("CLSDB", "  direct char32 @_Data+0: '%s'", direct.c_str());
            }

            // Also try alternate key offsets: +0x18, +0x20 (in case element has hash field)
            for (int altOff : {0x18, 0x20}) {
                uint64_t altKey = current + altOff;
                std::string altName = Flamme::Resolve(altKey);
                if (!altName.empty()) {
                    Solitaer::Debug("CLSDB", "  alt key at elem+0x%X: '%s'", altOff, altName.c_str());
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

    Solitaer::Info("CLSDB", "Built class cache: %d classes found (%d expected, %d empty names)",
        static_cast<int>(s_classMap.size()), static_cast<int>(s_numElements), emptyNames);
}

// ============================================================
// 3.x HashMap support (bucket-chain, separate chaining)
// ============================================================

// Read 3.x HashMap<StringName, ClassInfo> fields from the classDB address
static bool ReadHashMapFields3x() {
    if (!s_classDBAddr) return false;

    s_hashTable3x = Zoltraak::ReadSafe<uint64_t>(
        s_classDBAddr + DynOff::HASHMAP_3X_HASHTABLE_OFF, 0);
    s_power3x     = Zoltraak::ReadSafe<uint8_t>(
        s_classDBAddr + DynOff::HASHMAP_3X_POWER_OFF, 0);
    s_elements3x  = Zoltraak::ReadSafe<uint32_t>(
        s_classDBAddr + DynOff::HASHMAP_3X_ELEMENTS_OFF, 0);

    if (!s_hashTable3x || !Zoltraak::IsReadable(s_hashTable3x)) {
        Solitaer::Warn("CLSDB", "3x HashMap: invalid hash_table pointer");
        return false;
    }

    if (s_power3x < 4 || s_power3x > 20) {
        Solitaer::Warn("CLSDB", "3x HashMap: invalid power %u", s_power3x);
        return false;
    }

    Solitaer::Info("CLSDB", "3x HashMap: hashTable=%p power=%u elements=%u",
        reinterpret_cast<void*>(s_hashTable3x), s_power3x, s_elements3x);
    return true;
}

// Build the class cache by iterating 3.x bucket-chain HashMap
static void BuildClassCache3x() {
    s_classMap.clear();
    if (!s_hashTable3x) return;

    int bucketCount = 1 << s_power3x;
    int totalFound = 0;
    int emptyNames = 0;
    int chainBreaks = 0;  // Chains that stopped due to non-readable next pointer
    int occupiedBuckets = 0;
    int maxChainLen = 0;

    // Diagnostic: auto-detect element layout by testing the first few elements
    // Try two layouts: { hash(8), next(8), key(8), value } vs { next(8), hash(8), key(8), value }
    if (bucketCount > 0) {
        // Find the first non-null bucket entry for layout probing
        for (int b = 0; b < bucketCount; ++b) {
            uint64_t elem = Zoltraak::ReadSafe<uint64_t>(s_hashTable3x + b * 8, 0);
            if (!elem || !Zoltraak::IsReadable(elem)) continue;

            // Hex dump the first element for diagnostics
            uint8_t ebuf[64] = {};
            Zoltraak::ReadSafe(elem, ebuf, sizeof(ebuf));
            Solitaer::Info("CLSDB", "3x Element[0] layout probe at %p:",
                reinterpret_cast<void*>(elem));
            for (int i = 0; i < 64; i += 16) {
                char line[200];
                snprintf(line, sizeof(line),
                    "  +0x%02X: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X",
                    i, ebuf[i+0], ebuf[i+1], ebuf[i+2], ebuf[i+3],
                    ebuf[i+4], ebuf[i+5], ebuf[i+6], ebuf[i+7],
                    ebuf[i+8], ebuf[i+9], ebuf[i+10], ebuf[i+11],
                    ebuf[i+12], ebuf[i+13], ebuf[i+14], ebuf[i+15]);
                Solitaer::Info("CLSDB", "%s", line);
            }

            // Try resolving StringName at multiple offsets to identify key position
            for (int tryOff : {0x08, 0x10, 0x18, 0x20}) {
                std::string name = Flamme::Resolve(elem + tryOff);
                if (!name.empty()) {
                    Solitaer::Info("CLSDB", "  StringName at elem+0x%X: '%s'", tryOff, name.c_str());
                }
            }
            break;
        }
    }

    for (int b = 0; b < bucketCount; ++b) {
        uint64_t elem = Zoltraak::ReadSafe<uint64_t>(s_hashTable3x + b * 8, 0);
        if (!elem) continue;
        occupiedBuckets++;

        int chainLen = 0;
        while (elem) {
            if (!Zoltraak::IsReadable(elem)) { chainBreaks++; break; }

            // 3.x Element: hash(4B)+pad(4B)+next(8B)+key(8B StringName)+value(ClassInfo...)
            uint64_t keyAddr   = elem + DynOff::ELEM_3X_KEY_OFF;   // StringName (8B = _Data*)
            uint64_t valueAddr = elem + DynOff::ELEM_3X_VALUE_OFF; // ClassInfo starts here

            std::string className = Flamme::Resolve(keyAddr);

            if (className.empty()) {
                emptyNames++;
                // Diagnostic: log first 3 empty names
                if (emptyNames <= 3) {
                    uint64_t dataPtr = Zoltraak::ReadSafe<uint64_t>(keyAddr, 0);
                    Solitaer::Debug("CLSDB", "  3x elem[%d] at %p: _Data*=%p (no name)",
                        totalFound, reinterpret_cast<void*>(elem),
                        reinterpret_cast<void*>(dataPtr));
                }
            } else {
                ClassEntry entry;
                entry.elementAddr = elem;
                entry.infoAddr    = valueAddr;
                entry.parentName  = "";
                s_classMap[className] = entry;
            }

            totalFound++;
            chainLen++;

            uint64_t next = Zoltraak::ReadSafe<uint64_t>(
                elem + DynOff::ELEM_3X_NEXT_OFF, 0);
            if (next == elem) break; // Self-loop protection
            elem = next;
        }
        if (chainLen > maxChainLen) maxChainLen = chainLen;
    }

    Solitaer::Info("CLSDB", "3x: Built class cache: %d classes found (%u expected, %d empty names)",
        static_cast<int>(s_classMap.size()), s_elements3x, emptyNames);
    Solitaer::Info("CLSDB", "3x: %d occupied buckets / %d total, max chain=%d, %d chain breaks, %d walked",
        occupiedBuckets, bucketCount, maxChainLen, chainBreaks, totalFound);
}

// Enumerate classes by walking 3.x bucket chains (deterministic order not guaranteed)
static void EnumerateClasses3x(
    const std::function<void(const std::string& name, uint64_t infoAddr)>& callback) {
    if (!s_hashTable3x) return;

    int bucketCount = 1 << s_power3x;
    for (int b = 0; b < bucketCount; ++b) {
        uint64_t elem = Zoltraak::ReadSafe<uint64_t>(s_hashTable3x + b * 8, 0);

        while (elem) {
            if (!Zoltraak::IsReadable(elem)) break;

            uint64_t keyAddr   = elem + DynOff::ELEM_3X_KEY_OFF;
            uint64_t valueAddr = elem + DynOff::ELEM_3X_VALUE_OFF;

            std::string className = Flamme::Resolve(keyAddr);
            if (!className.empty()) {
                callback(className, valueAddr);
            }

            uint64_t next = Zoltraak::ReadSafe<uint64_t>(
                elem + DynOff::ELEM_3X_NEXT_OFF, 0);
            if (next == elem) break;
            elem = next;
        }
    }
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

// ============================================================
// ClassInfo Layout Probing
// ============================================================
// ClassInfo struct has version-dependent layout. We probe it dynamically
// using known classes (e.g. "Node") as validation targets.
//
// Key fields to find:
//   - name (StringName) — resolves to the class name string
//   - inherits (StringName) — resolves to parent class name
//   - property_list (List<PropertyInfo>) — 8B _Data* pointer
//   - method_map (HashMap) — 40B inline struct
//
// Strategy: Find 'name' StringName first (scan for StringName resolving to class name),
// then derive other offsets relative to it.

// Probed offsets (set once, used for all classes)
static int s_nameOffset       = -1;
static int s_inheritsOffset   = -1;
static int s_propListOffset   = -1;
static int s_methodMapOffset  = -1;
static int s_propSetGetOffset = -1;  // HashMap<StringName, PropertySetGet>
static bool s_isItaniumABI    = false; // Detected ABI (for vtable indexing)

// PropertyInfo layout within List::Element (probed)
static int s_propNameOffset   = -1;   // String name offset within PropertyInfo
static int s_propHintOffset   = -1;   // PropertyHint offset within PropertyInfo
static int s_propUsageOffset  = -1;   // usage offset within PropertyInfo
static int s_propTotalSize    = -1;   // total PropertyInfo size
static int s_elemNextOffset   = -1;   // next_ptr offset within Element (after PropertyInfo)

// Validate that a pointer looks like a List<PropertyInfo>._Data*
// _Data = { Element* first, Element* last, int size_cache }
static bool ValidateListData(uint64_t dataPtr, int minCount, int maxCount) {
    if (!dataPtr || !Zoltraak::IsReadable(dataPtr)) return false;

    uint64_t first = Zoltraak::ReadSafe<uint64_t>(dataPtr, 0);
    uint64_t last  = Zoltraak::ReadSafe<uint64_t>(dataPtr + 8, 0);
    int32_t  count = Zoltraak::ReadSafe<int32_t>(dataPtr + 16, 0);

    if (count < minCount || count > maxCount) return false;
    if (!first || !Zoltraak::IsReadable(first)) return false;
    if (!last  || !Zoltraak::IsReadable(last))  return false;

    // First element's first 4 bytes should be a valid Variant::Type
    // Version-aware: 3.x max=26, 4.0-4.4 max=37, 4.5+ max=38
    int32_t firstType = Zoltraak::ReadSafe<int32_t>(first, -1);
    int typeMax = GodotVer::VariantTypeMax() - 1;
    if (firstType < 0 || firstType > typeMax) return false;

    return true;
}

// Probe PropertyInfo layout within a List::Element.
// Uses the first Element of a known class to determine field positions.
static bool ProbePropertyInfoLayout(uint64_t dataPtr, const std::string& /*expectedPropName*/) {
    if (!dataPtr) return false;

    uint64_t first = Zoltraak::ReadSafe<uint64_t>(dataPtr, 0);
    int32_t  count = Zoltraak::ReadSafe<int32_t>(dataPtr + 16, 0);
    if (!first || count <= 0) return false;

    // Element layout: { PropertyInfo value; Element* next_ptr; Element* prev_ptr; }
    // PropertyInfo starts at Element+0x00
    // PropertyInfo { Variant::Type type(4B); pad(4B); String name; StringName class_name; ... }

    // Read type field (always at offset 0)
    int32_t propType = Zoltraak::ReadSafe<int32_t>(first, -1);
    Solitaer::Info("CLSDB", "  Probe: first Element at %p, type=%d",
        reinterpret_cast<void*>(first), propType);

    // Scan for the property name String within the Element
    // A Godot String is internally a CowData<char32_t> which is a pointer.
    // Try reading it at various offsets after the type field.
    for (int nameOff = 8; nameOff <= 64; nameOff += 8) {
        uint64_t strPtr = Zoltraak::ReadSafe<uint64_t>(first + nameOff, 0);
        if (!strPtr || !Zoltraak::IsReadable(strPtr)) continue;

        // Try reading as Godot String (CowData pointer)
        std::string resolved = Zoltraak::ReadGodotString(strPtr);
        if (resolved.empty()) {
            // Try as StringName
            resolved = Flamme::ResolveAt(strPtr);
        }

        if (!resolved.empty()) {
            Solitaer::Info("CLSDB", "  Probe: found string '%s' at Element+0x%X",
                resolved.c_str(), nameOff);

            // Check if this looks like a property name
            // Known Node properties: "name", "unique_name_in_owner", "scene_file_path",
            // "process_mode", "process_priority", etc.
            if (resolved.size() >= 2 && resolved.size() <= 64) {
                s_propNameOffset = nameOff;
                Solitaer::Info("CLSDB", "  PropertyInfo.name offset = 0x%X", nameOff);

                // Now find next_ptr: scan after PropertyInfo for a pointer to another Element
                // The next Element should also have a valid Variant::Type at offset 0
                for (int nextOff = nameOff + 8; nextOff <= 128; nextOff += 8) {
                    uint64_t nextPtr = Zoltraak::ReadSafe<uint64_t>(first + nextOff, 0);
                    if (!nextPtr || !Zoltraak::IsReadable(nextPtr)) continue;

                    // Check if nextPtr points to an Element with valid Variant::Type
                    int32_t nextType = Zoltraak::ReadSafe<int32_t>(nextPtr, -1);
                    if (nextType >= 0 && nextType < GodotVer::VariantTypeMax()) {
                        // Verify the next element also has a string at the same name offset
                        uint64_t nextStr = Zoltraak::ReadSafe<uint64_t>(nextPtr + nameOff, 0);
                        if (nextStr && Zoltraak::IsReadable(nextStr)) {
                            std::string nextName = Zoltraak::ReadGodotString(nextStr);
                            if (nextName.empty()) nextName = Flamme::ResolveAt(nextStr);

                            if (!nextName.empty() && nextName.size() >= 2) {
                                s_elemNextOffset = nextOff;
                                s_propTotalSize  = nextOff;  // PropertyInfo ends where next_ptr begins
                                Solitaer::Info("CLSDB", "  Element.next offset = 0x%X (propSize=%d)",
                                    nextOff, s_propTotalSize);
                                Solitaer::Info("CLSDB", "  Second property: '%s' (type=%d)",
                                    nextName.c_str(), nextType);

                                // Verify: follow the chain and count, should match _Data.size_cache
                                int walked = 0;
                                uint64_t cur = first;
                                while (cur && walked < count + 5) {
                                    walked++;
                                    uint64_t nxt = Zoltraak::ReadSafe<uint64_t>(cur + s_elemNextOffset, 0);
                                    if (nxt == cur) break;
                                    cur = nxt;
                                }
                                if (walked == count) {
                                    Solitaer::Info("CLSDB", "  Chain validation: walked %d == count %d ✓",
                                        walked, count);

                                    // Derive hint and usage offsets from the known PropertyInfo layout.
                                    // PropertyInfo { type(8B), name(8B), class_name(8B), hint(8B), hint_string(8B), usage(8B) }
                                    // So: hint = nameOff + 16, usage = nameOff + 32
                                    int hintOff  = nameOff + 16; // name(8B) + class_name(8B)
                                    int usageOff = nameOff + 32; // + hint(8B) + hint_string(8B)

                                    // Validate hint offset: PropertyHint is an int32 enum, values 0-52
                                    int32_t hintVal = Zoltraak::ReadSafe<int32_t>(first + hintOff, -1);
                                    if (hintVal >= 0 && hintVal <= 60) {
                                        s_propHintOffset = hintOff;
                                        Solitaer::Info("CLSDB", "  PropertyInfo.hint offset = 0x%X (value=%d)",
                                            hintOff, hintVal);
                                    } else {
                                        Solitaer::Warn("CLSDB", "  PropertyInfo.hint validation failed: value=%d at +0x%X",
                                            hintVal, hintOff);
                                    }

                                    // Validate usage offset: usage is a uint32 bitmask
                                    uint32_t usageVal = Zoltraak::ReadSafe<uint32_t>(first + usageOff, 0);
                                    if (usageVal > 0 && usageVal < 0x10000) {
                                        s_propUsageOffset = usageOff;
                                        Solitaer::Info("CLSDB", "  PropertyInfo.usage offset = 0x%X (value=0x%X)",
                                            usageOff, usageVal);
                                    } else {
                                        Solitaer::Warn("CLSDB", "  PropertyInfo.usage validation failed: value=0x%X at +0x%X",
                                            usageVal, usageOff);
                                    }

                                    return true;
                                } else {
                                    Solitaer::Debug("CLSDB", "  Chain validation: walked %d != count %d, trying next offset",
                                        walked, count);
                                    // Don't return false yet, try next nextOff
                                }
                            }
                        }
                    }
                }

                // If we found name but not next_ptr, still partial success
                if (s_elemNextOffset < 0) {
                    Solitaer::Warn("CLSDB", "  Found name offset but could not determine Element.next");
                }
                return s_elemNextOffset >= 0;
            }
        }
    }

    Solitaer::Warn("CLSDB", "  Could not probe PropertyInfo layout");
    return false;
}

// ============================================================
// PropertySetGet lookup — find getter MethodBind* for a property
// ============================================================

// PropertySetGet layout within HashMap element (4.x):
//   Element+0x00: next(8B), +0x08: prev(8B)
//   +0x10: StringName key (property name)
//   +0x18: int index (4B) + pad(4B)
//   +0x20: StringName setter (8B)
//   +0x28: StringName getter (8B)
//   +0x30: MethodBind* _setptr (8B)
//   +0x38: MethodBind* _getptr (8B)
//   +0x40: Variant::Type type (4B) + pad(4B)

static uint64_t LookupGetterMethodBind(uint64_t infoAddr, const std::string& propName) {
    if (s_propSetGetOffset < 0 || propName.empty()) return 0;

    if (s_is3x) {
        // 3.x: bucket-chain HashMap
        uint64_t ht    = Zoltraak::ReadSafe<uint64_t>(infoAddr + s_propSetGetOffset, 0);
        uint8_t  power = Zoltraak::ReadSafe<uint8_t>(infoAddr + s_propSetGetOffset + 8, 0);
        if (!ht || power < 1 || power > 15) return 0;

        int buckets = 1 << power;
        for (int b = 0; b < buckets; b++) {
            uint64_t elem = Zoltraak::ReadSafe<uint64_t>(ht + b * 8, 0);
            while (elem && Zoltraak::IsReadable(elem)) {
                // 3.x Element: hash(4B)+pad(4B)+next(8B)+key(8B)+value
                std::string name = Flamme::Resolve(elem + DynOff::ELEM_3X_KEY_OFF);
                if (name == propName) {
                    // PropertySetGet at ELEM_3X_VALUE_OFF
                    // Layout: index(8B), setter(8B), getter(8B), _setptr(8B), _getptr(8B), type(8B)
                    uint64_t getptr = Zoltraak::ReadSafe<uint64_t>(
                        elem + DynOff::ELEM_3X_VALUE_OFF + 0x20, 0); // _getptr
                    return getptr;
                }
                uint64_t next = Zoltraak::ReadSafe<uint64_t>(elem + DynOff::ELEM_3X_NEXT_OFF, 0);
                if (next == elem) break;
                elem = next;
            }
        }
    } else {
        // 4.x: robin-hood HashMap with linked list
        uint64_t head = Zoltraak::ReadSafe<uint64_t>(infoAddr + s_propSetGetOffset + 0x10, 0);
        uint32_t num  = Zoltraak::ReadSafe<uint32_t>(infoAddr + s_propSetGetOffset + 0x24, 0);
        if (!head || num == 0) return 0;

        uint64_t current = head;
        int walked = 0;
        while (current && walked < static_cast<int>(num) + 10) {
            if (!Zoltraak::IsReadable(current)) break;

            std::string name = Flamme::Resolve(current + 0x10);
            if (name == propName) {
                return Zoltraak::ReadSafe<uint64_t>(current + 0x38, 0); // _getptr
            }

            uint64_t next = Zoltraak::ReadSafe<uint64_t>(current, 0);
            if (next == current) break;
            current = next;
            walked++;
        }
    }
    return 0;
}

// ============================================================
// ExtractMemberOffset — analyze getter ptrcall code to find member offset
// ============================================================
// Searches the first N bytes of ptrcall for memory access patterns:
//   [rdx + imm32] (MSVC: rdx = Object*, or GCC: same on Windows)
//
// Returns { offset, accessSize } or { -1, 0 } if not found.
struct MemberAccess {
    int offset = -1;   // byte offset within object
    int size   = 0;    // native access size: 1 (bool), 4 (int32/float), 8 (int64/double/ptr)
};

static MemberAccess ExtractMemberAccess(uint64_t methodBindAddr) {
    MemberAccess result;
    if (!methodBindAddr || !Zoltraak::IsReadable(methodBindAddr)) return result;

    // Read MethodBind vtable
    uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(methodBindAddr, 0);
    if (!vtable || !Zoltraak::IsReadable(vtable)) return result;

    // ptrcall is at vtable[2] for MSVC, vtable[3] for Itanium (GCC)
    int ptrcallIdx = s_isItaniumABI ? 3 : 2;
    uint64_t ptrcallAddr = Zoltraak::ReadSafe<uint64_t>(vtable + ptrcallIdx * 8, 0);
    if (!ptrcallAddr || !Zoltraak::IsReadable(ptrcallAddr)) return result;

    // Read first 64 bytes of ptrcall code
    uint8_t code[64] = {};
    Zoltraak::ReadSafe(ptrcallAddr, code, sizeof(code));

    // Scan for common x86-64 memory access patterns:
    // Pattern 1: REX.W MOV reg, [rdx + imm32] → 8-byte read (ptr/int64/double)
    //   48 8B ?2 XX XX XX XX  (ModRM: mod=10, r/m=010=rdx)
    // Pattern 2: MOV eax, [rdx + imm32] → 4-byte read (int32)
    //   8B ?2 XX XX XX XX     (ModRM: mod=10, r/m=010=rdx)
    // Pattern 3: REX.W MOV reg, [rdx + imm8] → 8-byte read
    //   48 8B 42 XX           (ModRM: mod=01, r/m=010=rdx, reg=000=rax)
    // Pattern 4: MOV eax, [rdx + imm8] → 4-byte read
    //   8B 42 XX
    // Pattern 5: MOVSS xmm, [rdx + imm32] → 4-byte float read
    //   F3 0F 10 ?2 XX XX XX XX
    // Pattern 6: MOVSD xmm, [rdx + imm32] → 8-byte double read
    //   F2 0F 10 ?2 XX XX XX XX
    // Pattern 7: MOVZX eax, byte [rdx + imm32] → 1-byte read (bool)
    //   0F B6 82 XX XX XX XX
    // Pattern 8: MOVSXD reg, [rdx + imm32] → 4-byte sign-extended to 8 (int32)
    //   48 63 ?2 XX XX XX XX

    for (int i = 0; i < 56; i++) {
        // MOVSXD: 48 63 ?2 → sign-extend int32 to int64 (4-byte member)
        if (i + 7 <= 64 && code[i] == 0x48 && code[i+1] == 0x63 &&
            (code[i+2] & 0x07) == 0x02 && (code[i+2] & 0xC0) == 0x80) {
            int32_t off = *reinterpret_cast<int32_t*>(&code[i+3]);
            if (off > 0 && off < 0x10000) { result.offset = off; result.size = 4; return result; }
        }
        // REX.W + MOV reg, [rdx + imm32]: 48 8B ?2 → 8-byte read
        if (i + 7 <= 64 && code[i] == 0x48 && code[i+1] == 0x8B &&
            (code[i+2] & 0x07) == 0x02 && (code[i+2] & 0xC0) == 0x80) {
            int32_t off = *reinterpret_cast<int32_t*>(&code[i+3]);
            if (off > 0 && off < 0x10000) { result.offset = off; result.size = 8; return result; }
        }
        // MOV eax, [rdx + imm32]: 8B ?2 → 4-byte read
        if (i + 6 <= 64 && code[i] == 0x8B &&
            (code[i+1] & 0x07) == 0x02 && (code[i+1] & 0xC0) == 0x80) {
            int32_t off = *reinterpret_cast<int32_t*>(&code[i+2]);
            if (off > 0 && off < 0x10000) { result.offset = off; result.size = 4; return result; }
        }
        // REX.W + MOV reg, [rdx + imm8]: 48 8B 42 XX → 8-byte read
        if (i + 4 <= 64 && code[i] == 0x48 && code[i+1] == 0x8B &&
            code[i+2] == 0x42) {
            int8_t off = static_cast<int8_t>(code[i+3]);
            if (off > 0) { result.offset = off; result.size = 8; return result; }
        }
        // MOV eax, [rdx + imm8]: 8B 42 XX → 4-byte read
        if (i + 3 <= 64 && code[i] == 0x8B && code[i+1] == 0x42) {
            int8_t off = static_cast<int8_t>(code[i+2]);
            if (off > 0) { result.offset = off; result.size = 4; return result; }
        }
        // MOVSS xmm, [rdx + imm32]: F3 0F 10 ?2 → 4-byte float
        if (i + 8 <= 64 && code[i] == 0xF3 && code[i+1] == 0x0F && code[i+2] == 0x10 &&
            (code[i+3] & 0x07) == 0x02 && (code[i+3] & 0xC0) == 0x80) {
            int32_t off = *reinterpret_cast<int32_t*>(&code[i+4]);
            if (off > 0 && off < 0x10000) { result.offset = off; result.size = 4; return result; }
        }
        // MOVSD xmm, [rdx + imm32]: F2 0F 10 ?2 → 8-byte double
        if (i + 8 <= 64 && code[i] == 0xF2 && code[i+1] == 0x0F && code[i+2] == 0x10 &&
            (code[i+3] & 0x07) == 0x02 && (code[i+3] & 0xC0) == 0x80) {
            int32_t off = *reinterpret_cast<int32_t*>(&code[i+4]);
            if (off > 0 && off < 0x10000) { result.offset = off; result.size = 8; return result; }
        }
        // MOVZX eax, byte [rdx + imm32]: 0F B6 82 → 1-byte read (bool)
        if (i + 7 <= 64 && code[i] == 0x0F && code[i+1] == 0xB6 &&
            code[i+2] == 0x82) {
            int32_t off = *reinterpret_cast<int32_t*>(&code[i+3]);
            if (off > 0 && off < 0x10000) { result.offset = off; result.size = 1; return result; }
        }
        // MOVZX eax, byte [rdx + imm8]: 0F B6 42 XX → 1-byte read
        if (i + 4 <= 64 && code[i] == 0x0F && code[i+1] == 0xB6 &&
            code[i+2] == 0x42) {
            int8_t off = static_cast<int8_t>(code[i+3]);
            if (off > 0) { result.offset = off; result.size = 1; return result; }
        }
    }

    return result;
}

static void ProbeClassInfoLayout() {
    if (DynOff::bClassInfoProbed.exchange(true)) return; // One-shot

    Solitaer::Info("CLSDB", "=== Probing ClassInfo layout ===");

    // Use "Node" as our validation target — it's always present in Godot
    auto nodeIt = s_classMap.find("Node");
    if (nodeIt == s_classMap.end()) {
        // Try "Object" as fallback
        nodeIt = s_classMap.find("Object");
        if (nodeIt == s_classMap.end()) {
            Solitaer::Warn("CLSDB", "Cannot probe: neither 'Node' nor 'Object' found in ClassDB");
            return;
        }
    }

    const std::string& targetClass = nodeIt->first;
    uint64_t infoAddr = nodeIt->second.infoAddr;
    Solitaer::Info("CLSDB", "  Using '%s' class at ClassInfo=%p",
        targetClass.c_str(), reinterpret_cast<void*>(infoAddr));

    // Step 1: Find the 'name' StringName field by scanning ClassInfo memory.
    // Scan at 8-byte stride for a StringName that resolves to the target class name.
    // Range 0x40..0x800 covers all known versions (3.x ~536B, 4.5+ ~816B)
    s_nameOffset = -1;
    for (int off = 0x40; off <= 0x800; off += 8) {
        uint64_t candidate = Zoltraak::ReadSafe<uint64_t>(infoAddr + off, 0);
        if (!candidate || !Zoltraak::IsReadable(candidate)) continue;

        std::string resolved = Flamme::Resolve(infoAddr + off);
        if (resolved == targetClass) {
            s_nameOffset = off;
            Solitaer::Info("CLSDB", "  Found 'name' StringName at ClassInfo+0x%X -> '%s'",
                off, resolved.c_str());
            break;
        }
    }

    if (s_nameOffset < 0) {
        Solitaer::Warn("CLSDB", "  Could not find 'name' StringName field in ClassInfo");
        return;
    }

    // Step 2: Find 'inherits' StringName — typically 8 bytes before 'name'
    s_inheritsOffset = -1;
    for (int delta : {-8, -16, 8, 16}) {
        int tryOff = s_nameOffset + delta;
        if (tryOff < 0) continue;

        std::string resolved = Flamme::Resolve(infoAddr + tryOff);
        if (!resolved.empty() && resolved != targetClass) {
            // Validate: should match the known parent
            if (resolved == nodeIt->second.parentName || nodeIt->second.parentName.empty()) {
                s_inheritsOffset = tryOff;
                Solitaer::Info("CLSDB", "  Found 'inherits' StringName at ClassInfo+0x%X -> '%s'",
                    tryOff, resolved.c_str());
                break;
            }
        }
    }

    if (s_inheritsOffset < 0) {
        // Try the offset just before name
        s_inheritsOffset = s_nameOffset - 8;
        Solitaer::Info("CLSDB", "  Assuming 'inherits' at ClassInfo+0x%X (default -8 from name)",
            s_inheritsOffset);
    }

    // Step 3: Find 'property_list' — a List<PropertyInfo> = 8B _Data* pointer.
    // Scan backwards from 'inherits' looking for a valid List _Data*.
    s_propListOffset = -1;
    int searchStart = s_inheritsOffset - 8;
    int searchEnd   = 0x20; // Don't go below the first few fixed fields

    for (int off = searchStart; off >= searchEnd; off -= 8) {
        uint64_t candidate = Zoltraak::ReadSafe<uint64_t>(infoAddr + off, 0);
        if (ValidateListData(candidate, 1, 200)) {
            // Additional validation: try to read property names from the list
            int32_t  count = Zoltraak::ReadSafe<int32_t>(candidate + 16, 0);

            Solitaer::Info("CLSDB", "  Candidate property_list at ClassInfo+0x%X: _Data=%p count=%d",
                off, reinterpret_cast<void*>(candidate), count);

            // For "Node", count should be reasonable (5-100 properties)
            if (count >= 3 && count <= 200) {
                s_propListOffset = off;
                Solitaer::Info("CLSDB", "  Found 'property_list' at ClassInfo+0x%X (count=%d)",
                    off, count);

                // Probe PropertyInfo layout using this list
                ProbePropertyInfoLayout(candidate, targetClass);
                break;
            }
        }
    }

    if (s_propListOffset < 0) {
        Solitaer::Warn("CLSDB", "  Could not find 'property_list' in ClassInfo");
    }

    // Step 4: Find 'method_map' — a HashMap with StringName keys.
    s_methodMapOffset = -1;
    int mmSearchEnd = (s_propListOffset > 0) ? s_propListOffset : s_inheritsOffset;

    if (s_is3x) {
        // 3.x method_map: bucket-chain HashMap (16 bytes inline)
        // Layout: hash_table*(8B) + power(1B+3B pad) + elements(4B) = 16 bytes
        for (int off = 0x20; off < mmSearchEnd; off += 8) {
            uint64_t ht    = Zoltraak::ReadSafe<uint64_t>(infoAddr + off, 0);
            uint8_t  pw    = Zoltraak::ReadSafe<uint8_t>(infoAddr + off + 0x08, 0);
            uint32_t count = Zoltraak::ReadSafe<uint32_t>(infoAddr + off + 0x0C, 0);

            if (!ht || !Zoltraak::IsReadable(ht)) continue;
            if (pw < 2 || pw > 15) continue;
            if (count < 5 || count > 1000) continue;

            // Validate: first non-null bucket element should have a StringName key
            int buckets = 1 << pw;
            bool found = false;
            for (int b = 0; b < buckets && !found; ++b) {
                uint64_t elem = Zoltraak::ReadSafe<uint64_t>(ht + b * 8, 0);
                if (!elem || !Zoltraak::IsReadable(elem)) continue;

                uint64_t keyAddr = elem + DynOff::ELEM_3X_KEY_OFF;
                std::string methodName = Flamme::Resolve(keyAddr);
                if (!methodName.empty() && methodName.size() >= 2 && methodName.size() <= 64) {
                    s_methodMapOffset = off;
                    Solitaer::Info("CLSDB", "  Found 3.x 'method_map' at ClassInfo+0x%X "
                        "(power=%u, num=%u, first='%s')",
                        off, pw, count, methodName.c_str());
                    found = true;
                }
            }
            if (found) break;
        }
    } else {
        // 4.x method_map: robin-hood HashMap (40 bytes inline)
        // Layout: elements*(8B), hashes*(8B), head*(8B), tail*(8B), capIdx(4B), num(4B)
        for (int off = 0x20; off < mmSearchEnd; off += 8) {
            uint64_t elems  = Zoltraak::ReadSafe<uint64_t>(infoAddr + off, 0);
            uint64_t hashes = Zoltraak::ReadSafe<uint64_t>(infoAddr + off + 8, 0);
            uint64_t head   = Zoltraak::ReadSafe<uint64_t>(infoAddr + off + 0x10, 0);
            uint64_t tail   = Zoltraak::ReadSafe<uint64_t>(infoAddr + off + 0x18, 0);
            uint32_t num    = Zoltraak::ReadSafe<uint32_t>(infoAddr + off + 0x24, 0);

            if (!elems || !hashes || !head || !tail) continue;
            if (!Zoltraak::IsReadable(elems) || !Zoltraak::IsReadable(head)) continue;
            if (num < 5 || num > 1000) continue;

            // Element: next(8B), prev(8B), KeyValue{ StringName(8B), MethodBind*(8B) }
            uint64_t keyAddr = head + 0x10;
            std::string methodName = Flamme::Resolve(keyAddr);
            if (!methodName.empty() && methodName.size() >= 2 && methodName.size() <= 64) {
                s_methodMapOffset = off;
                Solitaer::Info("CLSDB", "  Found 'method_map' at ClassInfo+0x%X (num=%u, first='%s')",
                    off, num, methodName.c_str());
                break;
            }
        }
    }

    if (s_methodMapOffset < 0) {
        Solitaer::Warn("CLSDB", "  Could not find 'method_map' in ClassInfo");
    }

    // Step 5: Find 'property_setget' — HashMap<StringName, PropertySetGet>
    // Located after property_list in ClassInfo struct.
    // PropertySetGet contains getter/setter MethodBind* pointers.
    s_propSetGetOffset = -1;
    if (s_propListOffset > 0) {
        int psgSearchStart = s_propListOffset + 8; // After property_list (8B _Data*)
        int psgSearchEnd   = s_nameOffset;         // Don't go past 'name'

        if (s_is3x) {
            // 3.x: bucket-chain HashMap (16B)
            for (int off = psgSearchStart; off < psgSearchEnd; off += 8) {
                uint64_t ht    = Zoltraak::ReadSafe<uint64_t>(infoAddr + off, 0);
                uint8_t  pw    = Zoltraak::ReadSafe<uint8_t>(infoAddr + off + 0x08, 0);
                uint32_t count = Zoltraak::ReadSafe<uint32_t>(infoAddr + off + 0x0C, 0);

                if (!ht || !Zoltraak::IsReadable(ht)) continue;
                if (pw < 2 || pw > 15 || count < 3 || count > 500) continue;

                // Validate: key should be a property name
                int buckets = 1 << pw;
                for (int b = 0; b < buckets; ++b) {
                    uint64_t elem = Zoltraak::ReadSafe<uint64_t>(ht + b * 8, 0);
                    if (!elem || !Zoltraak::IsReadable(elem)) continue;
                    std::string name = Flamme::Resolve(elem + DynOff::ELEM_3X_KEY_OFF);
                    if (!name.empty() && name.size() <= 64 &&
                        off != s_methodMapOffset) { // Don't confuse with method_map
                        s_propSetGetOffset = off;
                        Solitaer::Info("CLSDB", "  Found 3.x 'property_setget' at ClassInfo+0x%X "
                            "(power=%u, num=%u, first='%s')", off, pw, count, name.c_str());
                        break;
                    }
                }
                if (s_propSetGetOffset >= 0) break;
            }
        } else {
            // 4.x: robin-hood HashMap (40B)
            for (int off = psgSearchStart; off < psgSearchEnd; off += 8) {
                if (off == s_methodMapOffset) continue; // Skip method_map

                uint64_t head = Zoltraak::ReadSafe<uint64_t>(infoAddr + off + 0x10, 0);
                uint32_t num  = Zoltraak::ReadSafe<uint32_t>(infoAddr + off + 0x24, 0);
                if (!head || num < 3 || num > 500) continue;
                if (!Zoltraak::IsReadable(head)) continue;

                // Read key from head element
                std::string name = Flamme::Resolve(head + 0x10);
                if (name.empty() || name.size() > 64) continue;

                // Validate: try to read PropertySetGet fields
                // At element+0x28: getter StringName should resolve to "get_xxx"
                std::string getter = Flamme::Resolve(head + 0x28);
                uint64_t getptr = Zoltraak::ReadSafe<uint64_t>(head + 0x38, 0);

                if (!getter.empty() && getptr && Zoltraak::IsReadable(getptr)) {
                    s_propSetGetOffset = off;
                    Solitaer::Info("CLSDB", "  Found 'property_setget' at ClassInfo+0x%X "
                        "(num=%u, first='%s', getter='%s', _getptr=%p)",
                        off, num, name.c_str(), getter.c_str(),
                        reinterpret_cast<void*>(getptr));
                    break;
                }
            }
        }

        if (s_propSetGetOffset < 0) {
            Solitaer::Info("CLSDB", "  Could not find 'property_setget' in ClassInfo (offset extraction unavailable)");
        }
    }

    // Store results in DynOff for global access
    DynOff::CLASSINFO_NAME_OFFSET      = s_nameOffset;
    DynOff::CLASSINFO_INHERITS_OFFSET  = s_inheritsOffset;
    DynOff::CLASSINFO_PROPLIST_OFFSET  = s_propListOffset;
    DynOff::CLASSINFO_METHODMAP_OFFSET = s_methodMapOffset;
    DynOff::PROPINFO_NAME_OFFSET       = s_propNameOffset;
    DynOff::PROPINFO_HINT_OFFSET       = s_propHintOffset;
    DynOff::PROPINFO_USAGE_OFFSET      = s_propUsageOffset;
    DynOff::ELEMENT_NEXT_OFFSET        = s_elemNextOffset;
    DynOff::PROPINFO_TOTAL_SIZE        = s_propTotalSize;

    Solitaer::Info("CLSDB", "=== ClassInfo probe complete ===");
    Solitaer::Info("CLSDB", "  name=+0x%X inherits=+0x%X propList=+0x%X methodMap=+0x%X propSetGet=+0x%X",
        s_nameOffset, s_inheritsOffset, s_propListOffset, s_methodMapOffset, s_propSetGetOffset);
    Solitaer::Info("CLSDB", "  propNameOff=+0x%X hintOff=+0x%X usageOff=+0x%X elemNextOff=+0x%X propSize=%d",
        s_propNameOffset, s_propHintOffset, s_propUsageOffset, s_elemNextOffset, s_propTotalSize);
}

void Init(uint64_t classDBAddr) {
    s_classDBAddr = classDBAddr;
    s_allocOffset = DynOff::CLASSDB_ALLOC_OFFSET;
    s_classMap.clear();
    s_vtableMap.clear();
    s_is3x = GodotVer::IsGodot3();
    EnsureModuleInfo();

    if (!classDBAddr) {
        Solitaer::Warn("CLSDB", "Init called with null address");
        return;
    }

    if (s_is3x) {
        // 3.x: bucket-chain HashMap<StringName, ClassInfo>
        if (!ReadHashMapFields3x()) {
            Solitaer::Warn("CLSDB", "Failed to read 3.x ClassDB HashMap fields");
            return;
        }
        BuildClassCache3x();
        // Set s_numElements for diagnostics/compatibility
        s_numElements = s_elements3x;
    } else {
        // 4.x: robin-hood HashMap with linked list
        if (!ReadHashMapFields()) {
            Solitaer::Warn("CLSDB", "Failed to read ClassDB HashMap fields");
            return;
        }
        BuildClassCache();
    }

    // Now resolve parent names for each class (second pass)
    int resolvedParents = 0;
    for (auto& [name, entry] : s_classMap) {
        entry.parentName = ResolveParentName(entry.infoAddr);
        if (!entry.parentName.empty()) resolvedParents++;
    }

    Solitaer::Info("CLSDB", "Init complete: %d classes, %d parent links resolved",
        static_cast<int>(s_classMap.size()), resolvedParents);

    // Probe ClassInfo struct layout (find property_list, method_map, etc.)
    ProbeClassInfoLayout();
}

int GetClassCount() {
    return static_cast<int>(s_classMap.size());
}

void EnumerateClasses(const std::function<void(const std::string& name, uint64_t infoAddr)>& callback) {
    if (s_is3x) {
        // 3.x: iterate bucket chains
        EnumerateClasses3x(callback);
        return;
    }

    // 4.x: Walk the linked list in order for deterministic output
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

    // Populate properties and methods on demand
    info.properties = GetPropertyList(className);
    info.methods    = GetMethodList(className);

    return info;
}

// Read properties from a single ClassInfo's property_list (no inheritance)
static std::vector<PropertyInfo> ReadPropertyListRaw(uint64_t infoAddr, const std::string& owningClass) {
    std::vector<PropertyInfo> result;
    if (s_propListOffset < 0 || s_propNameOffset < 0 || s_elemNextOffset < 0) return result;

    // Read List._Data* pointer
    uint64_t dataPtr = Zoltraak::ReadSafe<uint64_t>(infoAddr + s_propListOffset, 0);
    if (!dataPtr || !Zoltraak::IsReadable(dataPtr)) return result;

    // Read _Data.first and _Data.size_cache
    uint64_t first = Zoltraak::ReadSafe<uint64_t>(dataPtr, 0);
    int32_t  count = Zoltraak::ReadSafe<int32_t>(dataPtr + 16, 0);
    if (!first || count <= 0) return result;

    result.reserve(count);
    uint64_t elem = first;
    int walked = 0;

    while (elem && walked < count + 10) { // Safety margin
        if (!Zoltraak::IsReadable(elem)) break;

        // Read PropertyInfo from Element.value (starts at Element+0x00)
        // type is always the first field of PropertyInfo (offset 0)
        int32_t propType = Zoltraak::ReadSafe<int32_t>(elem, -1);
        if (propType < 0 || propType >= GodotVer::VariantTypeMax()) {
            // Invalid type — might have wrong offsets, stop
            Solitaer::Debug("CLSDB", "  Invalid prop type %d at elem %p, stopping",
                propType, reinterpret_cast<void*>(elem));
            break;
        }

        // Read property name
        std::string propName;
        uint64_t nameData = Zoltraak::ReadSafe<uint64_t>(elem + s_propNameOffset, 0);
        if (nameData && Zoltraak::IsReadable(nameData)) {
            propName = Zoltraak::ReadGodotString(nameData);
            if (propName.empty()) {
                propName = Flamme::ResolveAt(nameData);
            }
        }

        if (!propName.empty()) {
            PropertyInfo pi;
            pi.name      = propName;
            pi.className = owningClass;
            pi.type      = propType;
            pi.offset    = -1;
            pi.nativeSize = 0;

            // Try to extract byte offset via ptrcall analysis
            if (s_propSetGetOffset >= 0) {
                uint64_t getptr = LookupGetterMethodBind(infoAddr, propName);
                if (getptr) {
                    MemberAccess ma = ExtractMemberAccess(getptr);
                    if (ma.offset >= 0) {
                        pi.offset     = ma.offset;
                        pi.nativeSize = ma.size;
                    }
                }
            }

            // Read hint (PropertyHint enum, int32)
            if (s_propHintOffset >= 0) {
                pi.hint = Zoltraak::ReadSafe<int32_t>(elem + s_propHintOffset, 0);
            }

            // Read hint_string (Godot String = CowData pointer)
            if (s_propHintOffset >= 0) {
                int hintStrOff = s_propHintOffset + 8;
                uint64_t hintStrPtr = Zoltraak::ReadSafe<uint64_t>(elem + hintStrOff, 0);
                if (hintStrPtr && Zoltraak::IsReadable(hintStrPtr)) {
                    pi.hintString = Zoltraak::ReadGodotString(hintStrPtr);
                }
            }

            // Read usage (uint32_t bitmask)
            if (s_propUsageOffset >= 0) {
                pi.usage = Zoltraak::ReadSafe<uint32_t>(elem + s_propUsageOffset, 0);
            }

            result.push_back(std::move(pi));
        }

        // Advance to next Element
        uint64_t next = Zoltraak::ReadSafe<uint64_t>(elem + s_elemNextOffset, 0);
        if (next == elem) break; // Self-loop protection
        elem = next;
        walked++;
    }

    return result;
}

std::vector<PropertyInfo> GetPropertyList(const char* className) {
    if (!className || s_propListOffset < 0) {
        if (s_propListOffset < 0) {
            Solitaer::Debug("CLSDB", "GetPropertyList: ClassInfo layout not probed yet");
        }
        return {};
    }

    auto it = s_classMap.find(className);
    if (it == s_classMap.end()) return {};

    // Collect properties with inheritance (parent first, then child)
    std::vector<PropertyInfo> result;

    // Build inheritance chain
    std::vector<std::pair<std::string, uint64_t>> chain; // (className, infoAddr)
    std::string cur = className;
    while (!cur.empty()) {
        auto cit = s_classMap.find(cur);
        if (cit == s_classMap.end()) break;
        chain.push_back({cur, cit->second.infoAddr});
        cur = cit->second.parentName;
    }

    // Walk from root parent to child (reverse order)
    for (int i = static_cast<int>(chain.size()) - 1; i >= 0; i--) {
        auto props = ReadPropertyListRaw(chain[i].second, chain[i].first);
        for (auto& p : props) {
            result.push_back(std::move(p));
        }
    }

    return result;
}

// Read methods from a single ClassInfo's method_map HashMap (no inheritance)
static std::vector<MethodInfo> ReadMethodMapRaw(uint64_t infoAddr) {
    std::vector<MethodInfo> result;
    if (s_methodMapOffset < 0) return result;

    if (s_is3x) {
        // 3.x: bucket-chain HashMap (16 bytes inline)
        // Layout: hash_table*(8B) + power(1B+3B pad) + elements(4B)
        uint64_t ht    = Zoltraak::ReadSafe<uint64_t>(infoAddr + s_methodMapOffset, 0);
        uint8_t  pw    = Zoltraak::ReadSafe<uint8_t>(infoAddr + s_methodMapOffset + 0x08, 0);
        uint32_t count = Zoltraak::ReadSafe<uint32_t>(infoAddr + s_methodMapOffset + 0x0C, 0);

        if (!ht || !Zoltraak::IsReadable(ht) || count == 0) return result;

        result.reserve(count);
        int buckets = 1 << pw;

        for (int b = 0; b < buckets; ++b) {
            uint64_t elem = Zoltraak::ReadSafe<uint64_t>(ht + b * 8, 0);
            while (elem) {
                if (!Zoltraak::IsReadable(elem)) break;

                // 3.x Element: hash(4B)+pad(4B)+next(8B)+key(8B StringName)+value(8B MethodBind*)
                uint64_t keyAddr   = elem + DynOff::ELEM_3X_KEY_OFF;
                uint64_t valueAddr = elem + DynOff::ELEM_3X_VALUE_OFF;

                std::string methodName = Flamme::Resolve(keyAddr);
                if (!methodName.empty()) {
                    MethodInfo mi;
                    mi.name = methodName;
                    mi.returnType = "";
                    mi.argCount   = -1;

                    uint64_t methodBind = Zoltraak::ReadSafe<uint64_t>(valueAddr, 0);
                    (void)methodBind;

                    result.push_back(std::move(mi));
                }

                uint64_t next = Zoltraak::ReadSafe<uint64_t>(
                    elem + DynOff::ELEM_3X_NEXT_OFF, 0);
                if (next == elem) break;
                elem = next;
            }
        }
        return result;
    }

    // 4.x: robin-hood HashMap with linked list
    // HashMap layout (inline in ClassInfo):
    //   +0x00: HashMapElement** elements
    //   +0x08: uint32_t* hashes
    //   +0x10: HashMapElement* head_element
    //   +0x18: HashMapElement* tail_element
    //   +0x20: uint32_t capacity_index
    //   +0x24: uint32_t num_elements
    uint64_t head = Zoltraak::ReadSafe<uint64_t>(infoAddr + s_methodMapOffset + 0x10, 0);
    uint32_t num  = Zoltraak::ReadSafe<uint32_t>(infoAddr + s_methodMapOffset + 0x24, 0);

    if (!head || num == 0) return result;

    result.reserve(num);
    uint64_t current = head;
    int walked = 0;
    int maxIter = static_cast<int>(num) + 50;

    while (current && walked < maxIter) {
        if (!Zoltraak::IsReadable(current)) break;

        // HashMapElement: next(8B), prev(8B), KeyValue{ StringName key(8B), MethodBind* value(8B) }
        uint64_t keyAddr   = current + 0x10; // StringName
        uint64_t valueAddr = current + 0x18; // MethodBind*

        std::string methodName = Flamme::Resolve(keyAddr);
        if (!methodName.empty()) {
            MethodInfo mi;
            mi.name = methodName;
            mi.returnType = "";  // Would need MethodBind probing
            mi.argCount   = -1;  // Would need MethodBind probing

            // Try to read basic MethodBind info
            uint64_t methodBind = Zoltraak::ReadSafe<uint64_t>(valueAddr, 0);
            if (methodBind && Zoltraak::IsReadable(methodBind)) {
                (void)methodBind;
            }

            result.push_back(std::move(mi));
        }

        uint64_t next = Zoltraak::ReadSafe<uint64_t>(current, 0);
        if (next == current) break;
        current = next;
        walked++;
    }

    return result;
}

std::vector<MethodInfo> GetMethodList(const char* className) {
    if (!className || s_methodMapOffset < 0) {
        if (s_methodMapOffset < 0) {
            Solitaer::Debug("CLSDB", "GetMethodList: ClassInfo layout not probed yet");
        }
        return {};
    }

    auto it = s_classMap.find(className);
    if (it == s_classMap.end()) return {};

    // Collect methods with inheritance (parent first, then child)
    std::vector<MethodInfo> result;

    // Build inheritance chain
    std::vector<std::pair<std::string, uint64_t>> chain;
    std::string cur = className;
    while (!cur.empty()) {
        auto cit = s_classMap.find(cur);
        if (cit == s_classMap.end()) break;
        chain.push_back({cur, cit->second.infoAddr});
        cur = cit->second.parentName;
    }

    // Walk from root parent to child
    for (int i = static_cast<int>(chain.size()) - 1; i >= 0; i--) {
        auto methods = ReadMethodMapRaw(chain[i].second);
        for (auto& m : methods) {
            result.push_back(std::move(m));
        }
    }

    return result;
}

// Extract class name from MSVC RTTI data associated with a vtable pointer.
// MSVC stores RTTICompleteObjectLocator at vtable[-1] (or vtable[-8] in bytes).
// This allows us to recover the C++ class name without calling any game code.
static std::string GetClassFromMSVC_RTTI(uint64_t vtablePtr, std::string* rawNameOut = nullptr) {
    if (!vtablePtr || !s_moduleBase) return "";

    // MSVC RTTI: vtable[-1] = RTTICompleteObjectLocator*
    uint64_t rttiLocator = Zoltraak::ReadSafe<uint64_t>(vtablePtr - 8, 0);
    if (!rttiLocator || !Zoltraak::IsReadable(rttiLocator)) return "";

    // RTTICompleteObjectLocator (64-bit):
    //   +0x00: uint32_t signature (must be 1 for 64-bit)
    //   +0x04: uint32_t offset
    //   +0x08: uint32_t cdOffset
    //   +0x0C: uint32_t pTypeDescriptor (RVA from module base)
    //   +0x10: uint32_t pClassDescriptor (RVA)
    //   +0x14: uint32_t pSelf (RVA)
    uint32_t sig = Zoltraak::ReadSafe<uint32_t>(rttiLocator, 0);
    if (sig != 1) return ""; // Not 64-bit RTTI or RTTI disabled

    uint32_t typeDescRVA = Zoltraak::ReadSafe<uint32_t>(rttiLocator + 0x0C, 0);
    if (!typeDescRVA) return "";

    uint64_t typeDesc = s_moduleBase + typeDescRVA;
    if (!Zoltraak::IsReadable(typeDesc)) return "";

    // TypeDescriptor:
    //   +0x00: void* pVFTable (type_info vtable)
    //   +0x08: void* spare
    //   +0x10: char name[] (decorated name, null-terminated)
    // Name format: ".?AVClassName@@" or ".?AVNamespace@ClassName@@"
    char decorated[256] = {};
    Zoltraak::ReadSafe(typeDesc + 0x10, decorated, sizeof(decorated) - 1);

    std::string raw(decorated);
    if (rawNameOut) *rawNameOut = raw;
    if (raw.size() < 7) return ""; // Minimum: ".?AVX@@"

    // Parse MSVC decorated name: ".?AV<class>@@" or ".?AV<class>@<namespace>@@"
    if (raw.substr(0, 4) != ".?AV") return "";

    auto atAt = raw.find("@@");
    if (atAt == std::string::npos || atAt <= 4) return "";

    // Extract the class name part (everything between .?AV and the first @ or @@)
    std::string fullName = raw.substr(4, atAt - 4);

    // Handle namespaced names: "ClassName@Namespace" → just use "ClassName"
    // (Godot classes are typically in global scope or Godot:: namespace)
    auto atPos = fullName.find('@');
    std::string className = (atPos != std::string::npos) ? fullName.substr(0, atPos) : fullName;

    // Validate: only return names that exist in our ClassDB cache
    if (s_classMap.count(className)) {
        return className;
    }

    // Try the full name in case the simple name didn't match
    if (atPos != std::string::npos && s_classMap.count(fullName)) {
        return fullName;
    }

    return "";
}

// Extract class name from Itanium ABI RTTI (GCC/Clang/MinGW on Windows).
// Itanium ABI: vtable[-1] = std::type_info* which has:
//   +0x00: void* __vptr (type_info vtable)
//   +0x08: const char* __name (mangled name)
// Name format: "N5Godot6Node2DE" or just "6Node2D" for non-namespaced
static std::string GetClassFromItanium_RTTI(uint64_t vtablePtr, std::string* rawNameOut = nullptr) {
    if (!vtablePtr) return "";

    // Itanium: typeinfo ptr at vtable[-1] (i.e., vtable - 8 bytes)
    uint64_t typeInfoPtr = Zoltraak::ReadSafe<uint64_t>(vtablePtr - 8, 0);
    if (!typeInfoPtr || !Zoltraak::IsReadable(typeInfoPtr)) return "";

    // Read the __name pointer (at +0x08)
    uint64_t namePtr = Zoltraak::ReadSafe<uint64_t>(typeInfoPtr + 8, 0);
    if (!namePtr || !Zoltraak::IsReadable(namePtr)) return "";

    char mangledName[256] = {};
    Zoltraak::ReadSafe(namePtr, mangledName, sizeof(mangledName) - 1);

    std::string raw(mangledName);
    if (rawNameOut) *rawNameOut = raw;
    if (raw.empty()) return "";

    // Try to demangle simple Itanium names:
    // "6Node2D" → extract length-prefixed name
    // "N5Godot6Node2DE" → nested name, extract last segment
    std::string className;
    size_t pos = 0;

    if (raw[0] == 'N') {
        // Nested name: skip 'N', read length-prefixed segments, last one before 'E'
        pos = 1;
        std::string last;
        while (pos < raw.size() && raw[pos] != 'E') {
            int len = 0;
            while (pos < raw.size() && raw[pos] >= '0' && raw[pos] <= '9') {
                len = len * 10 + (raw[pos] - '0');
                pos++;
            }
            if (len <= 0 || pos + len > raw.size()) break;
            last = raw.substr(pos, len);
            pos += len;
        }
        className = last;
    } else if (raw[0] >= '1' && raw[0] <= '9') {
        // Simple name: length-prefixed
        int len = 0;
        while (pos < raw.size() && raw[pos] >= '0' && raw[pos] <= '9') {
            len = len * 10 + (raw[pos] - '0');
            pos++;
        }
        if (len > 0 && pos + len <= raw.size()) {
            className = raw.substr(pos, len);
        }
    }

    if (className.empty()) return "";

    // Validate against ClassDB
    if (s_classMap.count(className)) {
        return className;
    }
    return "";
}

// Try both MSVC and Itanium RTTI
static std::string GetClassFromRTTI(uint64_t vtablePtr) {
    // Try MSVC first (most common on Windows)
    std::string result = GetClassFromMSVC_RTTI(vtablePtr);
    if (!result.empty()) return result;

    // Fallback to Itanium ABI (MinGW/GCC/Clang builds)
    return GetClassFromItanium_RTTI(vtablePtr);
}

std::string GetObjectClass(uint64_t objectAddr) {
    if (!objectAddr) return "";
    EnsureModuleInfo();

    // Read vtable from object
    uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(objectAddr, 0);
    if (!vtable || vtable < s_moduleBase || vtable >= s_moduleBase + s_moduleSize) {
        return "";
    }

    // Check vtable cache first (populated by ScanObjectVtables at init)
    auto it = s_vtableMap.find(vtable);
    if (it != s_vtableMap.end()) {
        return it->second;
    }

    // Cache miss — try RTTI (both MSVC and Itanium)
    std::string className = GetClassFromRTTI(vtable);
    s_vtableMap[vtable] = className;
    return className;
}

void LogDiagnostics() {
    static std::atomic<bool> s_done{false};
    if (s_done.exchange(true)) return; // One-shot

    Solitaer::Info("CLSDB", "=== Diagoldze Diagnostics ===");
    Solitaer::Info("CLSDB", "ClassDB addr:   %p", reinterpret_cast<void*>(s_classDBAddr));
    Solitaer::Info("CLSDB", "Mode:           %s", s_is3x ? "3.x bucket-chain" : "4.x robin-hood");
    if (s_is3x) {
        Solitaer::Info("CLSDB", "3x HashMap:     hashTable=%p power=%u elements=%u",
            reinterpret_cast<void*>(s_hashTable3x), s_power3x, s_elements3x);
    } else {
        Solitaer::Info("CLSDB", "Alloc offset:   %d (0x%X)", s_allocOffset, s_allocOffset);
        Solitaer::Info("CLSDB", "HashMap:        head=%p tail=%p capIdx=%u num=%u",
            reinterpret_cast<void*>(s_headElement),
            reinterpret_cast<void*>(s_tailElement),
            s_capacityIndex, s_numElements);
    }
    Solitaer::Info("CLSDB", "Classes cached: %d", static_cast<int>(s_classMap.size()));
    Solitaer::Info("CLSDB", "Module range:   %p..+%llX",
        reinterpret_cast<void*>(s_moduleBase),
        static_cast<unsigned long long>(s_moduleSize));

    // Show first 20 class names with their parent
    int shown = 0;
    EnumerateClasses([&](const std::string& name, uint64_t infoAddr) {
        if (shown >= 20) return;

        auto it = s_classMap.find(name);
        std::string parent = (it != s_classMap.end()) ? it->second.parentName : "?";

        Solitaer::Info("CLSDB", "  [%d] %s : %s (info=%p)",
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
    Solitaer::Info("CLSDB", "Known class check: %d/%d found",
        foundExpected, static_cast<int>(sizeof(EXPECTED) / sizeof(EXPECTED[0])));
}

// Proactive RTTI scan: scan a list of object addresses and try to identify their classes.
// This builds the vtable→class cache and logs diagnostic info about RTTI availability.
// Call this after ObjectDB init to pre-seed the vtable map.
void ScanObjectVtables(const std::vector<uint64_t>& objectAddrs) {
    EnsureModuleInfo();

    Solitaer::Info("CLSDB", "=== RTTI Vtable Scan ===");

    int scanned = 0;
    int identified = 0;
    int msvcHits = 0;
    int itaniumHits = 0;
    int misses = 0;
    int maxDetailLog = 10;
    int detailLogged = 0;

    std::unordered_map<uint64_t, bool> seenVtables;

    for (uint64_t objAddr : objectAddrs) {
        scanned++;

        uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(objAddr, 0);
        if (!vtable) continue;

        // Skip already-seen vtables
        if (seenVtables.count(vtable)) continue;
        seenVtables[vtable] = true;

        // Check vtable range
        bool inModule = (vtable >= s_moduleBase && vtable < s_moduleBase + s_moduleSize);

        // Try MSVC RTTI
        std::string msvcRaw;
        std::string msvcResult = inModule ? GetClassFromMSVC_RTTI(vtable, &msvcRaw) : "";

        // Try Itanium RTTI
        std::string itaniumRaw;
        std::string itaniumResult = inModule ? GetClassFromItanium_RTTI(vtable, &itaniumRaw) : "";

        std::string finalClass = !msvcResult.empty() ? msvcResult :
                                 !itaniumResult.empty() ? itaniumResult : "";

        if (!finalClass.empty()) {
            identified++;
            if (!msvcResult.empty()) msvcHits++;
            else itaniumHits++;
            s_vtableMap[vtable] = finalClass;
        } else {
            misses++;
            s_vtableMap[vtable] = "";
        }

        // Log details for first N unique vtables
        if (detailLogged < maxDetailLog) {
            detailLogged++;
            uint64_t locPtr = inModule ? Zoltraak::ReadSafe<uint64_t>(vtable - 8, 0) : 0;
            uint32_t sig = (locPtr && Zoltraak::IsReadable(locPtr)) ?
                           Zoltraak::ReadSafe<uint32_t>(locPtr, 0xFFFF) : 0xFFFF;

            Solitaer::Info("CLSDB", "  vtable=%p obj=%p inMod=%d loc=%p sig=%u msvc='%s' ita='%s' -> %s",
                reinterpret_cast<void*>(vtable),
                reinterpret_cast<void*>(objAddr),
                inModule ? 1 : 0,
                reinterpret_cast<void*>(locPtr),
                sig,
                msvcRaw.empty() ? "(none)" : msvcRaw.c_str(),
                itaniumRaw.empty() ? "(none)" : itaniumRaw.c_str(),
                finalClass.empty() ? "(FAILED)" : finalClass.c_str());
        }
    }

    // Set global ABI flag based on which RTTI had more hits
    if (itaniumHits > 0 || msvcHits > 0) {
        s_isItaniumABI = (itaniumHits >= msvcHits);
        Solitaer::Info("CLSDB", "  ABI detected: %s (MSVC=%d, Itanium=%d)",
            s_isItaniumABI ? "Itanium" : "MSVC", msvcHits, itaniumHits);
    }

    Solitaer::Info("CLSDB", "  Scanned %d objects, %d unique vtables, %d identified (MSVC=%d Itanium=%d), %d misses",
        scanned, static_cast<int>(seenVtables.size()), identified, msvcHits, itaniumHits, misses);
    Solitaer::Info("CLSDB", "=== RTTI Vtable Scan Complete ===");
}

} // namespace Zolt::Diagoldze
