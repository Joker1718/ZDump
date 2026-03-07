#pragma once

// DiAgolze — 黃金化 (Mahat's Gold — ClassDB Scanner)
// Scans the Godot ClassDB registry to extract class metadata:
// class info, property lists, method lists.

#include <cstdint>
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
    uint64_t                 nativePtr;  // ClassDB::ClassInfo* address
    std::vector<PropertyInfo> properties;
    std::vector<MethodInfo>   methods;
};

// Initialize with the ClassDB base address
void Init(uint64_t classDBAddr);

// Get full class info for a given class name
ClassInfo GetClassInfo(const char* className);

// Get property list for a class (including inherited)
std::vector<PropertyInfo> GetPropertyList(const char* className);

// Get method list for a class (including inherited)
std::vector<MethodInfo> GetMethodList(const char* className);

} // namespace Zolt::DiAgolze
