// Ausetiese — 服從之秤 (Scales of Obedience — ObjectDB Enumeration)
// Enumerates the Godot ObjectDB sparse array.
//
// ObjectDB layout (in .bss/.data):
//   static ObjectSlot* object_slots;   // Heap pointer to sparse array
//   static uint32_t    slot_count;     // Current number of allocated slots
//   static uint32_t    slot_max;       // Maximum slot index ever used
//   static SpinLock    spin_lock;      // Thread safety (ignored for reads)
//
// ObjectSlot layout (16 bytes):
//   uint64_t metadata;     // validator(39b) + next_free(24b) + is_ref_counted(1b)
//   Object*  object;       // Pointer to the Object instance (null if slot is free)

#include "Ausetiese.h"
#include "Zoltraak.h"
#include "Flamme.h"
#include "Solitaer.h"
#include "Grimoire.h"

#include <atomic>
#include <algorithm>

namespace Zolt::Ausetiese {

// Cached ObjectDB state (4.x sparse-set slot array)
static uint64_t s_objectDBAddr   = 0;  // Address of object_slots pointer variable
static uint64_t s_slotsArray     = 0;  // Dereferenced: heap pointer to ObjectSlot[]
static int      s_slotMax        = 0;  // Max slot index
static int      s_cachedCount    = -1; // Cached valid object count (-1 = dirty)
static int      s_slotCountOff   = 0;  // Offset from objectDBAddr to slot_count
static int      s_slotMaxOff     = 0;  // Offset from objectDBAddr to slot_max

// 3.x ObjectDB state (bucket-chain HashMap<ObjectID, Object*>)
static bool     s_is3x           = false;
static uint64_t s_hashTable3x    = 0;
static uint8_t  s_power3x        = 0;
static uint32_t s_elements3x     = 0;

// Module range for vtable validation
static uint64_t s_moduleBase = 0;
static uint64_t s_moduleSize = 0;

static void EnsureModuleInfo() {
    if (!s_moduleBase) {
        s_moduleBase = Zoltraak::GetModuleBase();
        s_moduleSize = Zoltraak::GetModuleSize();
    }
}

// Try to find slot_count and slot_max near the object_slots pointer.
// These are static uint32_t variables that may be at positive OR negative offsets
// from object_slots, depending on Godot version and compiler layout.
//
// In Godot 4.2 source (core/object/object.cpp), the declaration order is:
//   static SafeNumeric<uint32_t> slot_count;   // before object_slots
//   static SafeNumeric<uint32_t> slot_max;     // before object_slots
//   static ObjectSlot *object_slots = nullptr;  // ← our reference point
// So counters are typically at NEGATIVE offsets (-8, -4) from object_slots.
static bool ProbeSlotCounters(uint64_t objectDBAddr) {
    // Strategy: scan nearby memory (both before and after object_slots pointer)
    // for plausible (slot_count, slot_max) pairs where:
    //   - slot_count > 0 (some objects exist)
    //   - slot_max >= slot_count
    //   - slot_max < 10000000 (reasonable upper bound)
    //   - slot_count < 10000000

    struct Candidate {
        int countOff;
        int maxOff;
        uint32_t count;
        uint32_t max;
    };

    // Check both negative offsets (Godot 4.2+ where counters are declared
    // BEFORE object_slots) and positive offsets (other layouts/versions).
    static const int OFFSET_PAIRS[][2] = {
        // Negative offsets: counters declared before object_slots pointer
        {-8,  -4},    // slot_count(4B) + slot_max(4B) immediately before pointer
        {-12, -8},    // With SpinLock(4B) between counters and pointer
        {-16, -12},   // With padding/alignment
        {-16, -8},    // slot_count at -16, slot_max at -8 (8B alignment each)
        {-12, -4},    // slot_count at -12, slot_max at -4
        {-20, -16},   // Further negative (SpinLock + padding before counters)
        // Positive offsets: counters declared after object_slots pointer
        {0x08, 0x0C},  // Packed: count(4B) + max(4B) right after pointer
        {0x08, 0x10},  // count at +8, max at +16 (with padding)
        {0x10, 0x14},  // count at +16, max at +20
        {0x0C, 0x10},  // count at +12, max at +16
        {0x10, 0x18},  // With more padding
    };

    Candidate best = {0, 0, 0, 0};

    for (auto& pair : OFFSET_PAIRS) {
        // Cast to intptr_t for safe signed arithmetic (negative offsets)
        uint64_t countAddr = static_cast<uint64_t>(static_cast<intptr_t>(objectDBAddr) + pair[0]);
        uint64_t maxAddr   = static_cast<uint64_t>(static_cast<intptr_t>(objectDBAddr) + pair[1]);
        uint32_t count = Zoltraak::ReadSafe<uint32_t>(countAddr, 0);
        uint32_t max   = Zoltraak::ReadSafe<uint32_t>(maxAddr, 0);

        if (count == 0 || max == 0) continue;
        if (count > 10000000 || max > 10000000) continue;
        if (max < count) continue;

        // Prefer the pair where count and max are most plausible
        if (count > best.count) {
            best = {pair[0], pair[1], count, max};
        }
    }

    if (best.count > 0) {
        // Validate by cross-checking: count actual valid objects within [0, max)
        // and compare with the candidate count value. If they roughly match,
        // the counters are correct.
        // NOTE: Do NOT scan beyond max — the slot array is allocated for exactly
        // 'max' entries. Reading past it hits unrelated heap memory where random
        // pointers can pass vtable checks (false positives that caused incorrect
        // rejection in previous approaches).
        uint64_t slotsArray = Zoltraak::ReadSafe<uint64_t>(objectDBAddr, 0);
        if (slotsArray && Zoltraak::IsReadable(slotsArray)) {
            int actualCount = 0;
            // Sample slots within [0, max) to count valid objects.
            // For large arrays, sample evenly to keep this fast.
            int maxIdx = static_cast<int>(best.max);
            int step = (maxIdx > 10000) ? (maxIdx / 5000) : 1;
            int sampled = 0;
            for (int i = 0; i < maxIdx && sampled < 10000; i += step) {
                uintptr_t slotAddr = static_cast<uintptr_t>(slotsArray) +
                    static_cast<uintptr_t>(i) * DynOff::SLOT_STRIDE;
                uint64_t objPtr = Zoltraak::ReadSafe<uint64_t>(
                    slotAddr + DynOff::SLOT_OBJECT_OFFSET, 0);
                sampled++;
                if (objPtr) {
                    uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(objPtr, 0);
                    if (vtable >= s_moduleBase && vtable < s_moduleBase + s_moduleSize) {
                        actualCount++;
                    }
                }
            }

            // Extrapolate if we sampled with a step > 1
            if (step > 1) actualCount = actualCount * step;

            // Accept if actual count is within 50% of candidate count
            // (exact match not expected due to sampling and timing)
            int diff = (actualCount > static_cast<int>(best.count))
                ? (actualCount - static_cast<int>(best.count))
                : (static_cast<int>(best.count) - actualCount);
            bool valid = (diff < static_cast<int>(best.count / 2 + 100));

            Solitaer::Debug("OBJDB", "Slot counter cross-validation: candidate count=%u, actual≈%d, diff=%d, valid=%d",
                best.count, actualCount, diff, valid ? 1 : 0);

            if (!valid) {
                Solitaer::Warn("OBJDB", "Slot counters rejected: count=%u but found ≈%d objects in [0,%u) (offsets %+d/%+d)",
                    best.count, actualCount, best.max, best.countOff, best.maxOff);
                return false;
            }
        }

        s_slotCountOff = best.countOff;
        s_slotMaxOff   = best.maxOff;
        Solitaer::Info("OBJDB", "Slot counters found: count=%u (at %+d), max=%u (at %+d)",
            best.count, best.countOff, best.max, best.maxOff);
        return true;
    }

    return false;
}

// Refresh s_slotsArray and s_slotMax from memory
static void RefreshState() {
    if (!s_objectDBAddr) return;

    s_slotsArray = Zoltraak::ReadSafe<uint64_t>(s_objectDBAddr, 0);

    if (s_slotMaxOff != 0) {
        uint64_t maxAddr = static_cast<uint64_t>(static_cast<intptr_t>(s_objectDBAddr) + s_slotMaxOff);
        s_slotMax = static_cast<int>(Zoltraak::ReadSafe<uint32_t>(maxAddr, 0));
    } else if (s_slotsArray && Zoltraak::IsReadable(s_slotsArray)) {
        // No counter offsets known — re-scan to find the current extent.
        // New objects may have been added since Init() (e.g. root Window created after startup).
        int emptyRun = 0;
        int maxFound = 0;
        int startFrom = s_slotMax > 0 ? s_slotMax - 100 : 0;
        if (startFrom < 0) startFrom = 0;
        for (int i = startFrom; i < 500000 && emptyRun < 5000; ++i) {
            uintptr_t slotAddr = static_cast<uintptr_t>(s_slotsArray) +
                static_cast<uintptr_t>(i) * DynOff::SLOT_STRIDE;
            uint64_t objPtr = Zoltraak::ReadSafe<uint64_t>(
                slotAddr + DynOff::SLOT_OBJECT_OFFSET, 0);
            if (objPtr) {
                emptyRun = 0;
                maxFound = i + 1;
            } else {
                emptyRun++;
            }
        }
        if (maxFound + 100 > s_slotMax) {
            s_slotMax = maxFound + 100;
        }
    }
}

// ============================================================
// 3.x ObjectDB support (bucket-chain HashMap<ObjectID, Object*>)
// ============================================================

// Read 3.x HashMap fields from objectDBAddr
static bool ReadHashMapFields3x(uint64_t objectDBAddr) {
    s_hashTable3x = Zoltraak::ReadSafe<uint64_t>(
        objectDBAddr + DynOff::HASHMAP_3X_HASHTABLE_OFF, 0);
    s_power3x     = Zoltraak::ReadSafe<uint8_t>(
        objectDBAddr + DynOff::HASHMAP_3X_POWER_OFF, 0);
    s_elements3x  = Zoltraak::ReadSafe<uint32_t>(
        objectDBAddr + DynOff::HASHMAP_3X_ELEMENTS_OFF, 0);

    if (!s_hashTable3x || !Zoltraak::IsReadable(s_hashTable3x)) {
        Solitaer::Warn("OBJDB", "3x: invalid hash_table pointer");
        return false;
    }
    if (s_power3x < 4 || s_power3x > 20) {
        Solitaer::Warn("OBJDB", "3x: invalid power %u", s_power3x);
        return false;
    }

    Solitaer::Info("OBJDB", "3x HashMap: hashTable=%p power=%u elements=%u",
        reinterpret_cast<void*>(s_hashTable3x), s_power3x, s_elements3x);
    return true;
}

// Iterate all objects in the 3.x bucket-chain HashMap
static bool ForEachEx3x(const std::function<bool(int index, uint64_t objectAddr)>& callback) {
    if (!s_objectDBAddr) return false;

    // Re-read HashMap fields from live memory before each iteration.
    // Godot may resize the HashMap at any time (new hashTable pointer, new power).
    // Using stale pointers after a resize reads freed memory → missing objects.
    ReadHashMapFields3x(s_objectDBAddr);
    if (!s_hashTable3x) return false;

    int bucketCount = 1 << s_power3x;
    int index = 0;

    for (int b = 0; b < bucketCount; ++b) {
        uint64_t elem = Zoltraak::ReadSafe<uint64_t>(s_hashTable3x + b * 8, 0);

        while (elem) {
            if (!Zoltraak::IsReadable(elem)) break;

            // 3.x HashMap<ObjectID, Object*>::Element:
            //   hash(4B)+pad(4B)+next*(8B)+key(8B ObjectID)+value(8B Object*)
            uint64_t objPtr = Zoltraak::ReadSafe<uint64_t>(
                elem + DynOff::ELEM_3X_VALUE_OFF, 0);

            if (objPtr && Zoltraak::IsReadable(objPtr)) {
                // Validate vtable
                uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(objPtr, 0);
                if (vtable >= s_moduleBase && vtable < s_moduleBase + s_moduleSize) {
                    if (!callback(index, objPtr)) return false;
                    index++;
                }
            }

            uint64_t next = Zoltraak::ReadSafe<uint64_t>(
                elem + DynOff::ELEM_3X_NEXT_OFF, 0);
            if (next == elem) break;
            elem = next;
        }
    }
    return true;
}

// Initialize 3.x ObjectDB
static void Init3x(uint64_t objectDBAddr) {
    if (!ReadHashMapFields3x(objectDBAddr)) {
        Solitaer::Warn("OBJDB", "3x: failed to read HashMap fields");
        return;
    }

    // Count valid objects
    int validCount = 0;
    ForEachEx3x([&](int /*index*/, uint64_t /*addr*/) {
        validCount++;
        return true;
    });
    s_cachedCount = validCount;

    Solitaer::Info("OBJDB", "3x Init: %d valid objects (HashMap elements=%u, power=%u)",
        s_cachedCount, s_elements3x, s_power3x);
}

void Init(uint64_t objectDBAddr) {
    s_objectDBAddr = objectDBAddr;
    s_cachedCount  = -1;
    s_is3x = GodotVer::IsGodot3();
    EnsureModuleInfo();

    if (!objectDBAddr) {
        Solitaer::Warn("OBJDB", "Init called with null address");
        return;
    }

    if (s_is3x) {
        // 3.x: HashMap<ObjectID, Object*> (16B bucket-chain)
        Init3x(objectDBAddr);
        return;
    }

    // 4.x: sparse-set slot array
    // Dereference to get the ObjectSlot array pointer
    s_slotsArray = Zoltraak::ReadSafe<uint64_t>(objectDBAddr, 0);
    if (!s_slotsArray || !Zoltraak::IsReadable(s_slotsArray)) {
        Solitaer::Warn("OBJDB", "ObjectDB at %p: null or unreadable slots array",
            reinterpret_cast<void*>(objectDBAddr));
        return;
    }

    // Find slot_count and slot_max in nearby memory
    if (!ProbeSlotCounters(objectDBAddr)) {
        // Fallback: estimate slot_max by scanning slots until we find too many empty ones.
        // Use a large gap threshold (5000) — Godot reuses freed slots via a free list,
        // so gaps of 1000+ empty slots can occur when many objects are destroyed.
        Solitaer::Warn("OBJDB", "Could not find slot counters, estimating slot_max by scanning");
        int emptyRun = 0;
        int maxFound = 0;
        for (int i = 0; i < 500000 && emptyRun < 5000; ++i) {
            uintptr_t slotAddr = static_cast<uintptr_t>(s_slotsArray) +
                static_cast<uintptr_t>(i) * DynOff::SLOT_STRIDE;
            uint64_t objPtr = Zoltraak::ReadSafe<uint64_t>(
                slotAddr + DynOff::SLOT_OBJECT_OFFSET, 0);
            if (objPtr) {
                emptyRun = 0;
                maxFound = i + 1;
            } else {
                emptyRun++;
            }
        }
        s_slotMax = maxFound + 100; // Add buffer
        Solitaer::Info("OBJDB", "Estimated slot_max: %d (last valid slot around %d)",
            s_slotMax, maxFound);
    } else {
        uint64_t maxAddr = static_cast<uint64_t>(static_cast<intptr_t>(objectDBAddr) + s_slotMaxOff);
        s_slotMax = static_cast<int>(Zoltraak::ReadSafe<uint32_t>(maxAddr, 0));
    }

    // Count valid objects
    int validCount = 0;
    ForEachEx([&](int /*index*/, uint64_t /*addr*/) {
        validCount++;
        return true; // continue
    });
    s_cachedCount = validCount;

    Solitaer::Info("OBJDB", "Init: %d valid objects in %d slots, array at %p",
        s_cachedCount, s_slotMax,
        reinterpret_cast<void*>(s_slotsArray));
}

int GetObjectCount() {
    // Always recount — the game can create/destroy objects at any time.
    // Returning a stale cached value causes the UI to load incorrect counts.
    int count = 0;
    ForEachEx([&](int, uint64_t) {
        count++;
        return true;
    });
    s_cachedCount = count;
    return s_cachedCount;
}

int GetSlotMax() {
    if (s_is3x) {
        // For 3.x, re-read all HashMap fields (hashTable/power/elements may change on resize)
        if (s_objectDBAddr) {
            ReadHashMapFields3x(s_objectDBAddr);
        }
        return static_cast<int>(s_elements3x);
    }
    RefreshState();
    return s_slotMax;
}

uint64_t GetObjectByIndex(int index) {
    if (index < 0) return 0;

    if (s_is3x) {
        // 3.x: iterate via ForEachEx3x and count to the requested index
        uint64_t result = 0;
        int current = 0;
        ForEachEx3x([&](int /*idx*/, uint64_t objAddr) {
            if (current == index) {
                result = objAddr;
                return false; // stop
            }
            current++;
            return true;
        });
        return result;
    }

    // 4.x: direct slot array access
    if (!s_slotsArray || index >= s_slotMax) return 0;

    uintptr_t slotAddr = static_cast<uintptr_t>(s_slotsArray) +
        static_cast<uintptr_t>(index) * DynOff::SLOT_STRIDE;

    uint64_t objPtr = Zoltraak::ReadSafe<uint64_t>(
        slotAddr + DynOff::SLOT_OBJECT_OFFSET, 0);

    if (!objPtr) return 0;

    // Quick validate: vtable should be in module range
    uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(objPtr, 0);
    if (!vtable || vtable < s_moduleBase || vtable >= s_moduleBase + s_moduleSize) {
        return 0; // Invalid or freed object
    }

    return objPtr;
}

void ForEach(const std::function<void(int index, uint64_t objectAddr)>& callback) {
    ForEachEx([&](int index, uint64_t addr) {
        callback(index, addr);
        return true; // continue
    });
}

bool ForEachEx(const std::function<bool(int index, uint64_t objectAddr)>& callback) {
    EnsureModuleInfo();

    if (s_is3x) {
        return ForEachEx3x(callback);
    }

    // Refresh slot array and max to pick up newly-added objects
    RefreshState();

    // 4.x: sparse-set slot array
    if (!s_slotsArray || s_slotMax <= 0) return false;

    for (int i = 0; i < s_slotMax; ++i) {
        uintptr_t slotAddr = static_cast<uintptr_t>(s_slotsArray) +
            static_cast<uintptr_t>(i) * DynOff::SLOT_STRIDE;

        uint64_t objPtr = Zoltraak::ReadSafe<uint64_t>(
            slotAddr + DynOff::SLOT_OBJECT_OFFSET, 0);

        if (!objPtr) continue; // Empty/freed slot

        // Validate vtable in module range
        uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(objPtr, 0);
        if (!vtable || vtable < s_moduleBase || vtable >= s_moduleBase + s_moduleSize) continue;

        if (!callback(i, objPtr)) return false; // Early exit
    }

    return true;
}

uint64_t FindByName(const char* name) {
    if (!name || !name[0]) return 0;

    std::string target(name);
    uint64_t result = 0;

    ForEachEx([&](int /*index*/, uint64_t objAddr) {
        // Try reading the StringName from Object's known offsets
        // Object has a _get_class_name() virtual, but we can't call it
        // Instead, try to read the node name if it's a Node (via NODE_NAME offset)
        // This is a best-effort linear scan

        // For now, just match by checking if the object has a StringName
        // that resolves to the target. This will be improved when Diagoldze
        // provides vtable-to-classname mapping.
        // TODO: Improve with Diagoldze::GetClassName integration
        (void)objAddr;
        return true; // continue scanning
    });

    (void)target;
    (void)result;
    Solitaer::Debug("OBJDB", "FindByName('%s'): not fully implemented (needs Diagoldze)", name);
    return 0;
}

int FindByAddress(uint64_t addr) {
    if (!addr || !s_slotsArray || s_slotMax <= 0) return -1;

    int foundIndex = -1;
    ForEachEx([&](int index, uint64_t objAddr) {
        if (objAddr == addr) {
            foundIndex = index;
            return false; // stop
        }
        return true; // continue
    });

    return foundIndex;
}

AddressMatch FindByAddressRange(uint64_t addr) {
    AddressMatch result;
    if (!addr) return result;

    // Step 1: Try exact match first (fast path)
    int exactIdx = FindByAddress(addr);
    if (exactIdx >= 0) {
        result.slotIndex    = exactIdx;
        result.objectAddr   = addr;
        result.memberOffset = 0;
        return result;
    }

    // Step 2: Collect all valid object addresses and sort them
    struct ObjSlot { uint64_t addr; int index; };
    std::vector<ObjSlot> sorted;
    sorted.reserve(static_cast<size_t>(s_slotMax > 0 ? s_slotMax : 4096));

    ForEachEx([&](int index, uint64_t objAddr) {
        sorted.push_back({ objAddr, index });
        return true;
    });

    if (sorted.empty()) return result;

    std::sort(sorted.begin(), sorted.end(),
        [](const ObjSlot& a, const ObjSlot& b) { return a.addr < b.addr; });

    // Step 3: Binary search — find largest objAddr <= addr
    auto it = std::upper_bound(sorted.begin(), sorted.end(), addr,
        [](uint64_t val, const ObjSlot& slot) { return val < slot.addr; });

    if (it == sorted.begin()) return result; // addr is before all objects
    --it; // now it->addr <= addr

    uint64_t objAddr = it->addr;
    uint64_t offset  = addr - objAddr;

    // Sanity: cap max displacement at 64KB — anything larger is unlikely a member
    constexpr uint64_t MAX_OBJECT_SIZE = 0x10000;
    if (offset > MAX_OBJECT_SIZE) return result;

    // Additional validation: if there's a next object, verify addr is before it
    auto next = it + 1;
    if (next != sorted.end() && addr >= next->addr) return result; // shouldn't happen after upper_bound

    result.slotIndex    = it->index;
    result.objectAddr   = objAddr;
    result.memberOffset = static_cast<int>(offset);

    Solitaer::Debug("OBJDB", "FindByAddressRange: %p → Object at %p (slot %d, offset +0x%X)",
        reinterpret_cast<void*>(addr),
        reinterpret_cast<void*>(objAddr),
        result.slotIndex, result.memberOffset);

    return result;
}

bool ValidateObjectAddress(uint64_t addr) {
    if (!addr) return false;
    EnsureModuleInfo();
    if (!Zoltraak::IsReadable(addr)) return false;

    // Check 1: vtable pointer must be in module range
    uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(addr, 0);
    if (!vtable || vtable < s_moduleBase || vtable >= s_moduleBase + s_moduleSize)
        return false;

    // Check 2: first vtable entry (destructor) must be a code pointer in module
    uint64_t vfunc0 = Zoltraak::ReadSafe<uint64_t>(vtable, 0);
    if (!vfunc0 || vfunc0 < s_moduleBase || vfunc0 >= s_moduleBase + s_moduleSize)
        return false;

    // Check 3: second vtable entry must also be a code pointer
    uint64_t vfunc1 = Zoltraak::ReadSafe<uint64_t>(vtable + 8, 0);
    if (!vfunc1 || vfunc1 < s_moduleBase || vfunc1 >= s_moduleBase + s_moduleSize)
        return false;

    return true;
}

void LogDiagnostics() {
    static std::atomic<bool> s_done{false};
    if (s_done.exchange(true)) return; // One-shot

    Solitaer::Info("OBJDB", "=== Ausetiese Diagnostics ===");
    Solitaer::Info("OBJDB", "ObjectDB addr: %p", reinterpret_cast<void*>(s_objectDBAddr));
    Solitaer::Info("OBJDB", "Mode:          %s", s_is3x ? "3.x bucket-chain" : "4.x sparse-set");

    if (s_is3x) {
        Solitaer::Info("OBJDB", "3x HashMap:    hashTable=%p power=%u elements=%u",
            reinterpret_cast<void*>(s_hashTable3x), s_power3x, s_elements3x);
    } else {
        Solitaer::Info("OBJDB", "Slots array:   %p", reinterpret_cast<void*>(s_slotsArray));
        Solitaer::Info("OBJDB", "Slot max:      %d", s_slotMax);
    }
    Solitaer::Info("OBJDB", "Valid objects:  %d", GetObjectCount());

    if (!s_is3x && s_slotCountOff != 0) {
        uint64_t countAddr = static_cast<uint64_t>(static_cast<intptr_t>(s_objectDBAddr) + s_slotCountOff);
        uint64_t maxAddr   = static_cast<uint64_t>(static_cast<intptr_t>(s_objectDBAddr) + s_slotMaxOff);
        uint32_t count = Zoltraak::ReadSafe<uint32_t>(countAddr, 0);
        uint32_t max   = Zoltraak::ReadSafe<uint32_t>(maxAddr, 0);
        Solitaer::Info("OBJDB", "Raw counters: slot_count=%u (at %+d), slot_max=%u (at %+d)",
            count, s_slotCountOff, max, s_slotMaxOff);
    }

    // Show first 5 valid objects
    int shown = 0;
    ForEachEx([&](int index, uint64_t objAddr) {
        if (shown >= 5) return false;

        uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(objAddr, 0);
        uint64_t objId  = Zoltraak::ReadSafe<uint64_t>(
            objAddr + DynOff::OBJECT_INSTANCE_ID, 0);

        Solitaer::Info("OBJDB", "  [%d] addr=%p vtable=%p objectId=%llu",
            index,
            reinterpret_cast<void*>(objAddr),
            reinterpret_cast<void*>(vtable),
            objId);

        shown++;
        return true;
    });
}

} // namespace Zolt::Ausetiese
