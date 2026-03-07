#pragma once

// Serie — 賽莉耶 (Living History Mage — SceneTree Walker)
// Walks the Godot SceneTree node hierarchy.
// Retrieves root node, children, and recursive tree structure.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Zolt::Serie {

// Node descriptor for tree walking
struct NodeInfo {
    uint64_t    addr;       // Node* address
    std::string name;       // Node name (StringName resolved)
    std::string className;  // Class name
    int         childCount; // Number of children
};

// Initialize with the SceneTree singleton address
void Init(uint64_t sceneTreeAddr);

// Walk the scene tree recursively up to maxDepth levels
// Calls callback(depth, nodeInfo) for each node
void WalkTree(int maxDepth, const std::function<void(int depth, const NodeInfo& info)>& callback);

// Get the root node address (/root viewport)
uint64_t GetRoot();

// Get children of a node
std::vector<NodeInfo> GetChildren(uint64_t nodeAddr);

} // namespace Zolt::Serie
