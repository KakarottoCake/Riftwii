// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/dol.hpp"
#include "riftwii/patch.hpp"

// The Wii Remote log (runtime/wlog, riftwii/wpadlog.hpp): the game's
// empty debug print functions branch to the blob, which keeps what the
// Bluetooth and Wii Remote libraries would print; when the player leaves
// through the HOME Menu's "Wii Menu" the blob saves it to the NAND
// (/shared2/riftwii/wpadlog.bin), and the menu turns it into
// sd:/riftwii/wpadlog.txt at its next start (wii/online/reportsend.hpp).
//
// A diagnostic, for one question: why a Wii U Pro Controller, which the
// menu reads, does nothing in games. It is on when the menu saw one (or
// settings.txt says wpad_log = on), never in a code build or a game that
// clears MEM1's top. The blob goes below the MEM1 arena's top, under the
// other blobs, its log at the bottom of the MEM2 arena, above theirs.
// Anything in the way turns it off with a reason for the log, never fails
// the launch.
namespace riftwii::wii {

struct WpadLogHook {
    bool active = false;
    std::vector<std::uint32_t> stubs;  // the print functions hooked
    std::uint32_t exit_site = 0;       // __OSLaunchMenu's "li rZ,0"
    std::uint32_t code_base = 0;       // the blob (MEM1)
    std::uint32_t code_bytes = 0;
    std::uint32_t state_base = 0;      // the log (MEM2)
    std::uint32_t state_bytes = 0;
    std::uint32_t new_arena1_hi = 0;
    std::uint32_t new_arena2_lo = 0;
};

// The menu saw a Wii U Pro Controller (DescribeControllers, as the menu
// closes): with wpad_log = auto (the default) the launch keeps the log.
void note_wiiu_pro_seen();
// Whether this launch keeps the log, from settings.txt's wpad_log (auto,
// on, off); `why` says which for boot.log.
bool wpad_log_wanted(std::string& why);

// Before the <memory> patches, on the game as loaded. `known_ios` are the
// resident's unhooked synchronous IOS_Open, IOS_Close, IOS_Write and
// IOS_Ioctl when it is installed (the resident's own would send the
// file to a pack's save folder), all 0 to search the game.
bool plan_wpad_log(const DolHeader& dol, std::uint32_t arena1_hi, std::uint32_t mem1_floor, std::uint32_t arena2_lo,
                   const std::uint32_t known_ios[4], const std::vector<MemoryPatch>& patches, WpadLogHook& out,
                   std::string& why);

// After the patches, the cheats and the other blobs' hooks.
bool install_wpad_log(WpadLogHook& hook, std::string& why);

}  // namespace riftwii::wii
