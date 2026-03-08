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
#include "Solliter.h"
#include "Grimoire.h"

#include <atomic>
#include <algorithm>

namespace Zolt::Ausetiese {

// Cached ObjectDB state
static uint64_t s_objectDBAddr   = 0;  // Address of object_slots pointer variable
static uint64_t s_slotsArray     = 0;  // Dereferenced: heap pointer to ObjectSlot[]
static int      s_slotMax        = 0;  // Max slot index
static int      s_cachedCount    = -1; // Cached valid object count (-1 = dirty)
static int      s_slotCountOff   = 0;  // Offset from objectDBAddr to slot_count
static int      s_slotMaxOff     = 0;  // Offset from objectDBAddr to slot_max

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
        s_slotCountOff = best.countOff;
        s_slotMaxOff   = best.maxOff;
        Solliter::Info("OBJDB", "Slot counters found: count=%u (at +0x%X), max=%u (at +0x%X)",
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
    }
}

void Init(uint64_t objectDBAddr) {
    s_objectDBAddr = objectDBAddr;
    s_cachedCount  = -1;
    EnsureModuleInfo();

    if (!objectDBAddr) {
        Solliter::Warn("OBJDB", "Init called with null address");
        return;
    }

    // Dereference to get the ObjectSlot array pointer
    s_slotsArray = Zoltraak::ReadSafe<uint64_t>(objectDBAddr, 0);
    if (!s_slotsArray || !Zoltraak::IsReadable(s_slotsArray)) {
        Solliter::Warn("OBJDB", "ObjectDB at %p: null or unreadable slots array",
            reinterpret_cast<void*>(objectDBAddr));
        return;
    }

    // Find slot_count and slot_max in nearby memory
    if (!ProbeSlotCounters(objectDBAddr)) {
        // Fallback: estimate slot_max by scanning slots until we find too many empty ones
        Solliter::Warn("OBJDB", "Could not find slot counters, estimating slot_max by scanning");
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
        Solliter::Info("OBJDB", "Estimated slot_max: %d (last valid slot around %d)",
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

    Solliter::Info("OBJDB", "Init: %d valid objects in %d slots, array at %p",
        s_cachedCount, s_slotMax,
        reinterpret_cast<void*>(s_slotsArray));
}

int GetObjectCount() {
    if (s_cachedCount < 0) {
        // Need to count
        int count = 0;
        ForEachEx([&](int, uint64_t) {
            count++;
            return true;
        });
        s_cachedCount = count;
    }
    return s_cachedCount;
}

int GetSlotMax() {
    RefreshState();
    return s_slotMax;
}

uint64_t GetObjectByIndex(int index) {
    if (!s_slotsArray || index < 0 || index >= s_slotMax) return 0;

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
    if (!s_slotsArray || s_slotMax <= 0) return false;
    EnsureModuleInfo();

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
        // that resolves to the target. This will be improved when DiAgolze
        // provides vtable-to-classname mapping.
        // TODO: Improve with DiAgolze::GetClassName integration
        (void)objAddr;
        return true; // continue scanning
    });

    (void)target;
    (void)result;
    Solliter::Debug("OBJDB", "FindByName('%s'): not fully implemented (needs DiAgolze)", name);
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

void LogDiagnostics() {
    static std::atomic<bool> s_done{false};
    if (s_done.exchange(true)) return; // One-shot

    Solliter::Info("OBJDB", "=== Ausetiese Diagnostics ===");
    Solliter::Info("OBJDB", "ObjectDB addr: %p", reinterpret_cast<void*>(s_objectDBAddr));
    Solliter::Info("OBJDB", "Slots array:   %p", reinterpret_cast<void*>(s_slotsArray));
    Solliter::Info("OBJDB", "Slot max:      %d", s_slotMax);
    Solliter::Info("OBJDB", "Valid objects:  %d", GetObjectCount());

    if (s_slotCountOff > 0) {
        uint32_t count = Zoltraak::ReadSafe<uint32_t>(s_objectDBAddr + s_slotCountOff, 0);
        uint32_t max   = Zoltraak::ReadSafe<uint32_t>(s_objectDBAddr + s_slotMaxOff, 0);
        Solliter::Info("OBJDB", "Raw counters: slot_count=%u (at +0x%X), slot_max=%u (at +0x%X)",
            count, s_slotCountOff, max, s_slotMaxOff);
    }

    // Show first 5 valid objects
    int shown = 0;
    ForEachEx([&](int index, uint64_t objAddr) {
        if (shown >= 5) return false;

        uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(objAddr, 0);
        uint64_t objId  = Zoltraak::ReadSafe<uint64_t>(
            objAddr + DynOff::OBJECT_INSTANCE_ID, 0);

        Solliter::Info("OBJDB", "  [%d] addr=%p vtable=%p objectId=%llu",
            index,
            reinterpret_cast<void*>(objAddr),
            reinterpret_cast<void*>(vtable),
            objId);

        shown++;
        return true;
    });
}

} // namespace Zolt::Ausetiese
