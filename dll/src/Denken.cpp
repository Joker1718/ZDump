// Denken — 頓肯 (Analytical Magic — CLR Hosting Bridge)
// Uses hostfxr to load ZoltReflector.dll into the game's running CoreCLR.
// Communication: JSON commands via shared buffer.

#include "Denken.h"
#include "Solitaer.h"
#include "Grimoire.h"
#include "Zoltraak.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// hostfxr / coreclr delegate types (from vendor/godot-4.4.1/modules/mono/thirdparty/)
// Inlined here to avoid additional include paths.
typedef int32_t(__cdecl* hostfxr_initialize_for_runtime_config_fn)(
    const wchar_t* runtime_config_path,
    const void*    parameters,
    void**         host_context_handle);

typedef int32_t(__cdecl* hostfxr_get_runtime_delegate_fn)(
    const void* host_context_handle,
    int         type,
    void**      delegate);

typedef int32_t(__cdecl* hostfxr_close_fn)(
    const void* host_context_handle);

// coreclr delegate type enum
enum hostfxr_delegate_type {
    hdt_com_activation = 0,
    hdt_load_in_memory_assembly = 1,
    hdt_winrt_activation = 2,
    hdt_com_register = 3,
    hdt_com_unregister = 4,
    hdt_load_assembly_and_get_function_pointer = 5,
    hdt_get_function_pointer = 6,
};

// load_assembly_and_get_function_pointer signature
typedef int32_t(__stdcall* load_assembly_fn)(
    const wchar_t* assembly_path,
    const wchar_t* type_name,
    const wchar_t* method_name,
    const wchar_t* delegate_type_name,
    void*          reserved,
    void**         delegate_out);

// Managed entry point signatures
typedef int32_t(*reflect_fn)(char* buffer, int bufferLen);
typedef int32_t(*ping_fn)();

// Magic constant for UnmanagedCallersOnly methods
#define UNMANAGEDCALLERSONLY_METHOD ((const wchar_t*)(intptr_t)-1)

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Zolt::Denken {

// ============================================================
// Module state (guarded by s_mutex)
// ============================================================
static std::mutex   s_mutex;
static bool         s_ready = false;
static reflect_fn   s_reflectFn = nullptr;
static ping_fn      s_pingFn = nullptr;

// Shared buffer for JSON request/response (4 MB — large games have thousands of types)
static constexpr int BUFFER_SIZE = 4 * 1024 * 1024;
static char s_buffer[BUFFER_SIZE];

// ============================================================
// hostfxr discovery and initialization
// ============================================================

static HMODULE FindHostFxr()
{
    // First: check if hostfxr is already loaded in the process
    HMODULE hmod = GetModuleHandleW(L"hostfxr");
    if (hmod) {
        Solitaer::Info("DENKEN", "hostfxr already loaded in process");
        return hmod;
    }

    // Second: look for hostfxr.dll near the game executable
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();

    // Common locations for Godot .NET games:
    // 1. Same directory as exe
    // 2. dotnet/ subdirectory
    // 3. The dotnet install path
    std::vector<fs::path> candidates = {
        exeDir / L"hostfxr.dll",
    };

    // Search for dotnet runtime directories
    for (const auto& candidate : candidates) {
        if (fs::exists(candidate)) {
            hmod = LoadLibraryW(candidate.c_str());
            if (hmod) {
                Solitaer::Info("DENKEN", "Loaded hostfxr from: %ls",
                    candidate.c_str());
                return hmod;
            }
        }
    }

    Solitaer::Warn("DENKEN", "hostfxr.dll not found in process or game directory");
    return nullptr;
}

static fs::path FindRuntimeConfig()
{
    // Look for .runtimeconfig.json in the game directory tree.
    // Godot .NET games store .NET files in different locations:
    //   - Editor builds: GodotPlugins.runtimeconfig.json next to exe
    //   - Export builds: data_<name>_<platform>/ subdirectory (e.g. data_sts2_windows_x86_64/)
    //   - Some games: <GameName>.runtimeconfig.json next to exe

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();
    std::error_code ec;

    // Search exe directory first (editor builds, simple exports)
    for (const auto& entry : fs::directory_iterator(exeDir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto name = entry.path().filename().wstring();
        if (name.ends_with(L".runtimeconfig.json")) {
            Solitaer::Info("DENKEN", "Found runtime config: %ls", entry.path().c_str());
            return entry.path();
        }
    }

    // Search immediate subdirectories (Godot export: data_*/ subdirectory)
    for (const auto& dirEntry : fs::directory_iterator(exeDir, ec)) {
        if (!dirEntry.is_directory()) continue;
        auto dirName = dirEntry.path().filename().wstring();

        // Only search plausible .NET data directories (data_*, dotnet, etc.)
        // Skip controller_config, crashpad, etc.
        if (!dirName.starts_with(L"data_") && dirName != L"dotnet") continue;

        std::error_code ec2;
        for (const auto& entry : fs::directory_iterator(dirEntry.path(), ec2)) {
            if (!entry.is_regular_file()) continue;
            auto name = entry.path().filename().wstring();
            if (name.ends_with(L".runtimeconfig.json")) {
                Solitaer::Info("DENKEN", "Found runtime config in subdir: %ls", entry.path().c_str());
                return entry.path();
            }
        }
    }

    return {};
}

static fs::path FindReflectorAssembly()
{
    // Look for ZoltReflector.dll in these locations:
    // 1. Same directory as ZoltDumper.dll (our injected DLL)
    // 2. Same directory as game executable
    // 3. dist/ directory (development)

    HMODULE ourDll = nullptr;
    wchar_t dllPath[MAX_PATH] = {};

    // Get our DLL's path
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&FindReflectorAssembly),
        &ourDll);

    if (ourDll) {
        GetModuleFileNameW(ourDll, dllPath, MAX_PATH);
        auto dllDir = fs::path(dllPath).parent_path();
        auto candidate = dllDir / L"ZoltReflector.dll";
        if (fs::exists(candidate)) return candidate;
    }

    // Fallback: game exe directory
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    auto exeDir = fs::path(exePath).parent_path();
    auto candidate = exeDir / L"ZoltReflector.dll";
    if (fs::exists(candidate)) return candidate;

    return {};
}

