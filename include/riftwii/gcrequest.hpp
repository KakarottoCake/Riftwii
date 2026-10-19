// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/gcoverlay.hpp"
#include "riftwii/plugin_gamecube.h"

// The gamecube plugin's request (riftwii/plugin_gamecube.h) from both
// sides: the DOL flattens the packs' plan into it (wii/launch/gcplugin.cpp),
// the plugin checks what it was given and builds the plan again
// (plugins/gamecube/gcentry.cpp). Plain C crosses between the two builds,
// never a std:: container (docs/PLUGINS.md, "Stage (b)").
namespace riftwii {

// The packs' plan as the request carries it: `entries` holds the window's
// pieces and must outlive the request, whose pack_* fields point into
// `plan` and `entries`.
void gc_plan_to_request(const GcOverlayPlan& plan, std::vector<rwp_gc_pack_entry>& entries, rwp_gc_launch& request);

// The plan back from a request that has one (pack_fst set). False, with
// `why`, when a piece has an unknown kind.
bool gc_plan_from_request(const rwp_gc_launch& request, GcOverlayPlan& plan, std::string& why);

// Whether `request` (`bytes` long) is one this plugin can read: its size,
// the game's path and ID, an error buffer, and every pointer set with its
// size (the packs' FST and 32 header bytes, the window's entries, the
// cheats, the code handler, a whole crash screen of `crash_screen_bytes`,
// the way back's view). False, with `why`, otherwise.
bool gc_request_check(const rwp_gc_launch* request, std::uint32_t bytes, std::uint32_t crash_screen_bytes,
                      std::string& why);

}  // namespace riftwii
