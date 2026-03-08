// Eisen — 艾乍 (Iron Warrior — Proxy DLL Forwarding)
// Common proxy infrastructure + target-specific forwarding.
// Only compiled when ZOLT_PROXY_BUILD is defined.
//
// Supports multiple proxy targets:
//   ZOLT_PROXY_WINMM   — winmm.dll proxy (for games importing WINMM)
//   default             — version.dll proxy (for games importing VERSION)
//
// The real system DLL is loaded lazily on first forwarded call.
// Internal functions use a Proxy_ prefix; the DEF file maps them
// to the real export names to avoid compile-time conflicts.

#ifdef ZOLT_PROXY_BUILD

#include "Eisen.h"
#include "Solliter.h"
#include <Windows.h>

namespace Zolt::Eisen {

// ============================================================
// Common proxy infrastructure
// ============================================================

// Real system DLL handle — loaded on first forwarded call
static HMODULE g_realDll = nullptr;

// Name of the real system DLL to load from System32
#if defined(ZOLT_PROXY_WINMM)
static const wchar_t* const kRealDllName = L"winmm.dll";
static const char*    const kRealDllTag  = "winmm.dll";
#else // default: version.dll
static const wchar_t* const kRealDllName = L"version.dll";
static const char*    const kRealDllTag  = "version.dll";
#endif

static HMODULE LoadRealDll()
{
    if (g_realDll) return g_realDll;

    // Build full path to the real DLL in System32
    wchar_t systemDir[MAX_PATH] = {};
    GetSystemDirectoryW(systemDir, MAX_PATH);

    wchar_t realPath[MAX_PATH] = {};
    wsprintfW(realPath, L"%s\\%s", systemDir, kRealDllName);

    g_realDll = LoadLibraryW(realPath);
    if (!g_realDll) {
        Solliter::Info("INIT", "Eisen: Failed to load real %s", kRealDllTag);
    } else {
        Solliter::Info("INIT", "Eisen: Loaded real %s from System32", kRealDllTag);
    }
    return g_realDll;
}

FARPROC RealProc(const char* name)
{
    HMODULE real = LoadRealDll();
    return real ? GetProcAddress(real, name) : nullptr;
}

bool InitProxy() {
    return LoadRealDll() != nullptr;
}

void ShutdownProxy() {
    if (g_realDll) {
        FreeLibrary(g_realDll);
        g_realDll = nullptr;
    }
}

} // namespace Zolt::Eisen

using namespace Zolt::Eisen;

// ============================================================
// version.dll forwarding exports (17 total)
// Only compiled when building as version.dll proxy.
// ============================================================

#if !defined(ZOLT_PROXY_WINMM)

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

#endif // !ZOLT_PROXY_WINMM

// ============================================================
// winmm.dll forwarding exports
// Only compiled when building as winmm.dll proxy.
// Covers the most commonly imported WINMM functions.
// ============================================================

#if defined(ZOLT_PROXY_WINMM)

// MMRESULT is UINT
typedef UINT MMRESULT;

// --- Timer functions ---

extern "C" MMRESULT WINAPI Proxy_timeBeginPeriod(UINT uPeriod)
{
    using Fn = MMRESULT(WINAPI*)(UINT);
    static auto fn = reinterpret_cast<Fn>(RealProc("timeBeginPeriod"));
    return fn ? fn(uPeriod) : 0;
}

extern "C" MMRESULT WINAPI Proxy_timeEndPeriod(UINT uPeriod)
{
    using Fn = MMRESULT(WINAPI*)(UINT);
    static auto fn = reinterpret_cast<Fn>(RealProc("timeEndPeriod"));
    return fn ? fn(uPeriod) : 0;
}

extern "C" MMRESULT WINAPI Proxy_timeGetDevCaps(void* ptc, UINT cbtc)
{
    using Fn = MMRESULT(WINAPI*)(void*, UINT);
    static auto fn = reinterpret_cast<Fn>(RealProc("timeGetDevCaps"));
    return fn ? fn(ptc, cbtc) : 0;
}

extern "C" DWORD WINAPI Proxy_timeGetTime()
{
    using Fn = DWORD(WINAPI*)();
    static auto fn = reinterpret_cast<Fn>(RealProc("timeGetTime"));
    return fn ? fn() : 0;
}

extern "C" MMRESULT WINAPI Proxy_timeGetSystemTime(void* pmmt, UINT cbmmt)
{
    using Fn = MMRESULT(WINAPI*)(void*, UINT);
    static auto fn = reinterpret_cast<Fn>(RealProc("timeGetSystemTime"));
    return fn ? fn(pmmt, cbmmt) : 0;
}

// --- MIDI In functions ---

extern "C" MMRESULT WINAPI Proxy_midiInClose(HANDLE hMidiIn)
{
    using Fn = MMRESULT(WINAPI*)(HANDLE);
    static auto fn = reinterpret_cast<Fn>(RealProc("midiInClose"));
    return fn ? fn(hMidiIn) : 0;
}