// ============================================================
// JSON command helpers
// ============================================================

static std::string SendCommand(const json& request)
{
    std::lock_guard<std::mutex> lock(s_mutex);

    if (!s_reflectFn) return R"({"error":"bridge not initialized"})";

    std::string reqStr = request.dump();
    if (static_cast<int>(reqStr.size()) >= BUFFER_SIZE)
        return R"({"error":"request too large"})";

    memcpy(s_buffer, reqStr.c_str(), reqStr.size());

    int result = s_reflectFn(s_buffer, static_cast<int>(reqStr.size()));
    if (result < 0)
        return R"({"error":"managed call failed","code":)" + std::to_string(result) + "}";

    // Upper-bound check: result must not exceed buffer size
    if (result > BUFFER_SIZE)
        return R"({"error":"response exceeds buffer size"})";

    return std::string(s_buffer, result);
}

// ============================================================
// Public API implementation
// ============================================================

bool Init()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_ready) return true;

    // Only attempt on .NET games
    if (!DynOff::bIsDotNetGame.load(std::memory_order_relaxed)) {
        Solitaer::Info("DENKEN", "Not a .NET game, skipping CLR bridge init");
        return false;
    }

    Solitaer::Info("DENKEN", "Initializing CLR hosting bridge...");

    // Step 1: Find hostfxr
    HMODULE hHostFxr = FindHostFxr();
    if (!hHostFxr) return false;

    auto pfnInit = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
        GetProcAddress(hHostFxr, "hostfxr_initialize_for_runtime_config"));
    auto pfnGetDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
        GetProcAddress(hHostFxr, "hostfxr_get_runtime_delegate"));
    auto pfnClose = reinterpret_cast<hostfxr_close_fn>(
        GetProcAddress(hHostFxr, "hostfxr_close"));

    if (!pfnGetDelegate) {
        Solitaer::Warn("DENKEN", "Failed to resolve hostfxr_get_runtime_delegate");
        return false;
    }

    // Step 2: Get load_assembly delegate from the already-running CLR.
    //
    // For self-contained Godot .NET games (like STS2), the CLR is already
    // fully initialized by the game. Using hostfxr_initialize_for_runtime_config
    // fails because:
    //   - Game's config uses "includedFrameworks" → CoreHostIncompatibleConfig
    //   - Minimal config with "framework" → FrameworkMissingFailure (no system .NET)
    //
    // Solution: hostfxr_get_runtime_delegate with NULL handle.
    // When the runtime is already active, .NET 8+ returns the delegate from
    // the active host context without needing initialization.
    //
    // Strategy order:
    //   1. NULL handle (active context) — works for self-contained deployments
    //   2. hostfxr_initialize_for_runtime_config — works for framework-dependent
    void* hostCtx = nullptr;
    bool needClose = false;

    // Step 3: Get load_assembly_and_get_function_pointer delegate
    load_assembly_fn pfnLoadAssembly = nullptr;

    // Strategy 1: NULL handle — use active host context (self-contained games)
    Solitaer::Info("DENKEN", "Trying NULL handle (active context)...");
    int32_t rc = pfnGetDelegate(nullptr, hdt_load_assembly_and_get_function_pointer,
                                reinterpret_cast<void**>(&pfnLoadAssembly));
    Solitaer::Info("DENKEN", "NULL handle result: 0x%08X, delegate=%p", rc, pfnLoadAssembly);

    // Strategy 2: Initialize with game's runtimeconfig (framework-dependent games)
    if (rc < 0 || !pfnLoadAssembly) {
        pfnLoadAssembly = nullptr;
        fs::path configPath = FindRuntimeConfig();
        if (!configPath.empty() && pfnInit) {
            Solitaer::Info("DENKEN", "Trying runtime config: %ls", configPath.c_str());
            rc = pfnInit(configPath.c_str(), nullptr, &hostCtx);
            Solitaer::Info("DENKEN", "Init result: 0x%08X (ctx=%p)", rc, hostCtx);

            if (rc >= 0 && hostCtx) {
                needClose = true;
                rc = pfnGetDelegate(hostCtx, hdt_load_assembly_and_get_function_pointer,
                                    reinterpret_cast<void**>(&pfnLoadAssembly));
                Solitaer::Info("DENKEN", "Config delegate result: 0x%08X", rc);
            }
        }
    }

    if (rc < 0 || !pfnLoadAssembly) {
        Solitaer::Warn("DENKEN", "Failed to get load_assembly delegate: 0x%08X", rc);
        if (needClose && hostCtx && pfnClose) pfnClose(hostCtx);
        return false;
    }

    // Step 4: Find and load ZoltReflector.dll
    fs::path reflectorPath = FindReflectorAssembly();
    if (reflectorPath.empty()) {
        Solitaer::Warn("DENKEN", "ZoltReflector.dll not found");
        if (needClose && hostCtx && pfnClose) pfnClose(hostCtx);
        return false;
    }
    Solitaer::Info("DENKEN", "Loading reflector: %ls", reflectorPath.c_str());

    // Step 5: Get function pointers from managed assembly
    // Reflect entry point
    rc = pfnLoadAssembly(
        reflectorPath.c_str(),
        L"ZoltReflector.NativeEntry, ZoltReflector",
        L"Reflect",
        UNMANAGEDCALLERSONLY_METHOD,
        nullptr,
        reinterpret_cast<void**>(&s_reflectFn));

    if (rc < 0 || !s_reflectFn) {
        Solitaer::Warn("DENKEN", "Failed to load Reflect entry point: 0x%08X", rc);
        if (needClose && hostCtx && pfnClose) pfnClose(hostCtx);
        return false;
    }

    // Ping entry point
    rc = pfnLoadAssembly(
        reflectorPath.c_str(),
        L"ZoltReflector.NativeEntry, ZoltReflector",
        L"DenkenPing",
        UNMANAGEDCALLERSONLY_METHOD,
        nullptr,
        reinterpret_cast<void**>(&s_pingFn));

    if (rc < 0 || !s_pingFn) {
        Solitaer::Warn("DENKEN", "Failed to load DenkenPing entry point: 0x%08X", rc);
        // Non-fatal: Reflect is the main entry point
    }

    if (needClose && hostCtx && pfnClose) pfnClose(hostCtx);

    // Step 6: Verify bridge is working
    if (s_pingFn) {
        int32_t pingResult = s_pingFn();
        if (pingResult == 0x44454E4B) {
            Solitaer::Info("DENKEN", "Bridge verified (ping=DENK)");
        } else {
            Solitaer::Warn("DENKEN", "Ping returned unexpected value: 0x%08X", pingResult);
        }
    }

    s_ready = true;
    Solitaer::Summary("Denken CLR bridge initialized — managed reflection ready");
    return true;
}

