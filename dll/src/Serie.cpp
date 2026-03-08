// Serie — 賽莉耶 (Living History Mage — SceneTree Walker)
// Walks the Godot SceneTree node hierarchy.
//
// SceneTree layout:
//   SceneTree inherits MainLoop which inherits Object.
//   SceneTree::root (Window*) is at some offset after the Object base fields.
//   The offset varies by Godot version — must be probed at runtime.
//
// Node::Data layout (partial):
//   String scene_file_path
//   Node* parent
//   Node* owner
//   HashMap<StringName, Node*> children
//   StringName name
//   SceneTree* tree
//
// We probe for the root Window by scanning SceneTree memory for a pointer
// whose target has a vtable in module range and whose StringName name = "root".

#include "Serie.h"
#include "DiAgolze.h"
#include "Zoltraak.h"
#include "Flamme.h"
#include "Solliter.h"
#include "Grimoire.h"

#include <atomic>

namespace Zolt::Serie {

// Cached state
static uint64_t s_sceneTreeAddr = 0;  // Address of SceneTree singleton pointer variable
static uint64_t s_sceneTree     = 0;  // Dereferenced SceneTree instance
static uint64_t s_rootNode      = 0;  // Root Window/Viewport node

// Module range for vtable validation
static uint64_t s_moduleBase = 0;
static uint64_t s_moduleSize = 0;

static void EnsureModuleInfo() {
    if (!s_moduleBase) {
        s_moduleBase = Zoltraak::GetModuleBase();
        s_moduleSize = Zoltraak::GetModuleSize();
    }
}

// Check if a pointer looks like a valid Godot Object (heap-allocated, vtable in module range)
static bool IsValidObject(uint64_t addr) {
    if (!addr || !Zoltraak::IsReadable(addr)) return false;
    // Real Objects are heap-allocated — reject pointers within the module's own address range
    // (these are .rdata/.data/.bss pointers that can accidentally pass the vtable check)
    if (addr >= s_moduleBase && addr < s_moduleBase + s_moduleSize) return false;
    uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(addr, 0);
    return vtable && vtable >= s_moduleBase && vtable < s_moduleBase + s_moduleSize;
}

// Try to read a Node's StringName name from its memory.
// We don't know the exact offset yet, so we probe multiple locations.
// Node inherits Object (which has ~0x50-0x100 bytes of base fields depending on version),
// then Node::Data starts at some offset.
// Within Data, the 'name' StringName is after parent, owner, children HashMap.
static std::string ProbeNodeName(uint64_t nodeAddr) {
    if (!nodeAddr) return "";

    // Quick check for known NODE_NAME offset
    if (DynOff::NODE_NAME >= 0) {
        std::string name = Flamme::Resolve(nodeAddr + DynOff::NODE_NAME);
        if (!name.empty()) return name;
    }

    // Probe: scan for StringName fields that resolve to plausible names.
    // A StringName is 8 bytes containing a _Data* pointer.
    for (int off = 0x80; off <= 0x500; off += 8) {
        uint64_t candidate = Zoltraak::ReadSafe<uint64_t>(nodeAddr + off, 0);
        if (!candidate || candidate < 0x10000) continue;
        if (!Zoltraak::IsReadable(candidate)) continue;

        // Check if it's a _Data* by trying to resolve it
        std::string name = Flamme::ResolveAt(candidate);
        if (!name.empty() && name.size() >= 1 && name.size() <= 64) {
            return name;
        }
    }

    return "";
}

// Probe for the root Window node within SceneTree
// SceneTree inherits Object → MainLoop → SceneTree, so the root pointer
// can be at a large offset (0x100-0xC00+) depending on version.
// Godot 4.3+ has ProcessGroup, PagedAllocator, CallQueue, etc. before root.
static uint64_t ProbeRootNode(uint64_t sceneTree) {
    if (!sceneTree) return 0;
    EnsureModuleInfo();
    LOG_TIMED("STREE", "ProbeRootNode");

    // If we already know the offset, use it
    if (DynOff::STREE_ROOT_OFFSET >= 0) {
        uint64_t root = Zoltraak::ReadSafe<uint64_t>(
            sceneTree + DynOff::STREE_ROOT_OFFSET, 0);
        if (IsValidObject(root)) return root;
    }

    Solliter::Debug("STREE", "Probing for root node in SceneTree at %p (range 0x40-0x%X)",
        reinterpret_cast<void*>(sceneTree), Grimoire::STREE_PROBE_MAX);

    // Diagnostic hexdump: dump key memory regions of SceneTree
    Solliter::Info("STREE", "SceneTree hexdump (first 0x80 bytes at %p):",
        reinterpret_cast<void*>(sceneTree));
    Solliter::HexDump("STREE", sceneTree, 0x80);
    Solliter::Info("STREE", "SceneTree hexdump (0x700-0x900, likely root area):");
    Solliter::HexDump("STREE", sceneTree + 0x700, 0x200);

    // Collect all Object pointers found
    struct ObjCandidate { int offset; uint64_t addr; std::string name; };
    std::vector<ObjCandidate> candidates;

    // Extended range: SceneTree has large fields before root (ProcessGroup, CallQueue, etc.)
    for (int off = 0x40; off <= Grimoire::STREE_PROBE_MAX; off += 8) {
        uint64_t candidate = Zoltraak::ReadSafe<uint64_t>(sceneTree + off, 0);
        if (!IsValidObject(candidate)) continue;

        // Avoid self-references (SceneTree pointing to itself)
        if (candidate == sceneTree) continue;

        std::string name = ProbeNodeName(candidate);

        if (name == "root") {
            DynOff::STREE_ROOT_OFFSET = off;
            Solliter::Info("STREE", "Found root node at SceneTree+0x%X = %p (name='root')",
                off, reinterpret_cast<void*>(candidate));
            return candidate;
        }

        if (candidates.size() < 25) {
            candidates.push_back({ off, candidate, name });
        }
    }

    // Log all Object candidates found for debugging
    if (!candidates.empty()) {
        Solliter::Info("STREE", "No 'root' name found. %d Object candidates in SceneTree:",
            static_cast<int>(candidates.size()));
        for (auto& c : candidates) {
            std::string cls = DiAgolze::GetObjectClass(c.addr);
            Solliter::Info("STREE", "  +0x%04X: %p name='%s' class='%s'",
                c.offset, reinterpret_cast<void*>(c.addr), c.name.c_str(), cls.c_str());
        }

        // Fallback: use the first candidate that has a Window-like class
        for (auto& c : candidates) {
            std::string cls = DiAgolze::GetObjectClass(c.addr);
            if (cls == "Window" || cls == "Viewport" || cls == "SubViewport") {
                DynOff::STREE_ROOT_OFFSET = c.offset;
                Solliter::Info("STREE", "Using %s at SceneTree+0x%X = %p as root (class match)",
                    cls.c_str(), c.offset, reinterpret_cast<void*>(c.addr));
                return c.addr;
            }
        }

        // Last resort: first Object pointer
        DynOff::STREE_ROOT_OFFSET = candidates[0].offset;
        Solliter::Warn("STREE", "Using first Object at SceneTree+0x%X = %p as root (name='%s', unconfirmed)",
            candidates[0].offset, reinterpret_cast<void*>(candidates[0].addr),
            candidates[0].name.c_str());
        return candidates[0].addr;
    }

    Solliter::Warn("STREE", "Could not find any Object pointers in SceneTree (probed 0x40-0x%X)",
        Grimoire::STREE_PROBE_MAX);
    return 0;
}

// Try to find and read children from a Node via HashMap<StringName, Node*>.
static void ReadChildrenHashMap(uint64_t nodeAddr, std::vector<NodeInfo>& out) {
    if (!nodeAddr) return;

    // Probe for HashMap in Node memory (after Object base)
    // A HashMap has: [PagedAllocator] + elements + hashes + head + tail + capIdx + numElems
    for (int off = 0x80; off <= 0x500; off += 8) {
        // Try treating 'off' as the start of a potential HashMap
        static const int ALLOC_SIZES[] = { 0x28, 0x20, 0x30 };
        for (int allocSize : ALLOC_SIZES) {
            int hoff = off + allocSize;
            if (hoff + 0x28 > 0x600) continue;

            uint64_t elements = Zoltraak::ReadSafe<uint64_t>(nodeAddr + hoff, 0);
            uint64_t hashes   = Zoltraak::ReadSafe<uint64_t>(nodeAddr + hoff + 0x08, 0);
            uint64_t head     = Zoltraak::ReadSafe<uint64_t>(nodeAddr + hoff + 0x10, 0);
            uint32_t capIdx   = Zoltraak::ReadSafe<uint32_t>(nodeAddr + hoff + 0x20, 0xFFFF);
            uint32_t numElems = Zoltraak::ReadSafe<uint32_t>(nodeAddr + hoff + 0x24, 0xFFFF);

            // Quick checks: small HashMap for children
            if (capIdx > 15) continue;
            if (numElems > 10000 || numElems == 0) continue;
            if (!elements || !hashes || !head) continue;
            if (!Zoltraak::IsReadable(elements) || !Zoltraak::IsReadable(head)) continue;

            // Walk linked list to read children
            uint64_t current = head;
            int count = 0;
            int maxIter = static_cast<int>(numElems) + 10;
            bool valid = true;

            while (current && count < maxIter) {
                if (!Zoltraak::IsReadable(current)) { valid = false; break; }

                // HashMapElement: next(8B) + prev(8B) + KeyValue(StringName(8B) + Node*(8B))
                uint64_t keyAddr   = current + 0x10;
                uint64_t childAddr = Zoltraak::ReadSafe<uint64_t>(current + 0x18, 0);

                if (!IsValidObject(childAddr)) { valid = false; break; }

                std::string childName = Flamme::Resolve(keyAddr);
                std::string childClass = DiAgolze::GetObjectClass(childAddr);

                NodeInfo ni;
                ni.addr       = childAddr;
                ni.name       = childName.empty() ? "?" : childName;
                ni.className  = childClass.empty() ? "?" : childClass;
                ni.childCount = 0;

                out.push_back(ni);
                count++;

                uint64_t next = Zoltraak::ReadSafe<uint64_t>(current, 0);
                if (next == current) break;
                current = next;
            }

            if (valid && count > 0) {
                if (DynOff::NODE_CHILDREN < 0) {
                    DynOff::NODE_CHILDREN = off;
                    Solliter::Info("STREE", "Children HashMap at Node+0x%X (allocSize=%d, %d children)",
                        off, allocSize, count);
                }
                return;
            } else {
                out.clear();
            }
        }
    }
}

void Init(uint64_t sceneTreeAddr) {
    s_sceneTreeAddr = sceneTreeAddr;
    s_sceneTree     = 0;
    s_rootNode      = 0;
    EnsureModuleInfo();

    if (!sceneTreeAddr) {
        Solliter::Warn("STREE", "Init called with null address");
        return;
    }

    // Dereference the singleton pointer
    s_sceneTree = Zoltraak::ReadSafe<uint64_t>(sceneTreeAddr, 0);
    if (!s_sceneTree || !Zoltraak::IsReadable(s_sceneTree)) {
        // Singleton pointer is NULL — Main::start() may not have created it yet.
        // Store the BSS address; GetRoot() will lazily retry the dereference.
        Solliter::Info("STREE", "SceneTree singleton at %p: null (deferred resolution via GetRoot())",
            reinterpret_cast<void*>(sceneTreeAddr));
        s_sceneTree = 0;
        return;
    }

    Solliter::Info("STREE", "SceneTree instance at %p",
        reinterpret_cast<void*>(s_sceneTree));

    // Probe for root node
    s_rootNode = ProbeRootNode(s_sceneTree);

    if (s_rootNode) {
        std::string rootName = ProbeNodeName(s_rootNode);
        Solliter::Info("STREE", "Root node: %p (name='%s')",
            reinterpret_cast<void*>(s_rootNode), rootName.c_str());
    }

    Solliter::Info("STREE", "Init complete (root=%p, rootOffset=0x%X)",
        reinterpret_cast<void*>(s_rootNode), DynOff::STREE_ROOT_OFFSET);
}

uint64_t GetRoot() {
    // Lazy deref: if we have the BSS address but SceneTree instance is NULL,
    // try to re-dereference. The game may have populated it since Init().
    if (!s_sceneTree && s_sceneTreeAddr) {
        uint64_t newTree = Zoltraak::ReadSafe<uint64_t>(s_sceneTreeAddr, 0);
        if (newTree && Zoltraak::IsReadable(newTree)) {
            EnsureModuleInfo();
            uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(newTree, 0);
            if (vtable >= s_moduleBase && vtable < s_moduleBase + s_moduleSize) {
                s_sceneTree = newTree;
                Solliter::Info("STREE", "Lazy deref: SceneTree populated at %p",
                    reinterpret_cast<void*>(s_sceneTree));
                // Also probe for root now
                s_rootNode = ProbeRootNode(s_sceneTree);
            }
        }
    }

    if (!s_rootNode && s_sceneTree) {
        s_rootNode = ProbeRootNode(s_sceneTree);
    }
    return s_rootNode;
}

std::vector<NodeInfo> GetChildren(uint64_t nodeAddr) {
    std::vector<NodeInfo> children;
    if (!nodeAddr) return children;

    EnsureModuleInfo();
    ReadChildrenHashMap(nodeAddr, children);
    return children;
}

// Recursive walk helper
static void WalkNode(uint64_t nodeAddr, int depth, int maxDepth,
                     const std::function<void(int depth, const NodeInfo& info)>& callback) {
    if (depth > maxDepth || !nodeAddr) return;

    std::string name = ProbeNodeName(nodeAddr);
    std::string className = DiAgolze::GetObjectClass(nodeAddr);

    std::vector<NodeInfo> children;
    if (depth < maxDepth) {
        ReadChildrenHashMap(nodeAddr, children);
    }

    NodeInfo info;
    info.addr       = nodeAddr;
    info.name       = name.empty() ? "?" : name;
    info.className  = className.empty() ? "?" : className;
    info.childCount = static_cast<int>(children.size());

    callback(depth, info);

    // Recurse into children
    for (auto& child : children) {
        WalkNode(child.addr, depth + 1, maxDepth, callback);
    }
}

void WalkTree(int maxDepth, const std::function<void(int depth, const NodeInfo& info)>& callback) {
    uint64_t root = GetRoot();
    if (!root) {
        Solliter::Warn("STREE", "WalkTree: no root node available");
        return;
    }

    Solliter::Info("STREE", "Walking tree from root=%p, maxDepth=%d",
        reinterpret_cast<void*>(root), maxDepth);

    WalkNode(root, 0, maxDepth, callback);
}

} // namespace Zolt::Serie