extern "C" MMRESULT WINAPI Proxy_midiInGetDevCapsA(
    UINT_PTR uDeviceID, void* pmic, UINT cbmic)
{
    using Fn = MMRESULT(WINAPI*)(UINT_PTR, void*, UINT);
    static auto fn = reinterpret_cast<Fn>(RealProc("midiInGetDevCapsA"));
    return fn ? fn(uDeviceID, pmic, cbmic) : 0;
}

extern "C" MMRESULT WINAPI Proxy_midiInGetDevCapsW(
    UINT_PTR uDeviceID, void* pmic, UINT cbmic)
{
    using Fn = MMRESULT(WINAPI*)(UINT_PTR, void*, UINT);
    static auto fn = reinterpret_cast<Fn>(RealProc("midiInGetDevCapsW"));
    return fn ? fn(uDeviceID, pmic, cbmic) : 0;
}

extern "C" MMRESULT WINAPI Proxy_midiInGetErrorTextA(
    MMRESULT mmrError, LPSTR pszText, UINT cchText)
{
    using Fn = MMRESULT(WINAPI*)(MMRESULT, LPSTR, UINT);
    static auto fn = reinterpret_cast<Fn>(RealProc("midiInGetErrorTextA"));
    return fn ? fn(mmrError, pszText, cchText) : 0;
}

extern "C" UINT WINAPI Proxy_midiInGetNumDevs()
{
    using Fn = UINT(WINAPI*)();
    static auto fn = reinterpret_cast<Fn>(RealProc("midiInGetNumDevs"));
    return fn ? fn() : 0;
}

extern "C" MMRESULT WINAPI Proxy_midiInOpen(
    void* phMidiIn, UINT uDeviceID, DWORD_PTR dwCallback,
    DWORD_PTR dwInstance, DWORD fdwOpen)
{
    using Fn = MMRESULT(WINAPI*)(void*, UINT, DWORD_PTR, DWORD_PTR, DWORD);
    static auto fn = reinterpret_cast<Fn>(RealProc("midiInOpen"));
    return fn ? fn(phMidiIn, uDeviceID, dwCallback, dwInstance, fdwOpen) : 0;
}

extern "C" MMRESULT WINAPI Proxy_midiInStart(HANDLE hMidiIn)
{
    using Fn = MMRESULT(WINAPI*)(HANDLE);
    static auto fn = reinterpret_cast<Fn>(RealProc("midiInStart"));
    return fn ? fn(hMidiIn) : 0;
}

extern "C" MMRESULT WINAPI Proxy_midiInStop(HANDLE hMidiIn)
{
    using Fn = MMRESULT(WINAPI*)(HANDLE);
    static auto fn = reinterpret_cast<Fn>(RealProc("midiInStop"));
    return fn ? fn(hMidiIn) : 0;
}

// --- Wave Out functions ---

extern "C" UINT WINAPI Proxy_waveOutGetNumDevs()
{
    using Fn = UINT(WINAPI*)();
    static auto fn = reinterpret_cast<Fn>(RealProc("waveOutGetNumDevs"));
    return fn ? fn() : 0;
}

extern "C" MMRESULT WINAPI Proxy_waveOutOpen(
    void* phwo, UINT uDeviceID, void* pwfx,
    DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen)
{
    using Fn = MMRESULT(WINAPI*)(void*, UINT, void*, DWORD_PTR, DWORD_PTR, DWORD);
    static auto fn = reinterpret_cast<Fn>(RealProc("waveOutOpen"));
    return fn ? fn(phwo, uDeviceID, pwfx, dwCallback, dwInstance, fdwOpen) : 0;
}

extern "C" MMRESULT WINAPI Proxy_waveOutClose(HANDLE hwo)
{
    using Fn = MMRESULT(WINAPI*)(HANDLE);
    static auto fn = reinterpret_cast<Fn>(RealProc("waveOutClose"));
    return fn ? fn(hwo) : 0;
}

// --- PlaySound ---

extern "C" BOOL WINAPI Proxy_PlaySoundA(LPCSTR pszSound, HMODULE hmod, DWORD fdwSound)
{
    using Fn = BOOL(WINAPI*)(LPCSTR, HMODULE, DWORD);
    static auto fn = reinterpret_cast<Fn>(RealProc("PlaySoundA"));
    return fn ? fn(pszSound, hmod, fdwSound) : FALSE;
}

extern "C" BOOL WINAPI Proxy_PlaySoundW(LPCWSTR pszSound, HMODULE hmod, DWORD fdwSound)
{
    using Fn = BOOL(WINAPI*)(LPCWSTR, HMODULE, DWORD);
    static auto fn = reinterpret_cast<Fn>(RealProc("PlaySoundW"));
    return fn ? fn(pszSound, hmod, fdwSound) : FALSE;
}

#endif // ZOLT_PROXY_WINMM

#endif // ZOLT_PROXY_BUILD
