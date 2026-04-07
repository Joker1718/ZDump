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

// Macht — 馬赫特 (Power — CEPlugin)
// Cheat Engine plugin integration flag and helpers.
// When g_isCEPlugin is true, the DLL was loaded by CE directly
// (not injected into a game process), so auto-start is skipped.

namespace Zolt::Macht {

// Set to true when the DLL is loaded as a CE plugin
// (i.e., loaded into cheatengine-x86_64.exe itself)
extern bool g_isCEPlugin;

} // namespace Zolt::Macht
