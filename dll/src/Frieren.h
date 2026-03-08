#pragma once

// Frieren — 芙莉蓮 (Protagonist — OffsetFinder)
// AOB signature scanning to locate Godot engine globals:
// ObjectDB, ClassDB, SceneTree, Engine singleton.
// Detects Godot version and validates/fixes DynOff offsets.
// Ported from UE5CEDumper OffsetFinder with Godot-specific patterns.

#include <cstdint>
#include <string>
#include <functional>

namespace Zolt::Frieren {

// Callback for reporting scan progress
// phase: step number (1-7), text: human-readable status
using ScanProgressFn = std::function<void(int phase, const char* text)>;

// Results from a complete scan of all Godot engine globals
struct EnginePointers {
    // Discovered addresses (0 = not found)
    uintptr_t ObjectDB   = 0;   // ObjectSlot* array address
    uintptr_t SlotMax    = 0;   // slot_max variable address (nearby)
    uintptr_t ClassDB    = 0;   // HashMap<StringName, ClassInfo> address
    uintptr_t SceneTree  = 0;   // SceneTree* singleton address (holds pointer)
    uintptr_t Engine     = 0;   // Engine* singleton address (holds pointer)

    // Godot version
    std::string godotVersion;   // e.g. "4.3.stable"
    uint32_t    godotMajor = 0; // 4
    uint32_t    godotMinor = 0; // 3

    // PE hash for hint cache
    char peHash[17] = {0};

    // Winning pattern IDs (point into GodotSig constant strings)
    const char* objectDBPatternId  = nullptr;
    const char* classDBPatternId   = nullptr;
    const char* sceneTreePatternId = nullptr;
    const char* enginePatternId    = nullptr;

    // Scan method for each pointer ("aob", "symbol", "not_found")
    const char* objectDBMethod   = "not_found";
    const char* classDBMethod    = "not_found";
    const char* sceneTreeMethod  = "not_found";
    const char* engineMethod     = "not_found";

    // Per-target statistics
    int objectDBPatternsTried  = 0;
    int classDBPatternsTried   = 0;
    int sceneTreePatternsTried = 0;
    int enginePatternsTried    = 0;
};

// Scan and locate all global pointers.
// Returns true if at least ObjectDB was found.
bool FindAll(EnginePointers& out, ScanProgressFn progress = nullptr);

// Individual target finders (used by FindAll)
// hintPatternId: cached pattern ID to try first (from Himmel)
uintptr_t FindObjectDB(const char* hintPatternId = nullptr);
uintptr_t FindClassDB(const char* hintPatternId = nullptr);
uintptr_t FindSceneTree(const char* hintPatternId = nullptr);
uintptr_t FindEngine(const char* hintPatternId = nullptr);

// Detect Godot version string from Engine singleton or .rdata scan
std::string DetectVersion();

// Validate DynOff offsets against known version tables
// and attempt to fix mismatches via probing
bool ValidateAndFixOffsets();

// Get the cached EnginePointers from the last FindAll call
const EnginePointers& GetPointers();

} // namespace Zolt::Frieren
