// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/dol.hpp"
#include "riftwii/patch.hpp"

// In-game screenshots (Settings > In-game screenshots): the blob
// runtime/shot hooks the game's IOS_IoctlvAsync, to see the Wii Remote's
// buttons in its Bluetooth reads, and PADRead when the game has one, for
// a GameCube controller's. Hold 1 and press HOME, or hold L and R and
// press Down: the picture on screen is copied and written to the NAND
// (/shared2/riftwii) while the game runs; the menu makes PNGs of them at
// its next start (wii/screenshot.hpp).
//
// The blob goes below the MEM1 arena's top, under the adapter's and the
// resident runtime's; its state and a frame's room (about 830 KB) at the
// bottom of the MEM2 arena, above theirs. It chains with their hooks:
// where the resident runtime (IOS_IoctlvAsync) or the adapter (PADRead)
// already put a branch, this blob's replay takes that branch.
//
// Anything in the way turns it off with a reason for the log, never
// fails the launch.
namespace riftwii::wii {

struct ShotHook {
    bool active = false;
    bool demo = false;             // also shoots 20 and 40 s into the game (Dolphin tests)
    std::uint32_t ioctlv = 0;      // IOS_IoctlvAsync (0: no Wii Remote combo)
    std::uint32_t pad_read = 0;    // PADRead (0: no GameCube controller combo)
    std::uint32_t code_base = 0;   // the blob (MEM1)
    std::uint32_t code_bytes = 0;
    std::uint32_t state_base = 0;  // state and frame (MEM2)
    std::uint32_t state_bytes = 0;
    std::uint32_t new_arena1_hi = 0;
    std::uint32_t new_arena2_lo = 0;
};

// The IOS functions the blob calls, when another blob has hooked them:
// what reaches IOS without passing that blob (its replay slots), and the
// IOS_IoctlvAsync to hook. Zero fields are searched in the game.
struct ShotIpc {
    std::uint32_t open_async = 0;
    std::uint32_t close_async = 0;
    std::uint32_t write_async = 0;
    std::uint32_t ioctl_async = 0;
    std::uint32_t ioctlv_async = 0;
};

// Before the <memory> patches. `arena1_hi` and `arena2_lo` are the arena
// ends the other blobs left; `pad_read` is PADRead when the adapter found
// it (0: searched here). `direct`: no frame copy in MEM2 (the picture is
// written straight from the frame buffer), for games that need all their
// memory, such as big mods: Newer Super Mario Bros. Wii crashes on its
// title screen with 0.8 MB of MEM2 gone, and runs with 300 KB.
bool plan_shot_hook(const DolHeader& dol, std::uint32_t arena1_hi, std::uint32_t mem1_floor,
                    std::uint32_t arena2_lo, const ShotIpc& known, std::uint32_t pad_read, bool demo, bool direct,
                    const std::vector<MemoryPatch>& patches, ShotHook& out, std::string& why);

// Last of the hooks (after the patches, the cheats and the adapter's).
bool install_shot_hook(ShotHook& hook, std::string& why);

}  // namespace riftwii::wii
