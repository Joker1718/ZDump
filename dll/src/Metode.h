#pragma once

// Metode — 梅特黛 (All-capable Analyst — CLR Hosting Bridge)
// Loads a .NET assembly into the game's already-running CoreCLR runtime
// via hostfxr, enabling managed reflection for type/field enumeration.
// This solves the name resolution problem for Godot + .NET games.

#include <cstdint>
#include <string>
#include <vector>

namespace Zolt::Metode {

// ============================================================
// Field layout information (mirrors ZoltReflector.FieldLayoutInfo)
// ============================================================
struct FieldInfo {
    std::string name;
    std::string typeName;
    int         offset;
    int         size;
    bool        isValueType;
    bool        isStatic;
};

// ============================================================
// Type layout information (mirrors ZoltReflector.TypeLayoutInfo)
// ============================================================
struct TypeLayout {
    std::string              fullName;
    std::string              assemblyName;
    std::string              baseTypeName;
    int                      instanceSize;
    bool                     isValueType;
    std::vector<FieldInfo>   fields;
};

// ============================================================
// Type summary for enumeration (mirrors ZoltReflector.TypeSummary)
// ============================================================
struct TypeSummary {
    std::string fullName;
    std::string assemblyName;
    int         fieldCount;
    uint64_t    methodTablePtr;
};

// ============================================================
// Public API
// ============================================================

// Initialize the CLR hosting bridge.
// Discovers hostfxr.dll in the game process, attaches to the running CoreCLR,
// and loads ZoltReflector.dll.
// Returns true on success.
bool Init();

// Check if the bridge is initialized and ready.
bool IsReady();

// Shut down the bridge (unload assembly, close hostfxr handle).
void Shutdown();

// Ping the managed side to verify it's alive.
// Returns true if MetodePing() returned the expected magic value.
bool Ping();

// ============================================================
// Reflection commands (JSON-based, delegated to managed code)
// ============================================================

// List all non-system assemblies loaded in the game.
std::vector<std::string> ListAssemblies();

// Enumerate all concrete types in the specified assembly (empty = all game assemblies).
std::vector<TypeSummary> EnumerateTypes(const std::string& assemblyName = "");

// Get detailed field layout for a specific type.
// typeName: fully qualified type name (e.g. "SlayTheSpire2.Creature")
TypeLayout GetTypeLayout(const std::string& typeName);

// Get the MethodTable pointer for a type by name.
uint64_t GetMethodTable(const std::string& typeName);

} // namespace Zolt::Metode
