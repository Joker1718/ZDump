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
#include "Ausetiese.h"
#include "Diagoldze.h"
#include "Zoltraak.h"
#include "Flamme.h"
#include "Solitaer.h"
#include "Grimoire.h"

#include <atomic>
#include <unordered_set>

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

// Cached ObjectDB address set for fast O(1) lookup during probing.
// Built once per ProbeRootNode call, cleared when done.
static std::unordered_set<uint64_t> s_objectDBSet;

static void BuildObjectDBSet() {
    s_objectDBSet.clear();
    s_objectDBSet.reserve(4096);
    Ausetiese::ForEach([](int /*index*/, uint64_t addr) {
        s_objectDBSet.insert(addr);
    });
}

// Check if a pointer looks like a valid Godot Object (heap-allocated, vtable in module range,
// AND present in ObjectDB for strong validation)
static bool IsValidObject(uint64_t addr) {
    if (!addr || !Zoltraak::IsReadable(addr)) return false;
    // Real Objects are heap-allocated — reject pointers within the module's own address range
    // (these are .rdata/.data/.bss pointers that can accidentally pass the vtable check)
    if (addr >= s_moduleBase && addr < s_moduleBase + s_moduleSize) return false;
    uint64_t vtable = Zoltraak::ReadSafe<uint64_t>(addr, 0);
    if (!vtable || vtable < s_moduleBase || vtable >= s_moduleBase + s_moduleSize) return false;
    // Strong validation: the Object must be registered in ObjectDB.
    // This eliminates MethodBind, Signal, and other internal structures with vtable pointers.
    if (!s_objectDBSet.empty()) {
        return s_objectDBSet.count(addr) > 0;
    }
    // Fallback if set not built (shouldn't happen during probe)
    return Ausetiese::FindByAddress(addr) >= 0;
}

// Try to read a Node's StringName name from its memory.
// We don't know the exact offset yet, so we probe multiple locations.
// Node inherits Object (which has ~0x50-0x100 bytes of base fields depending on version),
// then Node::Data starts at some offset.
// Within Data, the 'name' StringName is after parent, owner, children HashMap.
// Internal probe with option to cache offset (only safe for confirmed Nodes)
static std::string ProbeNodeNameInternal(uint64_t nodeAddr, bool allowCache) {
    if (!nodeAddr) return "";

    // Quick check for known NODE_NAME offset
    if (DynOff::NODE_NAME >= 0) {
        std::string name = Flamme::Resolve(nodeAddr + DynOff::NODE_NAME);
        if (!name.empty()) return name;
    }

    // Probe: scan for StringName fields that resolve to plausible node names.
    // Godot 4.5+ has larger Node objects — extend range to 0x800.
    for (int off = 0x80; off <= 0x800; off += 8) {
        uint64_t candidate = Zoltraak::ReadSafe<uint64_t>(nodeAddr + off, 0);
        if (!candidate || candidate < 0x10000) continue;
        if (!Zoltraak::IsReadable(candidate)) continue;

        std::string name = Flamme::ResolveAt(candidate);
        if (name.empty() || name.size() > 64) continue;

        // Node names: "root", "Camera3D", "@Label@123", "Player", "_internal"
        // Start with letter, '@', or '_'. Reject unlikely patterns.
        char first = name[0];
        bool plausibleNodeName = (first >= 'A' && first <= 'Z') ||
                                 (first >= 'a' && first <= 'z') ||
                                 first == '@' || first == '_';
        if (plausibleNodeName) {
            // Only cache when we know this is a confirmed Node object
            if (allowCache && DynOff::NODE_NAME < 0) {
                DynOff::NODE_NAME = off;
                Solitaer::Info("STREE", "Probed NODE_NAME at +0x%X = '%s'", off, name.c_str());
            }
            return name;
        }
    }

    return "";
}

// Probe for a node name — used by ProbeRootNode (confirmed Node-derived objects)
static std::string ProbeNodeName(uint64_t nodeAddr) {
    return ProbeNodeNameInternal(nodeAddr, true);
}

// Probe for any object's name — used by Fern for generic objects (no offset caching)
static std::string ProbeObjectName(uint64_t objAddr) {
    return ProbeNodeNameInternal(objAddr, false);
}

