// Frieren — 芙莉蓮 (Protagonist — OffsetFinder)
// AOB signature scanning to locate Godot engine globals.

#include "Frieren.h"
#include "Zoltraak.h"
#include "Solliter.h"
#include "Grimoire.h"

namespace Zolt::Frieren {

uint64_t FindObjectDB() {
    // TODO: Define AOB pattern for ObjectDB::get_instance()
    // TODO: Scan .text section via Zoltraak::AOBScan
    // TODO: Resolve RIP-relative address to ObjectDB global
    Solliter::LogScan("FindObjectDB: TODO — not yet implemented");
    return 0;
}

uint64_t FindClassDB() {
    // TODO: Define AOB pattern for ClassDB singleton access
    // TODO: Scan and resolve RIP-relative address
    Solliter::LogScan("FindClassDB: TODO — not yet implemented");
    return 0;
}

uint64_t FindSceneTree() {
    // TODO: Define AOB pattern for SceneTree::get_singleton()
    // TODO: Scan and resolve RIP-relative address
    Solliter::LogScan("FindSceneTree: TODO — not yet implemented");
    return 0;
}

uint64_t FindEngine() {
    // TODO: Define AOB pattern for Engine::get_singleton()
    // TODO: Scan and resolve RIP-relative address
    Solliter::LogScan("FindEngine: TODO — not yet implemented");
    return 0;
}

std::string DetectVersion() {
    // TODO: Read Engine singleton -> get_version_info()
    // TODO: Parse major.minor.status string
    Solliter::LogOffsets("DetectVersion: TODO — not yet implemented");
    return "";
}

bool ValidateAndFixOffsets() {
    // TODO: Use detected version to select offset table
    // TODO: Validate DynOff values against known layouts
    // TODO: Probe unknown offsets (ScriptInstance, Node::Data, StringName::_Data)
    // TODO: Set DynOff::bOffsetsValidated on success
    Solliter::LogOffsets("ValidateAndFixOffsets: TODO — not yet implemented");
    return false;
}

} // namespace Zolt::Frieren
