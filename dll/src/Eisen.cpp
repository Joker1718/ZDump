// Eisen — 艾乍 (Iron Warrior — ProxyVersion)
// version.dll forwarding stub for proxy DLL deployment.
// Only compiled when ZOLT_PROXY_BUILD is defined.

#ifdef ZOLT_PROXY_BUILD

#include "Eisen.h"
#include "Solliter.h"
#include <cstdio>

namespace Zolt::Eisen {

static HMODULE s_hRealVersion = nullptr;

// Forwarded function pointers
// version.dll exports (17 functions)
static FARPROC s_pGetFileVersionInfoA      = nullptr;
static FARPROC s_pGetFileVersionInfoByHandle = nullptr;
static FARPROC s_pGetFileVersionInfoExA    = nullptr;
static FARPROC s_pGetFileVersionInfoExW    = nullptr;
static FARPROC s_pGetFileVersionInfoSizeA  = nullptr;
static FARPROC s_pGetFileVersionInfoSizeExA = nullptr;
static FARPROC s_pGetFileVersionInfoSizeExW = nullptr;
static FARPROC s_pGetFileVersionInfoSizeW  = nullptr;
static FARPROC s_pGetFileVersionInfoW      = nullptr;
static FARPROC s_pVerFindFileA             = nullptr;
static FARPROC s_pVerFindFileW             = nullptr;
static FARPROC s_pVerInstallFileA          = nullptr;
static FARPROC s_pVerInstallFileW          = nullptr;
static FARPROC s_pVerLanguageNameA         = nullptr;
static FARPROC s_pVerLanguageNameW         = nullptr;
static FARPROC s_pVerQueryValueA           = nullptr;
static FARPROC s_pVerQueryValueW           = nullptr;

bool InitProxy() {
    // Build path to real version.dll in System32
    char sysDir[MAX_PATH];
    GetSystemDirectoryA(sysDir, MAX_PATH);

    char realPath[MAX_PATH];
    snprintf(realPath, MAX_PATH, "%s\\version.dll", sysDir);

    s_hRealVersion = LoadLibraryA(realPath);
    if (!s_hRealVersion) {
        Solliter::LogInit("Eisen: Failed to load real version.dll");
        return false;
    }

    // Resolve all exports
    // TODO: Resolve all 17 version.dll exports via GetProcAddress
    s_pGetFileVersionInfoA       = GetProcAddress(s_hRealVersion, "GetFileVersionInfoA");
    s_pGetFileVersionInfoByHandle = GetProcAddress(s_hRealVersion, "GetFileVersionInfoByHandle");
    s_pGetFileVersionInfoExA     = GetProcAddress(s_hRealVersion, "GetFileVersionInfoExA");
    s_pGetFileVersionInfoExW     = GetProcAddress(s_hRealVersion, "GetFileVersionInfoExW");
    s_pGetFileVersionInfoSizeA   = GetProcAddress(s_hRealVersion, "GetFileVersionInfoSizeA");
    s_pGetFileVersionInfoSizeExA = GetProcAddress(s_hRealVersion, "GetFileVersionInfoSizeExA");
    s_pGetFileVersionInfoSizeExW = GetProcAddress(s_hRealVersion, "GetFileVersionInfoSizeExW");
    s_pGetFileVersionInfoSizeW   = GetProcAddress(s_hRealVersion, "GetFileVersionInfoSizeW");
    s_pGetFileVersionInfoW       = GetProcAddress(s_hRealVersion, "GetFileVersionInfoW");
    s_pVerFindFileA              = GetProcAddress(s_hRealVersion, "VerFindFileA");
    s_pVerFindFileW              = GetProcAddress(s_hRealVersion, "VerFindFileW");
    s_pVerInstallFileA           = GetProcAddress(s_hRealVersion, "VerInstallFileA");
    s_pVerInstallFileW           = GetProcAddress(s_hRealVersion, "VerInstallFileW");
    s_pVerLanguageNameA          = GetProcAddress(s_hRealVersion, "VerLanguageNameA");
    s_pVerLanguageNameW          = GetProcAddress(s_hRealVersion, "VerLanguageNameW");
    s_pVerQueryValueA            = GetProcAddress(s_hRealVersion, "VerQueryValueA");
    s_pVerQueryValueW            = GetProcAddress(s_hRealVersion, "VerQueryValueW");

    Solliter::LogInit("Eisen: Proxy version.dll initialized");
    return true;
}

void ShutdownProxy() {
    if (s_hRealVersion) {
        FreeLibrary(s_hRealVersion);
        s_hRealVersion = nullptr;
    }
}

} // namespace Zolt::Eisen

// ============================================================
// Forwarded exports — each calls through to the real version.dll
// These are defined as naked extern "C" functions.
// The DEF file (ProxyVersion.def) maps the ordinals.
// ============================================================

// TODO: Implement forwarding thunks for all 17 exports
// For now, provide stub signatures that forward via function pointers

extern "C" {

// Example forwarding stub (others follow the same pattern):
// BOOL WINAPI GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
//     typedef BOOL(WINAPI* fn_t)(LPCSTR, DWORD, DWORD, LPVOID);
//     return ((fn_t)s_pGetFileVersionInfoA)(lptstrFilename, dwHandle, dwLen, lpData);
// }

// TODO: Implement all 17 forwarding functions

} // extern "C"

#endif // ZOLT_PROXY_BUILD
