// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/dol.hpp"
#include "riftwii/hook.hpp"
#include "rt_hook.h"
#include "rtfat.h"

// E2: installs the resident runtime (runtime/resident, embedded as
// riftwii_rt_bin): its code at the top of the MEM1 arena, its data at the
// top of the MEM2 arena, after the apploader has loaded the DOL and before
// the jump; the game never sees either reservation because the arena
// fields (0x80000034/0x80003110, 0x80003128) are lowered to the
// reservations. Every SDK IPC API function the structural search finds
// (riftwii/symsearch.hpp: the 14 async and sync forms of open, close,
// read, write, seek, ioctl, ioctlv) is diverted to its trampoline;
// IOS_IoctlAsync must be, the rest are skipped with a log line when
// their first instructions cannot be displaced. Those the game does not
// link are absent and need no hook.
namespace riftwii::wii {

// <savegame>: the title's data directory served from a folder of the
// card by the runtime's FAT engine (docs/CONDUCTOR_REVIEW_2.md section
// 24). Needs the card open (sdio_fd) and every present IPC API function
// hooked, or the game could reach NAND behind the redirect.
struct SavegameOptions {
    bool enabled = false;
    bool clone = false;      // the runtime copies the NAND save into the folder before the game's first request
    std::string prefix;      // "/title/<type>/<game id in hex>/data" from the TMD's title id, no trailing slash
    rtfat_volume volume{};   // the card's geometry and the folder's first cluster (wii/sdfile.hpp)
};

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
    SavegameOptions savegame;
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
    unsigned hooked = 0;              // SDK IPC API functions diverted (1 = only IOS_IoctlAsync)
    // Exactly the SDK entry addresses overwritten with 16-byte trampolines.
    // Entries after `hook_site_count` are zero.
    std::array<std::uint32_t, RT_IPC_ENTRIES> hook_sites{};
    unsigned hook_site_count = 0;
    std::uint32_t fs_state = 0;       // the savegame state block, 0 when not redirected
};

// `dol` describes the sections the apploader has already loaded; the text
// sections are searched in place.
bool install_resident(const DolHeader& dol, const ResidentOptions& options, ResidentInstall& out,
                      std::string& error);

}  // namespace riftwii::wii
