/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Diagoldze — 黃金化 (Mahat's Gold — ClassDB Scanner)
// Scans the Godot ClassDB registry to extract class metadata:
// class info, property lists, method lists.
// Traverses the ClassDB HashMap<StringName, ClassInfo> via linked list.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Zolt::Diagoldze {

// Property descriptor
struct PropertyInfo {
    std::string name;
    std::string className;   // owning class
    int         type;        // Variant::Type
    int         offset;      // memory offset within instance (-1 if unknown)
    int         nativeSize = 0; // native access size in bytes (1, 4, 8) from ptrcall analysis
    int         bitShift = 0;  // right-shift for bitfield extraction (0 = none)
    int         bitMask  = 0;  // bitmask after shift (0 = use full value)
    uint64_t    getterMethodBind = 0; // MethodBind* for getter (ptrcall fallback when offset == -1)
    int         hint = 0;    // PropertyHint enum value
    std::string hintString;  // hint parameter string (e.g., "0,100,1" for Range)
    uint32_t    usage = 0;   // PROPERTY_USAGE_* flags
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

// Check if a class name inherits from Node (walks parent chain in ClassDB)
bool IsNodeDerived(const std::string& className);

// One-shot diagnostics dump
void LogDiagnostics();

// Proactive RTTI scan: scan object addresses and pre-seed the vtable→class cache.
// Call after ObjectDB init to identify classes before any UI requests.
void ScanObjectVtables(const std::vector<uint64_t>& objectAddrs);

// Count vtables in the cache that have empty class names (RTTI failed).
int GetUnresolvedVtableCount();

// Fallback vtable classification via static code analysis of ClassDB
// creation functions. Extracts vtable addresses from x86-64 LEA instructions
// in each class's creation_func — no game function calls needed.
// Use when RTTI is stripped (custom Godot builds).
void ClassifyUnresolvedVtables();

// Heiter-based vtable classification: uses Object::get_class() ptrcall
// on the game thread to identify classes for remaining unresolved vtables.
// Requires Heiter hook to be active. Call after ClassifyUnresolvedVtables.
void ClassifyVtablesViaHeiter();

} // namespace Zolt::Diagoldze
