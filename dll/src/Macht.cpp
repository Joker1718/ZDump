/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
// Macht — 馬赫特 (Power — CEPlugin)
// Cheat Engine plugin integration flag and helpers.

#include "Macht.h"

namespace Zolt::Macht {

// Default: not a CE plugin (will be set by CE Lua if applicable)
bool g_isCEPlugin = false;

// TODO: Add CE plugin initialization helpers if needed
// TODO: CE plugin version negotiation

} // namespace Zolt::Macht
