// Eisen - Iron Warrior (ProxyVersion)
// version.dll forwarding layer for proxy DLL deployment.
// Only compiled when ZOLT_PROXY_BUILD is defined.
//
// Loads the real version.dll from System32 and forwards all 17 exports.
// Internal functions use a Proxy_ prefix; ProxyVersion.def maps them
// to the real export names (GetFileVersionInfoA, etc.).

#ifdef ZOLT_PROXY_BUILD

#include "Eisen.h"
#include "Solliter.h"
#include <Windows.h>

namespace Zolt::Eisen {

// Real version.dll handle - loaded on first call
static HMODULE g_realVersion = nullptr;

static HMODULE LoadRealVersion()
{
    if (g_realVersion) return g_realVersion;

    // Build path to the real version.dll in System32
    wchar_t systemDir[MAX_PATH] = {};
    GetSystemDirectoryW(systemDir, MAX_PATH);

    wchar_t realPath[MAX_PATH] = {};
    wsprintfW(realPath, L"%s\\version.dll", systemDir);

    g_realVersion = LoadLibraryW(realPath);
    if (!g_realVersion) {
        Solliter::LogInit("Eisen: Failed to load real version.dll");
    } else {
        Solliter::LogInit("Eisen: Loaded real version.dll from System32");
    }
    return g_realVersion;
}

// Helper: resolve a function from the real version.dll
static FARPROC RealProc(const char* name)
{
    HMODULE real = LoadRealVersion();
    return real ? GetProcAddress(real, name) : nullptr;
}

bool InitProxy() {
    return LoadRealVersion() != nullptr;
}

void ShutdownProxy() {
    if (g_realVersion) {
        FreeLibrary(g_realVersion);
        g_realVersion = nullptr;
    }
}

} // namespace Zolt::Eisen

// ============================================================
// Forwarded exports (17 total)
// Internal names use Proxy_ prefix; ProxyVersion.def maps them
// to the real export names (GetFileVersionInfoA, etc.).
// ============================================================

using namespace Zolt::Eisen;

// --- GetFileVersionInfo family ---

