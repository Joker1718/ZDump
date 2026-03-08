#pragma once

// GodotSig — Grimoire spell page (AOB patterns for Godot Engine)
// Centralized AOB signature database for ObjectDB, ClassDB, SceneTree, Engine.
// All signatures stored as constexpr POD in .rdata.
// Modeled after UE5CEDumper Signatures.h with Godot-specific patterns.
//
// Pattern priority convention:
//   0    = MSVC mangled symbol exports (O(1) lookup via GetProcAddress)
//   10   = Partial symbol prefix (export table scan)
//   50+  = AOB byte patterns (for stripped release builds)
//   60+  = Multi-match AOB (finds ALL matches, validates each)
//   80+  = .rdata string scans (version detection, etc.)

#include <cstdint>

// ============================================================
// Enums
// ============================================================

// Which Godot global this pattern targets
enum class GodotTarget : uint8_t {
    ObjectDB  = 0,   // ObjectDB::object_slots sparse array
    ClassDB   = 1,   // ClassDB::classes HashMap<StringName, ClassInfo>
    SceneTree = 2,   // SceneTree::singleton static pointer
    Engine    = 3,   // Engine::singleton_ptr static pointer
};

// How to resolve the match address to the final pointer
enum class GodotResolve : uint8_t {
    SymbolDirect  = 0,   // MSVC mangled symbol -> address IS the variable
    SymbolPrefix  = 1,   // Partial symbol match via PE export table iteration
    RipDirect     = 2,   // AOB match -> RIP-relative -> target address (direct)
    RipDeref      = 3,   // AOB match -> RIP-relative -> deref once (ptr-to-ptr)
    RdataScan     = 4,   // Scan .rdata for byte sequence (for string matching)
    MultiMatch    = 5,   // Find ALL matches, resolve+validate each (for short patterns)
};

// ============================================================
// Signature structure
// ============================================================

struct GodotAobSig {
    const char*   id;           // Unique pattern ID (e.g. "OBJDB_SYM1")
    const char*   pattern;      // MSVC symbol name or AOB hex string
    GodotTarget   target;       // Which global this targets
    GodotResolve  resolve;      // Resolution strategy
    int           instrOffset;  // Byte offset to RIP instruction within match
    int           opcodeLen;    // Opcode bytes before 4-byte displacement
    int           totalLen;     // Total instruction length (typically 7 for mov/lea)
    int           adjustment;   // Post-resolution offset adjustment
    int           priority;     // Lower = tried first (0 = symbols, 50+ = AOB)
    const char*   notes;        // Human-readable description
};