bool IsReady()
{
    return s_ready;
}

void Shutdown()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_reflectFn = nullptr;
    s_pingFn = nullptr;
    s_ready = false;
    Solitaer::Info("DENKEN", "CLR bridge shut down");
}

bool Ping()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_pingFn) return false;
    return s_pingFn() == 0x44454E4B;
}

std::vector<std::string> ListAssemblies()
{
    std::vector<std::string> result;
    if (!s_ready) return result;

    auto resp = SendCommand({{"cmd", "list_assemblies"}});
    try {
        auto arr = json::parse(resp);
        if (arr.is_array()) {
            for (const auto& item : arr) {
                result.push_back(item.get<std::string>());
            }
        }
    } catch (...) {
        Solitaer::Warn("DENKEN", "ListAssemblies parse error: %s", resp.c_str());
    }
    return result;
}

std::vector<TypeSummary> EnumerateTypes(const std::string& assemblyName)
{
    std::vector<TypeSummary> result;
    if (!s_ready) return result;

    json req = {{"cmd", "enumerate_types"}};
    if (!assemblyName.empty()) req["assembly"] = assemblyName;

    auto resp = SendCommand(req);
    Solitaer::Info("DENKEN", "EnumerateTypes response (%zu bytes): %.200s",
        resp.size(), resp.c_str());
    try {
        auto arr = json::parse(resp);
        if (arr.is_array()) {
            for (const auto& item : arr) {
                TypeSummary ts;
                ts.fullName = item.value("fullName", "");
                ts.assemblyName = item.value("assemblyName", "");
                ts.fieldCount = item.value("fieldCount", 0);
                ts.methodTablePtr = 0;
                if (item.contains("methodTablePtr")) {
                    auto& mtVal = item["methodTablePtr"];
                    if (mtVal.is_number_unsigned())
                        ts.methodTablePtr = mtVal.get<uint64_t>();
                    else if (mtVal.is_number_integer())
                        ts.methodTablePtr = static_cast<uint64_t>(mtVal.get<int64_t>());
                    else if (mtVal.is_string()) {
                        auto s = mtVal.get<std::string>();
                        try { ts.methodTablePtr = std::stoull(s, nullptr, 0); }
                        catch (...) {}
                    }
                }
                result.push_back(std::move(ts));
            }
        }
    } catch (...) {
        Solitaer::Warn("DENKEN", "EnumerateTypes parse error: %s",
            resp.substr(0, 200).c_str());
    }
    return result;
}

