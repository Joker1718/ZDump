#pragma once

// DiAgolze — 黃金化 (Mahat's Gold — ClassDB Scanner)
// Scans the Godot ClassDB registry to extract class metadata:
// class info, property lists, method lists.
// Traverses the ClassDB HashMap<StringName, ClassInfo> via linked list.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Zolt::DiAgolze {

// Property descriptor
struct PropertyInfo {
    std::string name;
    std::string className;   // owning class
    int         type;        // Variant::Type
    int         offset;      // memory offset within instance
    std::string hint;        // property hint string
};

// Method descriptor
struct MethodInfo {
    std::string name;
    std::string returnType;
    int         argCount;
    // TODO: argument types, flags
};

// Class descriptor
struct ClassInfo {
    std::string              name;
    std::string              parentName;
    uint64_t                 nativePtr;  // ClassDB::ClassInfo* address in memory
    std::vector<PropertyInfo> properties;
    std::vector<MethodInfo>   methods;
};

// Initialize with the ClassDB base address (start of HashMap struct)
void Init(uint64_t classDBAddr);

// Get full class info for a given class name
ClassInfo GetClassInfo(const char* className);

// Get property list for a class (including inherited)
std::vector<PropertyInfo> GetPropertyList(const char* className);

// Get method list for a class (including inherited)
std::vector<MethodInfo> GetMethodList(const char* className);

// Get total number of registered classes
int GetClassCount();

// Enumerate all registered classes via linked list traversal
void EnumerateClasses(const std::function<void(const std::string& name, uint64_t infoAddr)>& callback);

// Resolve an Object* address to its class name (via class map lookup)
// Named GetObjectClass to avoid collision with Windows GetClassName macro
std::string GetObjectClass(uint64_t objectAddr);

// One-shot diagnostics dump
void LogDiagnostics();

// Proactive RTTI scan: scan object addresses and pre-seed the vtable→class cache.
// Call after ObjectDB init to identify classes before any UI requests.
void ScanObjectVtables(const std::vector<uint64_t>& objectAddrs);

} // namespace Zolt::DiAgolze
