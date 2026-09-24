// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/dol.hpp"
#include "riftwii/patch.hpp"

// The GameCube controller adapter for Wii U in games that support the
// GameCube controller: the game's PADRead and PADControlMotor are hooked
// (riftwii/symsearch.hpp finds them) and the adapter's controllers fill
// the ports that have none plugged in, rumble included. The blob
// (runtime/pad, embedded as riftwii_pad_bin) goes just below the MEM1
// arena's top, below the resident runtime when there is one; the driver
// state (runtime/rtgcad.h, buffers IOS reaches) at the MEM2 arena's
// bottom, above the resident's data. The driver zeroes its state at the
// game's first PADRead, so nothing is staged there. /dev/usb/hid is
// opened here, after the last IOS reload, and handed to the game.
//
// Anything that stands in the way turns the adapter off with a reason
// for the log, never fails the launch: no PADRead in the game, a pack's
// memory patch on the memory it would take, or (after the patches) a
// first instruction of PADRead that can no longer be moved (a pack or
// code hooked it). A Gecko code that later writes over the hook simply
// wins: its own branch replaces ours and the adapter goes quiet.
namespace riftwii::wii {

struct PadHook {
    bool active = false;             // planned; install_pad_hook hooks it
    bool demo = false;               // no adapter: the first empty port presses A (Dolphin tests)
    std::uint32_t read = 0;          // PADRead
    std::uint32_t motor = 0;         // PADControlMotor, 0: no rumble
    std::uint32_t code_base = 0;     // the blob (MEM1)
    std::uint32_t code_bytes = 0;
    std::uint32_t state_base = 0;    // the driver state (MEM2)
    std::uint32_t state_bytes = 0;
    std::uint32_t new_arena1_hi = 0; // == code_base
    std::uint32_t new_arena2_lo = 0; // the end of the state
    std::int32_t fd = -1;            // /dev/usb/hid
    std::uint32_t version = 0;       // 4 or 5
};

// Opens /dev/usb/hid on RiftWii's own handle and tells v4 from v5.
bool open_usb_hid(std::int32_t& fd, std::uint32_t& version, std::string& why);

// Whether the running IOS has /dev/usb/hid (IOS36, which many games
// ask for, has none: with the adapter on, the running IOS is kept).
bool usb_hid_present();

// Before the <memory> patches. `arena1_hi` and `arena2_lo` are the arena
// ends left by the resident runtime (or the game's own without it); the
// IOS entries are the resident's unhooked ones, 0 to search the game.
bool plan_pad_hook(const DolHeader& dol, std::uint32_t arena1_hi, std::uint32_t mem1_floor, std::uint32_t arena2_lo,
                   std::uint32_t ioctl_async, std::uint32_t ioctlv_async,
                   const std::vector<MemoryPatch>& patches, bool demo, PadHook& out, std::string& why);

// After the patches and the cheats, just before the handover.
bool install_pad_hook(PadHook& hook, std::string& why);

}  // namespace riftwii::wii