TypeLayout GetTypeLayout(const std::string& typeName)
{
    TypeLayout result;
    if (!s_ready) return result;

    auto resp = SendCommand({{"cmd", "get_type_layout"}, {"typeName", typeName}});
    try {
        auto obj = json::parse(resp);
        if (obj.contains("error")) {
            Solitaer::Warn("DENKEN", "GetTypeLayout(%s): %s",
                typeName.c_str(), obj["error"].get<std::string>().c_str());
            return result;
        }

        result.fullName = obj.value("fullName", "");
        result.assemblyName = obj.value("assemblyName", "");
        result.baseTypeName = obj.value("baseTypeName", "");
        result.instanceSize = obj.value("instanceSize", 0);
        result.isValueType = obj.value("isValueType", false);

        if (obj.contains("fields") && obj["fields"].is_array()) {
            for (const auto& f : obj["fields"]) {
                FieldInfo fi;
                fi.name = f.value("name", "");
                fi.typeName = f.value("typeName", "");
                fi.offset = f.value("offset", -1);
                fi.size = f.value("size", 0);
                fi.isValueType = f.value("isValueType", false);
                fi.isStatic = f.value("isStatic", false);
                result.fields.push_back(std::move(fi));
            }
        }
    } catch (...) {
        Solitaer::Warn("DENKEN", "GetTypeLayout parse error: %s",
            resp.substr(0, 200).c_str());
    }
    return result;
}

uint64_t GetMethodTable(const std::string& typeName)
{
    if (!s_ready) return 0;

    auto resp = SendCommand({{"cmd", "get_method_table"}, {"typeName", typeName}});
    try {
        auto obj = json::parse(resp);
        auto mtStr = obj.value("methodTable", "");
        if (mtStr.starts_with("0x") || mtStr.starts_with("0X")) {
            return std::stoull(mtStr.substr(2), nullptr, 16);
        }
    } catch (...) {}
    return 0;
}

} // namespace Zolt::Denken
