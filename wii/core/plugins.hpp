// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

#include <cstddef>
#include <string>

#include "riftwii/plugin_api.h"
#include "riftwii/pluginfile.hpp"

// The plugin loader (docs/PLUGINS.md). A plugin is read from
// <plugins dir>/<name>.<version>.rwp the first time RiftWii needs it,
// checked (header before any allocation, then its SHA-256, API version and
// build stamp), placed in the plugin arena at the top of RiftWii's MEM2,
// relocated, flushed from the data cache and invalidated in the
// instruction cache, and only then started. It stays until a launch or
// RiftWii closing shuts it down; a warm restart starts with none loaded
// and loads again on demand.
//
// A plugin that cannot be loaded never takes RiftWii down: the caller gets
// nullptr and a plain sentence for the screen (in the menu's language),
// and the log says which check failed and why. Its feature stays off.
namespace riftwii::wii::plugin {

// The plugin `name`, loaded on first use. nullptr when it cannot be; then
// `message`, when given, holds the sentence for the player.
const rwp_plugin* Use(const char* name, std::string* message = nullptr);

// Starts what plugin `name` starts (its table's launch, with `request` of
// `request_bytes`), loading it on first use. Returns only on failure: the
// plugin's RWP_LAUNCH_* result (its reason in the request), or -1 when the
// plugin cannot be used or starts nothing, with `message` the sentence for
// the player (the log already says why).
int32_t Launch(const char* name, const void* request, uint32_t request_bytes, std::string& message);

// Calls every loaded plugin's shutdown and forgets them, giving the arena
// back to the heap. `why` is for the log ("the menu closed").
void ShutdownAll(const char* why);

// The folder the plugins are read from: settings.txt's "plugins = <dir>"
// (a test override), else the running boot.dol's folder (argv[0]) plus
// "plugins/", else sd:/apps/riftwii/plugins/.
std::string Directory();

// For the in-app update's apps pack: removes plugin files of other
// versions from Directory() (the stamped names of docs/PLUGINS.md), once
// this version's are there. Logs what it removed.
void RemoveOtherVersions();

// For the crash screen, with no allocation: the address ranges of the
// loaded plugins and what they registered. Returns how many it wrote.
std::size_t CrashRanges(PluginRange* out, std::size_t max);
// "selftest 1 at 0x933b0000-0x933b0800 (2610-460-g1a2b3c4d)", one per
// loaded plugin, into `out`; "none" when none is loaded. No allocation.
void CrashSummary(char* out, std::size_t bytes);

// For problem reports: the plugins this run loaded or refused, with their
// versions, stamps and addresses.
std::string Summary();

// The test switch (docs/DEVELOPING.md): sd:/riftwii/plugin_test.txt's
// "load <name>" lines load those plugins at start, log what happened and
// call their probe. False when there is no test file (nothing happens);
// otherwise `refused` gets the first refusal's sentence for Home's notice,
// "" when every plugin loaded.
bool RunTestSwitch(std::string& refused);

}  // namespace riftwii::wii::plugin
