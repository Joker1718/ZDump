// Serie — 賽莉耶 (Living History Mage — SceneTree Walker)
// Walks the Godot SceneTree node hierarchy.

#include "Serie.h"
#include "Zoltraak.h"
#include "Flamme.h"
#include "Solliter.h"
#include "Grimoire.h"

namespace Zolt::Serie {

static uint64_t s_sceneTreeAddr = 0;

void Init(uint64_t sceneTreeAddr) {
    s_sceneTreeAddr = sceneTreeAddr;
    // TODO: Read SceneTree layout, locate root Window/Viewport
    Solliter::LogInit("Serie::Init — SceneTree address set");
}

void WalkTree(int /*maxDepth*/, const std::function<void(int depth, const NodeInfo& info)>& callback) {
    // TODO: Get root node
    // TODO: Recursively visit children using DynOff::NODE_CHILDREN
    // TODO: Resolve node names via Flamme, read class via vtable/ClassDB
    // TODO: Respect maxDepth limit
    (void)callback;
    Solliter::LogWalk("WalkTree: TODO — not yet implemented");
}

uint64_t GetRoot() {
    // TODO: Read SceneTree::root (Window*) from SceneTree singleton
    // TODO: Typically at a fixed offset from SceneTree base
    Solliter::LogWalk("GetRoot: TODO — not yet implemented");
    return 0;
}

std::vector<NodeInfo> GetChildren(uint64_t /*nodeAddr*/) {
    // TODO: Read Node::Data::children HashMap<StringName, Node*>
    // TODO: Iterate HashMap entries, resolve names
    Solliter::LogWalk("GetChildren: TODO — not yet implemented");
    return {};
}

} // namespace Zolt::Serie
