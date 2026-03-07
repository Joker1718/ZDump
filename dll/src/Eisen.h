#pragma once

// Eisen — 艾乍 (Iron Warrior — ProxyVersion)
// version.dll forwarding stub for proxy DLL deployment.
// Only compiled when ZOLT_PROXY_BUILD is defined.
// Forwards all version.dll exports to the real system version.dll.

#ifdef ZOLT_PROXY_BUILD

#include <Windows.h>

namespace Zolt::Eisen {

// Load the real version.dll from System32 and resolve all forwarded exports
bool InitProxy();

// Unload the real version.dll
void ShutdownProxy();

} // namespace Zolt::Eisen

#endif // ZOLT_PROXY_BUILD
