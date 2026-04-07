/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
// Eisen — 艾冉 (Iron Warrior — Proxy DLL Forwarding)
// Common proxy infrastructure + target-specific forwarding.
// Only compiled when ZOLT_PROXY_BUILD is defined.
//
// Supports multiple proxy targets:
//   ZOLT_PROXY_WINMM   — winmm.dll proxy (180 exports via MASM trampolines)
//   default             — version.dll proxy (17 typed C++ wrappers)
//
// winmm: Real DLL loaded at DLL_PROCESS_ATTACH, all 180 function pointers
// resolved immediately. MASM naked JMP stubs forward calls perfectly.
// version: Typed C++ wrappers with lazy GetProcAddress on first call.

#ifdef ZOLT_PROXY_BUILD

#include "Eisen.h"
#include "Solitaer.h"
#include <Windows.h>

// Include generated winmm function pointer table (defines g_pfn_* variables)
#if defined(ZOLT_PROXY_WINMM)
#include "EisenWinmmPtrs.h"
#endif

namespace Zolt::Eisen {

// ============================================================
// Common proxy infrastructure
// ============================================================

// Real system DLL handle — loaded at DLL_PROCESS_ATTACH
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
        Solitaer::Info("INIT", "Eisen: Failed to load real %s", kRealDllTag);
    } else {
        Solitaer::Info("INIT", "Eisen: Loaded real %s from System32", kRealDllTag);
    }
    return g_realDll;
}

FARPROC RealProc(const char* name)
{
    HMODULE real = LoadRealDll();
    return real ? GetProcAddress(real, name) : nullptr;
}

bool InitProxy() {
    HMODULE real = LoadRealDll();
    if (!real) return false;

#if defined(ZOLT_PROXY_WINMM)
    // Resolve all 180 winmm function pointers for MASM trampolines
    InitWinmmPointers(real);
#endif

    return true;
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
// All 180 exports are handled by MASM naked JMP trampolines
// (ProxyWinmmTrampoline.asm) through function pointers initialized
// in InitProxy() above. No typed C++ wrappers needed.
// ============================================================

#endif // ZOLT_PROXY_BUILD
