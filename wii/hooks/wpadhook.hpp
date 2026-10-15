// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
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
// It began as a diagnostic, for one question: why a Wii U Pro Controller,
// which the menu reads, does nothing in games. It also hooks the game's
// IOS_IoctlvAsync, chained with the resident runtime and the screenshot
// blob, to run the Wii U Pro Controller translator (runtime/rtpro.h: the
// Pro reaches the game as a Wii Remote with a Classic Controller Pro) and
// to keep a trace of the game's Bluetooth traffic in the same file.
//
// It is on when the menu saw a Pro Controller or the console has one
// paired (or settings.txt says wpad_log = on), never in a code build or a
// game that clears MEM1's top; settings.txt pro_controller = off keeps the
// trace and leaves the traffic alone. The blob goes below the MEM1 arena's
// top, under the other blobs, its log at the bottom of the MEM2 arena,
// above theirs. Anything in the way turns it off with a reason for the
// log, never fails the launch.
namespace riftwii::wii {

struct WpadLogHook {
    bool active = false;
    std::vector<std::uint32_t> stubs;  // the print functions hooked
    std::uint32_t exit_site = 0;       // __OSLaunchMenu's "li rZ,0"
    std::uint32_t ioctlv = 0;          // IOS_IoctlvAsync (0: no Bluetooth trace or translator)
    bool translate = false;            // the Pro Controller translator, else the trace only
    std::uint32_t tables = 0;          // the game's encryption lookup tables (riftwii/wpadlog.hpp), 0: none
    std::uint32_t code_base = 0;       // the blob (MEM1)
    std::uint32_t code_bytes = 0;
    std::uint32_t state_base = 0;      // the log (MEM2)
    std::uint32_t state_bytes = 0;
    std::uint32_t new_arena1_hi = 0;
    std::uint32_t new_arena2_lo = 0;
    // What the Pro Controller test did to the firmware probe (RTWLOG_PROBE_*),
    // kept in the log's header: boot.log ends before it is done.
    std::uint32_t probe = 0;
    std::uint32_t probe_at = 0;
};

// The menu saw a Wii U Pro Controller (DescribeControllers, as the menu
// closes): with wpad_log = auto (the default) the launch keeps the log.
void note_wiiu_pro_seen();
// Whether this launch keeps the log, from settings.txt's wpad_log (auto,
// on, off) and, for auto, whether a Pro Controller was seen in the menu or
// is among the console's `pro_pairings`; `why` says which for boot.log.
bool wpad_log_wanted(std::size_t pro_pairings, std::string& why);
// Whether the translator runs (settings.txt pro_controller, on unless off).
bool pro_translator_wanted();
// settings.txt pro_controller = every: every link is taken as a Pro
// Controller, to run the translator in Dolphin, which has none (a real
// Wii Remote then has no buttons of its own: the game sees a Classic
// Controller Pro made of its extension's bytes).
bool pro_translator_every_link();

// Before the <memory> patches, on the game as loaded. `known_ios` are the
// resident's unhooked synchronous IOS_Open, IOS_Close, IOS_Write and
// IOS_Ioctl when it is installed (the resident's own would send the
// file to a pack's save folder), all 0 to search the game; `known_ioctlv`
// the IOS_IoctlvAsync the resident found (0: searched). `pros` are the
// Pro Controller pairings' addresses, as SYSCONF keeps them.
bool plan_wpad_log(const DolHeader& dol, std::uint32_t arena1_hi, std::uint32_t mem1_floor, std::uint32_t arena2_lo,
                   const std::uint32_t known_ios[4], std::uint32_t known_ioctlv,
                   const std::vector<std::array<std::uint8_t, 6>>& pros, bool translate,
                   const std::vector<MemoryPatch>& patches, WpadLogHook& out, std::string& why);

// After the patches, the cheats, the other blobs' hooks and Return to
// RiftWii, which finds __OSLaunchMenu by the instruction the exit hook takes.
bool install_wpad_log(WpadLogHook& hook, std::string& why);

}  // namespace riftwii::wii
