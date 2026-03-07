#pragma once

// Frieren — 芙莉蓮 (Protagonist — OffsetFinder)
// AOB signature scanning to locate Godot engine globals:
// ObjectDB, ClassDB, SceneTree, Engine singleton.
// Detects Godot version and validates/fixes DynOff offsets.

#include <cstdint>
#include <string>

namespace Zolt::Frieren {

// Find the ObjectDB singleton address via AOB signature
uint64_t FindObjectDB();

// Find the ClassDB singleton address via AOB signature
uint64_t FindClassDB();

// Find the SceneTree singleton address via AOB signature
uint64_t FindSceneTree();

// Find the Engine singleton address via AOB signature
uint64_t FindEngine();

// Detect the Godot version string from the Engine singleton
// Returns version string like "4.3.stable" or empty on failure
std::string DetectVersion();

// Validate DynOff offsets against known version tables
// and attempt to fix mismatches via probing
bool ValidateAndFixOffsets();

} // namespace Zolt::Frieren
