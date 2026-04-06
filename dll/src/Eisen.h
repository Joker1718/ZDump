#pragma once

// Eisen — 艾冉 (Iron Warrior — Proxy DLL Forwarding)
// DLL forwarding layer for proxy DLL deployment.
// Only compiled when ZOLT_PROXY_BUILD is defined.
//
// Supports multiple proxy targets:
//   ZOLT_PROXY_VERSION  — version.dll (17 exports)
//   ZOLT_PROXY_WINMM    — winmm.dll  (10+ exports)
//
// The real system DLL is loaded lazily on first forwarded call.

#ifdef ZOLT_PROXY_BUILD

#include <Windows.h>

namespace Zolt::Eisen {

// Load the real system DLL from System32
bool InitProxy();

// Unload the real system DLL
void ShutdownProxy();

// Helper: resolve a function from the real system DLL by name
FARPROC RealProc(const char* name);

} // namespace Zolt::Eisen

#endif // ZOLT_PROXY_BUILD