// Probe for the root Window node within SceneTree
// SceneTree inherits Object → MainLoop → SceneTree, so the root pointer
// can be at a large offset (0x100-0xC00+) depending on version.
// Godot 4.3+ has ProcessGroup, PagedAllocator, CallQueue, etc. before root.
//
// Scoring strategy:
//   1. Object with name=="root" AND Node-derived class → immediate accept
//   2. Node-derived Object with class Window/Viewport → strong candidate
//   3. Node-derived Object (any class) → weak candidate
//   4. Non-Node objects (NativeMenu, MethodBind, Signal, etc.) → rejected
static uint64_t ProbeRootNode(uint64_t sceneTree) {
    if (!sceneTree) return 0;
    EnsureModuleInfo();
    LOG_TIMED("STREE", "ProbeRootNode");

    // Build ObjectDB address set for fast O(1) validation
    BuildObjectDBSet();
    // Auto-clear on scope exit (any return path)
    struct SetCleaner { ~SetCleaner() { s_objectDBSet.clear(); } } _cleaner;
    Solitaer::Debug("STREE", "ObjectDB set: %d addresses for validation",
        static_cast<int>(s_objectDBSet.size()));

    // If we already know the offset, use it
    if (DynOff::STREE_ROOT_OFFSET >= 0) {
        uint64_t root = Zoltraak::ReadSafe<uint64_t>(
            sceneTree + DynOff::STREE_ROOT_OFFSET, 0);
        if (IsValidObject(root)) return root;
    }

    Solitaer::Debug("STREE", "Probing for root node in SceneTree at %p (range 0x40-0x%X)",
        reinterpret_cast<void*>(sceneTree), Grimoire::STREE_PROBE_MAX);

    // Diagnostic hexdump
    Solitaer::Info("STREE", "SceneTree hexdump (first 0x80 bytes at %p):",
        reinterpret_cast<void*>(sceneTree));
    Solitaer::HexDump("STREE", sceneTree, 0x80);

    // Collect all Object pointers found
    struct ObjCandidate {
        int offset;
        uint64_t addr;
        std::string name;
        std::string className;
        bool isNode;
        int score;  // higher = better candidate
    };
    std::vector<ObjCandidate> candidates;

    for (int off = 0x40; off <= Grimoire::STREE_PROBE_MAX; off += 8) {
        uint64_t candidate = Zoltraak::ReadSafe<uint64_t>(sceneTree + off, 0);
        if (!IsValidObject(candidate)) continue;
        if (candidate == sceneTree) continue;

        std::string cls = Diagoldze::GetObjectClass(candidate);
        bool isNode = Diagoldze::IsNodeDerived(cls);
        bool classUnknown = cls.empty();
        bool isKnownNonNode = !classUnknown && !isNode;

        // Skip objects with a known non-Node class (NativeMenu, MethodBind, etc.)
        if (isKnownNonNode) {
            if (candidates.size() < 25) {
                candidates.push_back({ off, candidate, "", cls, false, 0 });
            }
            continue;
        }

        // Probe name for Node-derived or unknown-class objects
        std::string name = ProbeNodeName(candidate);

        // Score: name="root" + Node → 100, Window/Viewport → 50,
        //        any confirmed Node → 10, unknown class → 5
        int score = classUnknown ? 5 : 10;
        if (isNode) {
            if (cls == "Window" || cls == "Viewport" || cls == "SubViewport")
                score = 50;
        }
        if (name == "root")
            score = 100;

        // Immediate accept: name="root"
        if (score == 100) {
            DynOff::STREE_ROOT_OFFSET = off;
            Solitaer::Info("STREE", "Found root node at SceneTree+0x%X = %p (name='root', class='%s')",
                off, reinterpret_cast<void*>(candidate), cls.c_str());
            return candidate;
        }

        if (candidates.size() < 25) {
            candidates.push_back({ off, candidate, name, cls, isNode, score });
        }
    }

    // Log all candidates for debugging
    if (!candidates.empty()) {
        Solitaer::Info("STREE", "No 'root'-named Node found. %d Object candidates in SceneTree:",
            static_cast<int>(candidates.size()));
        for (auto& c : candidates) {
            Solitaer::Info("STREE", "  +0x%04X: %p name='%s' class='%s' isNode=%s score=%d",
                c.offset, reinterpret_cast<void*>(c.addr),
                c.name.c_str(), c.className.c_str(),
                c.isNode ? "yes" : "no", c.score);
        }
    }

    // Select best candidate by score (highest wins, first among ties)
    ObjCandidate* best = nullptr;
    for (auto& c : candidates) {
        if (c.score > 0 && (!best || c.score > best->score)) {
            best = &c;
        }
    }

    if (best && best->score >= 10) {
        // Only accept if at least a confirmed Node class (score >= 10).
        // Score 5 = unknown class Object pointer — too unreliable, fall through to ObjectDB.
        DynOff::STREE_ROOT_OFFSET = best->offset;
        Solitaer::Info("STREE", "Selected root at SceneTree+0x%X = %p (name='%s', class='%s', score=%d)",
            best->offset, reinterpret_cast<void*>(best->addr),
            best->name.c_str(), best->className.c_str(), best->score);
        return best->addr;
    }

    int skippedNonNode = 0;
    for (auto& c : candidates) {
        if (c.score == 0) skippedNonNode++;
    }
    Solitaer::Warn("STREE", "Could not find root in SceneTree memory (probed 0x40-0x%X, %d non-Node objects skipped)",
        Grimoire::STREE_PROBE_MAX, skippedNonNode);

    // === Fallback Strategy: Search ObjectDB for root Window ===
    // If SceneTree memory scan fails, look for a Window/Viewport object
    // that has name "root" — this is always the SceneTree root node.
    Solitaer::Info("STREE", "Fallback: searching ObjectDB for root Window/Viewport...");
    uint64_t fallbackRoot = 0;
    Ausetiese::ForEachEx([&](int /*index*/, uint64_t objAddr) -> bool {
        std::string cls = Diagoldze::GetObjectClass(objAddr);
        if (cls != "Window" && cls != "Viewport" && cls != "SubViewport") return true;
        std::string name = ProbeObjectName(objAddr);
        if (name == "root") {
            Solitaer::Info("STREE", "  Found root via ObjectDB: %p class='%s'",
                reinterpret_cast<void*>(objAddr), cls.c_str());
            fallbackRoot = objAddr;
            return false; // stop
        }
        return true; // continue
    });

    if (fallbackRoot) {
        // Try to determine SceneTree offset for this root (scan back for the pointer)
        for (int off = 0x40; off <= Grimoire::STREE_PROBE_MAX; off += 8) {
            uint64_t val = Zoltraak::ReadSafe<uint64_t>(sceneTree + off, 0);
            if (val == fallbackRoot) {
                DynOff::STREE_ROOT_OFFSET = off;
                Solitaer::Info("STREE", "  Root pointer at SceneTree+0x%X (validated via ObjectDB fallback)", off);
                break;
            }
        }
        return fallbackRoot;
    }

    Solitaer::Warn("STREE", "Fallback also failed — no 'root' Window/Viewport in ObjectDB");
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
                std::string childClass = Diagoldze::GetObjectClass(childAddr);

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
                    Solitaer::Info("STREE", "Children HashMap at Node+0x%X (allocSize=%d, %d children)",
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
        Solitaer::Warn("STREE", "Init called with null address");
        return;
    }

    // Dereference the singleton pointer
    s_sceneTree = Zoltraak::ReadSafe<uint64_t>(sceneTreeAddr, 0);
    if (!s_sceneTree || !Zoltraak::IsReadable(s_sceneTree)) {
        // Singleton pointer is NULL — Main::start() may not have created it yet.
        // Store the BSS address; GetRoot() will lazily retry the dereference.
        Solitaer::Info("STREE", "SceneTree singleton at %p: null (deferred resolution via GetRoot())",
            reinterpret_cast<void*>(sceneTreeAddr));
        s_sceneTree = 0;
        return;
    }

    Solitaer::Info("STREE", "SceneTree instance at %p",
        reinterpret_cast<void*>(s_sceneTree));

    // Probe for root node
    s_rootNode = ProbeRootNode(s_sceneTree);

    if (s_rootNode) {
        std::string rootName = ProbeNodeName(s_rootNode);
        Solitaer::Info("STREE", "Root node: %p (name='%s')",
            reinterpret_cast<void*>(s_rootNode), rootName.c_str());
    }

    Solitaer::Info("STREE", "Init complete (root=%p, rootOffset=0x%X)",
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
                Solitaer::Info("STREE", "Lazy deref: SceneTree populated at %p",
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

std::string GetNodeName(uint64_t nodeAddr) {
    // Use the non-caching variant — callers may pass non-Node objects
    // (e.g. Fern's ReadObjectName for get_object_list).
    // NODE_NAME offset is only cached from confirmed Node-derived objects
    // during ProbeRootNode/ReadChildrenHashMap.
    return ProbeObjectName(nodeAddr);
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
    std::string className = Diagoldze::GetObjectClass(nodeAddr);

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
        Solitaer::Warn("STREE", "WalkTree: no root node available");
        return;
    }

    Solitaer::Info("STREE", "Walking tree from root=%p, maxDepth=%d",
        reinterpret_cast<void*>(root), maxDepth);

    WalkNode(root, 0, maxDepth, callback);
}

} // namespace Zolt::Serie
