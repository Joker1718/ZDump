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
// These are static uint32_t variables typically at +8, +12 or +8, +16 from object_slots.
static bool ProbeSlotCounters(uint64_t objectDBAddr) {
    // Strategy: scan nearby memory (within 64 bytes after object_slots pointer)
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

    // Common layouts: variables stored as sequential uint32_t fields
    static const int OFFSET_PAIRS[][2] = {
        {0x08, 0x0C},  // Packed: count(4B) + max(4B) right after pointer
        {0x08, 0x10},  // count at +8, max at +16 (with padding)
        {0x10, 0x14},  // count at +16, max at +20
        {0x0C, 0x10},  // count at +12, max at +16
        {0x10, 0x18},  // With more padding
    };

    Candidate best = {0, 0, 0, 0};

    for (auto& pair : OFFSET_PAIRS) {
        uint32_t count = Zoltraak::ReadSafe<uint32_t>(objectDBAddr + pair[0], 0);
        uint32_t max   = Zoltraak::ReadSafe<uint32_t>(objectDBAddr + pair[1], 0);

        if (count == 0 || max == 0) continue;
        if (count > 10000000 || max > 10000000) continue;
        if (max < count) continue;

        // Prefer the pair where count and max are most plausible
        if (count > best.count) {
            best = {pair[0], pair[1], count, max};
        }
    }

    if (best.count > 0) {
        // Validate: scan the slot array to check for objects beyond best.max.
        // If many objects exist past the candidate's max, the counters are wrong/stale.
        uint64_t slotsArray = Zoltraak::ReadSafe<uint64_t>(objectDBAddr, 0);
        if (slotsArray && Zoltraak::IsReadable(slotsArray)) {
            int beyondCount = 0;
            int emptyRun = 0;
            // Scan from best.max onward — if there are real objects beyond the reported max,
            // the counters are incorrect (likely reading wrong offsets).
            // Use a large emptyRun threshold (5000) to handle sparse allocation patterns.
            int startIdx = static_cast<int>(best.max);
            for (int i = startIdx; i < 500000 && emptyRun < 5000; ++i) {
                uintptr_t slotAddr = static_cast<uintptr_t>(slotsArray) +
                    static_cast<uintptr_t>(i) * DynOff::SLOT_STRIDE;
                uint64_t objPtr = Zoltraak::ReadSafe<uint64_t>(
                    slotAddr + DynOff::SLOT_OBJECT_OFFSET, 0);
                if (objPtr) {
                    uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(objPtr, 0);
                    if (vtable >= s_moduleBase && vtable < s_moduleBase + s_moduleSize) {
                        beyondCount++;
                        emptyRun = 0;
                    } else {
                        emptyRun++;
                    }
                } else {
                    emptyRun++;
                }
                // Early exit: clearly wrong
                if (beyondCount > 50) break;
            }

            if (beyondCount > 10) {
                Solitaer::Warn("OBJDB", "Slot counters rejected: candidate max=%u but found %d objects beyond that index (false positive at +0x%X/+0x%X)",
                    best.max, beyondCount, best.countOff, best.maxOff);
                return false;
            }
        }

        s_slotCountOff = best.countOff;
        s_slotMaxOff   = best.maxOff;
        Solitaer::Info("OBJDB", "Slot counters found: count=%u (at +0x%X), max=%u (at +0x%X)",
            best.count, best.countOff, best.max, best.maxOff);
        return true;
    }

    return false;
}

// Refresh s_slotsArray and s_slotMax from memory
static void RefreshState() {
    if (!s_objectDBAddr) return;

    s_slotsArray = Zoltraak::ReadSafe<uint64_t>(s_objectDBAddr, 0);

    if (s_slotMaxOff > 0) {
        s_slotMax = static_cast<int>(Zoltraak::ReadSafe<uint32_t>(
            s_objectDBAddr + s_slotMaxOff, 0));
    } else if (s_slotsArray && Zoltraak::IsReadable(s_slotsArray)) {
        // No counter offsets known — re-scan to find the current extent.
        // New objects may have been added since Init() (e.g. root Window created after startup).
        int emptyRun = 0;
        int maxFound = 0;
        int startFrom = s_slotMax > 0 ? s_slotMax - 100 : 0;
        if (startFrom < 0) startFrom = 0;
        for (int i = startFrom; i < 500000 && emptyRun < 1000; ++i) {
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
        // Fallback: estimate slot_max by scanning slots until we find too many empty ones
        Solitaer::Warn("OBJDB", "Could not find slot counters, estimating slot_max by scanning");
        int emptyRun = 0;
        int maxFound = 0;
        for (int i = 0; i < 500000 && emptyRun < 1000; ++i) {
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
        s_slotMax = static_cast<int>(Zoltraak::ReadSafe<uint32_t>(
            objectDBAddr + s_slotMaxOff, 0));
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
        // For 3.x, re-read the elements count from the HashMap
        if (s_objectDBAddr) {
            s_elements3x = Zoltraak::ReadSafe<uint32_t>(
                s_objectDBAddr + DynOff::HASHMAP_3X_ELEMENTS_OFF, s_elements3x);
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

    if (!s_is3x && s_slotCountOff > 0) {
        uint32_t count = Zoltraak::ReadSafe<uint32_t>(s_objectDBAddr + s_slotCountOff, 0);
        uint32_t max   = Zoltraak::ReadSafe<uint32_t>(s_objectDBAddr + s_slotMaxOff, 0);
        Solitaer::Info("OBJDB", "Raw counters: slot_count=%u (at +0x%X), slot_max=%u (at +0x%X)",
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
