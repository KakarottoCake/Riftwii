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
    // E3: same-size replacements served from memory. The redirect table and
    // the bytes are laid out after the blob inside the reservation.
    std::vector<MemReplacement> replacements;
    std::uint64_t table_tag = 0;
    // E5: first word offset of the virtual window, 0 = none. Reads at or
    // above it never reach the drive with their offset.
    std::uint32_t virtual_start_words = 0;
    // E4: replacements served from SD sectors through `sdio_fd`, a
    // /dev/sdio/slot0 fd the loader opened after the IOS reload with the
    // card selected (wii/sdio.hpp). Needs the game's IOS_IoctlvAsync.
    std::vector<SdReplacement> sd_replacements;
    std::int32_t sdio_fd = -1;
    bool sdio_sdhc = false;
    // E7: ranges served from the disc's own bytes elsewhere (relocated or
    // partially patched files).
    std::vector<DiscReplacement> disc_replacements;
};

struct ResidentInstall {
    std::uint32_t base = 0;
    std::uint32_t reserved_bytes = 0;
    std::uint32_t old_arena_end = 0;
    std::uint32_t new_arena_end = 0;  // to be written to 0x80003128 after the low-memory flush
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