namespace GodotSig {

// ============================================================
// Priority 0: MSVC Symbol Exports (fastest - O(1) via GetProcAddress)
// These work when the game exports debug symbols (common in Godot
// development builds and many release builds that don't strip exports).
// ============================================================

// ObjectDB::object_slots - static member variable
static constexpr GodotAobSig OBJDB_SYM1 = {
    "OBJDB_SYM1",
    "?object_slots@ObjectDB@@0PEAUObjectSlot@1@EA",
    GodotTarget::ObjectDB, GodotResolve::SymbolDirect,
    0, 0, 0, 0,
    0,
    "ObjectDB::object_slots static member (MSVC x64)"
};

// ClassDB::classes - static HashMap<StringName, ClassInfo>
static constexpr GodotAobSig CLASSDB_SYM1 = {
    "CLASSDB_SYM1",
    "?classes@ClassDB@@0V?$HashMap@VStringName@@UClassInfo@ClassDB@@@@A",
    GodotTarget::ClassDB, GodotResolve::SymbolDirect,
    0, 0, 0, 0,
    0,
    "ClassDB::classes static HashMap (MSVC x64)"
};

// SceneTree::singleton - static pointer to self
static constexpr GodotAobSig STREE_SYM1 = {
    "STREE_SYM1",
    "?singleton@SceneTree@@0PEAV1@EA",
    GodotTarget::SceneTree, GodotResolve::SymbolDirect,
    0, 0, 0, 0,
    0,
    "SceneTree::singleton static pointer (MSVC x64)"
};

// Engine::singleton_ptr - static pointer to Engine instance
static constexpr GodotAobSig ENGINE_SYM1 = {
    "ENGINE_SYM1",
    "?singleton_ptr@Engine@@0PEAV1@EA",
    GodotTarget::Engine, GodotResolve::SymbolDirect,
    0, 0, 0, 0,
    0,
    "Engine::singleton_ptr static pointer (MSVC x64)"
};

// ============================================================
// Priority 10: Partial MSVC mangled symbol variants
// ============================================================

static constexpr GodotAobSig OBJDB_SYM_P = {
    "OBJDB_SYM_P",
    "?object_slots@ObjectDB@@",
    GodotTarget::ObjectDB, GodotResolve::SymbolPrefix,
    0, 0, 0, 0,
    10,
    "ObjectDB::object_slots (partial mangled prefix)"
};

static constexpr GodotAobSig CLASSDB_SYM_P = {
    "CLASSDB_SYM_P",
    "?classes@ClassDB@@",
    GodotTarget::ClassDB, GodotResolve::SymbolPrefix,
    0, 0, 0, 0,
    10,
    "ClassDB::classes (partial mangled prefix)"
};

static constexpr GodotAobSig STREE_SYM_P = {
    "STREE_SYM_P",
    "?singleton@SceneTree@@",
    GodotTarget::SceneTree, GodotResolve::SymbolPrefix,
    0, 0, 0, 0,
    10,
    "SceneTree::singleton (partial mangled prefix)"
};

static constexpr GodotAobSig ENGINE_SYM_P = {
    "ENGINE_SYM_P",
    "?singleton_ptr@Engine@@",
    GodotTarget::Engine, GodotResolve::SymbolPrefix,
    0, 0, 0, 0,
    10,
    "Engine::singleton_ptr (partial mangled prefix)"
};

// ============================================================
// Priority 50: AOB patterns for stripped release builds
// These patterns are derived from Godot 4.x x64 compiled code.
//
// IMPORTANT: MSVC uses NOP padding (0F 1F ...) between functions,
// NOT int3 (CC CC ...) like some older compilers. Patterns must
// account for this.
// ============================================================

// ObjectDB slot access pattern (from Godot 4.4.1 official build)
// Loads object_slots pointer, converts index, multiplies by sizeof(ObjectSlot)=16:
//   mov rdx, [rip+disp]     ; load object_slots
//   mov eax, r9d             ; index parameter
//   shl rax, 4               ; multiply by 16 (ObjectSlot stride)
//   mov eax, [rdx+rax+4]    ; read slot.validator (offset 4)
static constexpr GodotAobSig OBJDB_AOB1 = {
    "OBJDB_AOB1",
    "48 8B 15 ?? ?? ?? ?? 44 89 C8 48 C1 E0 04 8B 44 02 04",
    GodotTarget::ObjectDB, GodotResolve::RipDirect,
    0, 3, 7, 0,
    50,
    "ObjectDB::get_instance - mov rdx,[rip+X]; mov eax,r9d; shl rax,4; mov eax,[rdx+rax+4]"
};

// ObjectDB slot access variant with different register assignment:
//   mov rcx, [rip+disp]     ; load object_slots
//   mov rdx, rbx            ; index
//   shl rdx, 4              ; multiply by 16
static constexpr GodotAobSig OBJDB_AOB2 = {
    "OBJDB_AOB2",
    "48 8B 0D ?? ?? ?? ?? 48 89 DA 48 C1 E2 04",
    GodotTarget::ObjectDB, GodotResolve::RipDirect,
    0, 3, 7, 0,
    51,
    "ObjectDB slot access - mov rcx,[rip+X]; mov rdx,rbx; shl rdx,4"
};

// ObjectDB flexible pattern: load + shl by 4 + add (generic register assignment)
//   mov rax, [rip+disp]     ; load object_slots
//   shl r??, 4              ; multiply by 16
//   add ...                 ; compute slot address
static constexpr GodotAobSig OBJDB_AOB3 = {
    "OBJDB_AOB3",
    "48 8B 05 ?? ?? ?? ?? 48 C1 ?? 04 48 01",
    GodotTarget::ObjectDB, GodotResolve::RipDirect,
    0, 3, 7, 0,
    52,
    "ObjectDB - mov rax,[rip+X]; shl ?,4; add (flexible registers)"
};

// ObjectDB with lea index calculation:
//   mov rax, [rip+disp]     ; load object_slots
//   lea rdx, [rdx+rdx*4]   ; index * 5 (or similar)
//   shl rdx, 4              ; stride adjustment
static constexpr GodotAobSig OBJDB_AOB4 = {
    "OBJDB_AOB4",
    "48 8B 05 ?? ?? ?? ?? 48 8D ?? ?? 48 C1 ?? 04",
    GodotTarget::ObjectDB, GodotResolve::RipDirect,
    0, 3, 7, 0,
    53,
    "ObjectDB - mov rax,[rip+X]; lea ?; shl ?,4"
};

// SceneTree / Engine singleton getter pattern (multi-match)
// Pattern: mov rax,[rip+disp]; ret; <padding>
//
// MSVC padding varies by version/config:
//   Release (modern MSVC): NOP (0F 1F 84 00 ...) or (66 90 / 90 90 ...)
//   Debug / /hotpatch:     int3 (CC CC CC CC ...)
//   MinGW/GCC:             NOP (90) or no padding
//
// We use separate patterns for each padding type and multi-match
// all of them to maximize compatibility.

// NOP padding (0F 1F ...) — official Godot MSVC release builds
static constexpr GodotAobSig STREE_AOB1 = {
    "STREE_AOB1",
    "48 8B 05 ?? ?? ?? ?? C3 0F 1F",
    GodotTarget::SceneTree, GodotResolve::MultiMatch,
    0, 3, 7, 0,
    60,
    "Singleton getter - mov rax,[rip+X]; ret; NOP (MSVC release)"
};

static constexpr GodotAobSig ENGINE_AOB1 = {
    "ENGINE_AOB1",
    "48 8B 05 ?? ?? ?? ?? C3 0F 1F",
    GodotTarget::Engine, GodotResolve::MultiMatch,
    0, 3, 7, 0,
    61,
    "Singleton getter - mov rax,[rip+X]; ret; NOP (MSVC release)"
};

// int3 padding (CC CC) — MSVC debug builds, /hotpatch, older MSVC
static constexpr GodotAobSig STREE_AOB2 = {
    "STREE_AOB2",
    "48 8B 05 ?? ?? ?? ?? C3 CC CC",
    GodotTarget::SceneTree, GodotResolve::MultiMatch,
    0, 3, 7, 0,
    62,
    "Singleton getter - mov rax,[rip+X]; ret; int3 (MSVC debug/hotpatch)"
};

static constexpr GodotAobSig ENGINE_AOB2 = {
    "ENGINE_AOB2",
    "48 8B 05 ?? ?? ?? ?? C3 CC CC",
    GodotTarget::Engine, GodotResolve::MultiMatch,
    0, 3, 7, 0,
    63,
    "Singleton getter - mov rax,[rip+X]; ret; int3 (MSVC debug/hotpatch)"
};

// 90 90 padding (single-byte NOPs) — some GCC/MinGW or alignment-only builds
static constexpr GodotAobSig STREE_AOB3 = {
    "STREE_AOB3",
    "48 8B 05 ?? ?? ?? ?? C3 90 90",
    GodotTarget::SceneTree, GodotResolve::MultiMatch,
    0, 3, 7, 0,
    64,
    "Singleton getter - mov rax,[rip+X]; ret; NOP*2 (MinGW/GCC)"
};

static constexpr GodotAobSig ENGINE_AOB3 = {
    "ENGINE_AOB3",
    "48 8B 05 ?? ?? ?? ?? C3 90 90",
    GodotTarget::Engine, GodotResolve::MultiMatch,
    0, 3, 7, 0,
    65,
    "Singleton getter - mov rax,[rip+X]; ret; NOP*2 (MinGW/GCC)"
};

// ============================================================
// Priority 54-56: Long getter patterns (Godot 4.0-4.2 and stripped builds)
// Older Godot versions use lazy-init singleton getters that check for null:
//   sub rsp, 28h; mov rax, [rip+X]; test rax, rax; jnz .ret
//   or: mov rax, [rip+X]; test rax, rax; jnz .ret
// These have many matches but MultiMatch + validator filters correctly.
// ============================================================

// Long getter with function prologue (more specific, tried first)
static constexpr GodotAobSig ENGINE_AOB4 = {
    "ENGINE_AOB4",
    "48 83 EC 28 48 8B 05 ?? ?? ?? ?? 48 85 C0 75",
    GodotTarget::Engine, GodotResolve::MultiMatch,
    4, 3, 7, 0,
    54,
    "Long getter - sub rsp,28h; mov rax,[rip+X]; test; jnz (Godot 4.0-4.2)"
};

static constexpr GodotAobSig STREE_AOB4 = {
    "STREE_AOB4",
    "48 83 EC 28 48 8B 05 ?? ?? ?? ?? 48 85 C0 75",
    GodotTarget::SceneTree, GodotResolve::MultiMatch,
    4, 3, 7, 0,
    54,
    "Long getter - sub rsp,28h; mov rax,[rip+X]; test; jnz (Godot 4.0-4.2)"
};

// Long getter without prologue (generic null-check singleton getter)
static constexpr GodotAobSig ENGINE_AOB5 = {
    "ENGINE_AOB5",
    "48 8B 05 ?? ?? ?? ?? 48 85 C0 75",
    GodotTarget::Engine, GodotResolve::MultiMatch,
    0, 3, 7, 0,
    55,
    "Long getter - mov rax,[rip+X]; test; jnz (generic null-check getter)"
};

static constexpr GodotAobSig STREE_AOB5 = {
    "STREE_AOB5",
    "48 8B 05 ?? ?? ?? ?? 48 85 C0 75",
    GodotTarget::SceneTree, GodotResolve::MultiMatch,
    0, 3, 7, 0,
    55,
    "Long getter - mov rax,[rip+X]; test; jnz (generic null-check getter)"
};

// Long getter with jz variant (inverted condition — jump if null to create)
static constexpr GodotAobSig ENGINE_AOB6 = {
    "ENGINE_AOB6",
    "48 8B 05 ?? ?? ?? ?? 48 85 C0 74",
    GodotTarget::Engine, GodotResolve::MultiMatch,
    0, 3, 7, 0,
    56,
    "Long getter - mov rax,[rip+X]; test; jz (inverted null-check getter)"
};

static constexpr GodotAobSig STREE_AOB6 = {
    "STREE_AOB6",
    "48 8B 05 ?? ?? ?? ?? 48 85 C0 74",
    GodotTarget::SceneTree, GodotResolve::MultiMatch,
    0, 3, 7, 0,
    56,
    "Long getter - mov rax,[rip+X]; test; jz (inverted null-check getter)"
};

// ClassDB access pattern - lea rcx, [rip+disp] followed by call
// In official builds, ClassDB methods load the HashMap address via LEA
static constexpr GodotAobSig CLASSDB_AOB1 = {
    "CLASSDB_AOB1",
    "48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 85 C0",
    GodotTarget::ClassDB, GodotResolve::MultiMatch,
    0, 3, 7, 0,
    60,
    "ClassDB access - lea rcx,[rip+X]; call ???; test rax,rax (multi-match)"
};

// ClassDB HashMap element iteration pattern:
// Some ClassDB functions iterate entries with a pattern like:
//   mov rax, [rip+disp]     ; load ClassDB::classes._data or related field
//   ... access HashMap internals ...
// The HashMap has capacity, num_elements, then pointers to data arrays
static constexpr GodotAobSig CLASSDB_AOB2 = {
    "CLASSDB_AOB2",
    "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 ?? 48 8B 0D",
    GodotTarget::ClassDB, GodotResolve::RipDirect,
    0, 3, 7, 0,
    61,
    "ClassDB - mov rax,[rip+X]; test rax,rax; jz; mov rcx,[rip+...] (data ptr load)"
};

// ClassDB alternative: load HashMap address via LEA into different register
// lea rdx, [rip+disp] ; &ClassDB::classes
static constexpr GodotAobSig CLASSDB_AOB3 = {
    "CLASSDB_AOB3",
    "48 8D 15 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 85 C0",
    GodotTarget::ClassDB, GodotResolve::MultiMatch,
    0, 3, 7, 0,
    62,
    "ClassDB access - lea rdx,[rip+X]; call ???; test rax,rax (multi-match)"
};

// ============================================================
// Priority 80: .rdata string scans (version detection)
// ============================================================

// "Godot Engine v" - version string in .rdata
static constexpr GodotAobSig VERSION_RDATA1 = {
    "VERSION_RDATA1",
    "47 6F 64 6F 74 20 45 6E 67 69 6E 65 20 76",
    GodotTarget::Engine, GodotResolve::RdataScan,
    0, 0, 0, 0,
    80,
    ".rdata scan for 'Godot Engine v' version string"
};

// "4.x.y.stable" version string pattern
static constexpr GodotAobSig VERSION_RDATA2 = {
    "VERSION_RDATA2",
    "34 2E ?? 2E ?? 2E 73 74 61 62 6C 65",
    GodotTarget::Engine, GodotResolve::RdataScan,
    0, 0, 0, 0,
    81,
    ".rdata scan for '4.?.?.stable' version string"
};

// "3.x.y.stable" version string pattern (Godot 3.x)
static constexpr GodotAobSig VERSION_RDATA3 = {
    "VERSION_RDATA3",
    "33 2E ?? 2E ?? 2E 73 74 61 62 6C 65",
    GodotTarget::Engine, GodotResolve::RdataScan,
    0, 0, 0, 0,
    82,
    ".rdata scan for '3.?.?.stable' version string (Godot 3.x)"
};

// "3.x.dev" version string pattern (Godot 3.x dev/custom builds)
static constexpr GodotAobSig VERSION_RDATA4 = {
    "VERSION_RDATA4",
    "33 2E ?? 2E 64 65 76",
    GodotTarget::Engine, GodotResolve::RdataScan,
    0, 0, 0, 0,
    83,
    ".rdata scan for '3.?.dev' version string (Godot 3.x dev builds)"
};

// ============================================================
// Pattern arrays (null-terminated) sorted by priority
// ============================================================

static constexpr const GodotAobSig* OBJECTDB_PATTERNS[] = {
    &OBJDB_SYM1,
    &OBJDB_SYM_P,
    &OBJDB_AOB1,
    &OBJDB_AOB2,
    &OBJDB_AOB3,
    &OBJDB_AOB4,
    nullptr
};

static constexpr const GodotAobSig* CLASSDB_PATTERNS[] = {
    &CLASSDB_SYM1,
    &CLASSDB_SYM_P,
    &CLASSDB_AOB1,
    &CLASSDB_AOB2,
    &CLASSDB_AOB3,
    nullptr
};

static constexpr const GodotAobSig* SCENETREE_PATTERNS[] = {
    &STREE_SYM1,
    &STREE_SYM_P,
    &STREE_AOB4,     // long getter with prologue (priority 54)
    &STREE_AOB5,     // long getter generic (priority 55)
    &STREE_AOB6,     // long getter jz variant (priority 56)
    &STREE_AOB1,     // simple getter NOP (priority 60)
    &STREE_AOB2,     // simple getter int3 (priority 62)
    &STREE_AOB3,     // simple getter NOP*2 (priority 64)
    nullptr
};

static constexpr const GodotAobSig* ENGINE_PATTERNS[] = {
    &ENGINE_SYM1,
    &ENGINE_SYM_P,
    &ENGINE_AOB4,    // long getter with prologue (priority 54)
    &ENGINE_AOB5,    // long getter generic (priority 55)
    &ENGINE_AOB6,    // long getter jz variant (priority 56)
    &ENGINE_AOB1,    // simple getter NOP (priority 60)
    &ENGINE_AOB2,    // simple getter int3 (priority 63)
    &ENGINE_AOB3,    // simple getter NOP*2 (priority 65)
    &VERSION_RDATA1, // "Godot Engine v" (priority 80)
    &VERSION_RDATA2, // "4.?.?.stable" (priority 81)
    &VERSION_RDATA3, // "3.?.?.stable" (priority 82)
    &VERSION_RDATA4, // "3.?.dev" (priority 83)
    nullptr
};

// Total pattern count (excluding null terminators)
static constexpr int TOTAL_PATTERNS =
    (sizeof(OBJECTDB_PATTERNS) / sizeof(OBJECTDB_PATTERNS[0]) - 1) +
    (sizeof(CLASSDB_PATTERNS) / sizeof(CLASSDB_PATTERNS[0]) - 1) +
    (sizeof(SCENETREE_PATTERNS) / sizeof(SCENETREE_PATTERNS[0]) - 1) +
    (sizeof(ENGINE_PATTERNS) / sizeof(ENGINE_PATTERNS[0]) - 1);

} // namespace GodotSig
