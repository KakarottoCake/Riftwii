// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/dol.hpp"
#include "riftwii/hook.hpp"

// E2: installs the resident runtime (runtime/resident, embedded as
// riftwii_rt_bin) into the top of the MEM2 arena and hooks the game's
// IOS_IoctlAsync, after the apploader has loaded the DOL and before the
// jump. The game never sees the reservation because 0x80003128 (the MEM2
// arena end IOS published) is lowered to the runtime's base.
namespace riftwii::wii {

struct ResidentOptions {
    bool gecko = false;  // report each DI read over the USB Gecko in slot B
    // The redirect table's content (riftwii/hook.hpp). The table and the
    // MEM bytes are laid out after the blob inside the reservation; SD
    // runs need `sdio_fd`, a /dev/sdio/slot0 fd the loader opened after
    // the IOS reload with the card selected (wii/sdio.hpp), and the game's
    // IOS_IoctlvAsync; DISC runs need the game's IOS_IoctlAsync (always
    // found, it is the hooked one).
    PayloadPieces pieces;
    std::uint64_t table_tag = 0;
    std::int32_t sdio_fd = -1;
    bool sdio_sdhc = false;
    // E5: first word offset of the virtual window, 0 = none. Reads at or
    // above it never reach the drive with their offset.
    std::uint32_t virtual_start_words = 0;
    // The lowest MEM1 address the code may take: above this loader and the
    // apploader image, which the game reclaims only after it starts.
    std::uint32_t mem1_floor = 0;
};

struct ResidentInstall {
    std::uint32_t code_base = 0;        // the blob, at the top of the MEM1 arena
    std::uint32_t code_bytes = 0;
    std::uint32_t old_arena1_hi = 0;    // the MEM1 arena end as the apploader left it (below the BI2 and FST)
    std::uint32_t new_arena1_hi = 0;    // to be stored at 0x80000034 and 0x80003110 (== code_base)
    std::uint32_t data_base = 0;        // payload and buffers at the top of the MEM2 arena, 0 when none
    std::uint32_t data_bytes = 0;
    std::uint32_t old_arena2_end = 0;
    std::uint32_t new_arena2_end = 0;   // to be written to 0x80003128 after the low-memory flush
    std::uint32_t ioctl_async = 0;    // hooked function
    std::uint32_t ioctlv_async = 0;   // found, not hooked (0 if unknown)
    std::uint32_t table = 0;          // redirect table address, 0 when there are no replacements
    std::uint32_t payload_bytes = 0;
    std::uint32_t bounce_bytes = 0;   // SD bounce buffers after the payload, 0 without SD replacements
};

// `dol` describes the sections the apploader has already loaded; the text
// sections are searched in place.
bool install_resident(const DolHeader& dol, const ResidentOptions& options, ResidentInstall& out,
                      std::string& error);

}  // namespace riftwii::wii