extern "C" BOOL WINAPI Proxy_GetFileVersionInfoA(
    LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
    using Fn = BOOL(WINAPI*)(LPCSTR, DWORD, DWORD, LPVOID);
    static auto fn = reinterpret_cast<Fn>(RealProc("GetFileVersionInfoA"));
    return fn ? fn(lptstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

extern "C" BOOL WINAPI Proxy_GetFileVersionInfoW(
    LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
    using Fn = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    static auto fn = reinterpret_cast<Fn>(RealProc("GetFileVersionInfoW"));
    return fn ? fn(lptstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

extern "C" DWORD WINAPI Proxy_GetFileVersionInfoSizeA(
    LPCSTR lptstrFilename, LPDWORD lpdwHandle)
{
    using Fn = DWORD(WINAPI*)(LPCSTR, LPDWORD);
    static auto fn = reinterpret_cast<Fn>(RealProc("GetFileVersionInfoSizeA"));
    return fn ? fn(lptstrFilename, lpdwHandle) : 0;
}

extern "C" DWORD WINAPI Proxy_GetFileVersionInfoSizeW(
    LPCWSTR lptstrFilename, LPDWORD lpdwHandle)
{
    using Fn = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    static auto fn = reinterpret_cast<Fn>(RealProc("GetFileVersionInfoSizeW"));
    return fn ? fn(lptstrFilename, lpdwHandle) : 0;
}

// --- GetFileVersionInfoEx family (Vista+) ---

extern "C" BOOL WINAPI Proxy_GetFileVersionInfoExA(
    DWORD dwFlags, LPCSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
    using Fn = BOOL(WINAPI*)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
    static auto fn = reinterpret_cast<Fn>(RealProc("GetFileVersionInfoExA"));
    return fn ? fn(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

extern "C" BOOL WINAPI Proxy_GetFileVersionInfoExW(
    DWORD dwFlags, LPCWSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
    using Fn = BOOL(WINAPI*)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
    static auto fn = reinterpret_cast<Fn>(RealProc("GetFileVersionInfoExW"));
    return fn ? fn(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

extern "C" DWORD WINAPI Proxy_GetFileVersionInfoSizeExA(
    DWORD dwFlags, LPCSTR lpwstrFilename, LPDWORD lpdwHandle)
{
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPDWORD);
    static auto fn = reinterpret_cast<Fn>(RealProc("GetFileVersionInfoSizeExA"));
    return fn ? fn(dwFlags, lpwstrFilename, lpdwHandle) : 0;
}

extern "C" DWORD WINAPI Proxy_GetFileVersionInfoSizeExW(
    DWORD dwFlags, LPCWSTR lpwstrFilename, LPDWORD lpdwHandle)
{
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPDWORD);
    static auto fn = reinterpret_cast<Fn>(RealProc("GetFileVersionInfoSizeExW"));
    return fn ? fn(dwFlags, lpwstrFilename, lpdwHandle) : 0;
}

// --- GetFileVersionInfoByHandle (undocumented, but exported) ---

extern "C" BOOL WINAPI Proxy_GetFileVersionInfoByHandle(
    DWORD dwFlags, HANDLE hFile, LPVOID lpData, DWORD dwLen)
{
    using Fn = BOOL(WINAPI*)(DWORD, HANDLE, LPVOID, DWORD);
    static auto fn = reinterpret_cast<Fn>(RealProc("GetFileVersionInfoByHandle"));
    return fn ? fn(dwFlags, hFile, lpData, dwLen) : FALSE;
}

// --- VerQueryValue ---

extern "C" BOOL WINAPI Proxy_VerQueryValueA(
    LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen)
{
    using Fn = BOOL(WINAPI*)(LPCVOID, LPCSTR, LPVOID*, PUINT);
    static auto fn = reinterpret_cast<Fn>(RealProc("VerQueryValueA"));
    return fn ? fn(pBlock, lpSubBlock, lplpBuffer, puLen) : FALSE;
}

extern "C" BOOL WINAPI Proxy_VerQueryValueW(
    LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen)
{
    using Fn = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    static auto fn = reinterpret_cast<Fn>(RealProc("VerQueryValueW"));
    return fn ? fn(pBlock, lpSubBlock, lplpBuffer, puLen) : FALSE;
}

// --- VerFindFile ---

extern "C" DWORD WINAPI Proxy_VerFindFileA(
    DWORD uFlags, LPCSTR szFileName, LPCSTR szWinDir, LPCSTR szAppDir,
    LPSTR szCurDir, PUINT puCurDirLen, LPSTR szDestDir, PUINT puDestDirLen)
{
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT);
    static auto fn = reinterpret_cast<Fn>(RealProc("VerFindFileA"));
    return fn ? fn(uFlags, szFileName, szWinDir, szAppDir, szCurDir, puCurDirLen, szDestDir, puDestDirLen) : 0;
}

extern "C" DWORD WINAPI Proxy_VerFindFileW(
    DWORD uFlags, LPCWSTR szFileName, LPCWSTR szWinDir, LPCWSTR szAppDir,
    LPWSTR szCurDir, PUINT puCurDirLen, LPWSTR szDestDir, PUINT puDestDirLen)
{
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
    static auto fn = reinterpret_cast<Fn>(RealProc("VerFindFileW"));
    return fn ? fn(uFlags, szFileName, szWinDir, szAppDir, szCurDir, puCurDirLen, szDestDir, puDestDirLen) : 0;
}

// --- VerInstallFile ---

extern "C" DWORD WINAPI Proxy_VerInstallFileA(
    DWORD uFlags, LPCSTR szSrcFileName, LPCSTR szDestFileName,
    LPCSTR szSrcDir, LPCSTR szDestDir, LPCSTR szCurDir,
    LPSTR szTmpFile, PUINT puTmpFileLen)
{
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT);
    static auto fn = reinterpret_cast<Fn>(RealProc("VerInstallFileA"));
    return fn ? fn(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, puTmpFileLen) : 0;
}

extern "C" DWORD WINAPI Proxy_VerInstallFileW(
    DWORD uFlags, LPCWSTR szSrcFileName, LPCWSTR szDestFileName,
    LPCWSTR szSrcDir, LPCWSTR szDestDir, LPCWSTR szCurDir,
    LPWSTR szTmpFile, PUINT puTmpFileLen)
{
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT);
    static auto fn = reinterpret_cast<Fn>(RealProc("VerInstallFileW"));
    return fn ? fn(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, puTmpFileLen) : 0;
}

// --- VerLanguageName ---

extern "C" DWORD WINAPI Proxy_VerLanguageNameA(DWORD wLang, LPSTR szLang, DWORD cchLang)
{
    using Fn = DWORD(WINAPI*)(DWORD, LPSTR, DWORD);
    static auto fn = reinterpret_cast<Fn>(RealProc("VerLanguageNameA"));
    return fn ? fn(wLang, szLang, cchLang) : 0;
}

extern "C" DWORD WINAPI Proxy_VerLanguageNameW(DWORD wLang, LPWSTR szLang, DWORD cchLang)
{
    using Fn = DWORD(WINAPI*)(DWORD, LPWSTR, DWORD);
    static auto fn = reinterpret_cast<Fn>(RealProc("VerLanguageNameW"));
    return fn ? fn(wLang, szLang, cchLang) : 0;
}

#endif // ZOLT_PROXY_BUILD
