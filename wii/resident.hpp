// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "riftwii/dol.hpp"

// E2: installs the resident runtime (runtime/resident, embedded as
// riftwii_rt_bin) into the top of the MEM2 arena and hooks the game's
// IOS_IoctlAsync, after the apploader has loaded the DOL and before the
// jump. The game never sees the reservation because 0x80003128 (the MEM2
// arena end IOS published) is lowered to the runtime's base.
namespace riftwii::wii {

struct ResidentOptions {
    bool gecko = false;          // report each DI read over the USB Gecko in slot B
    std::uint32_t extra_bytes = 0;  // reserved after the blob (tables, buffers; unused by E2)
};

struct ResidentInstall {
    std::uint32_t base = 0;
    std::uint32_t reserved_bytes = 0;
    std::uint32_t old_arena_end = 0;
    std::uint32_t new_arena_end = 0;  // to be written to 0x80003128 after the low-memory flush
    std::uint32_t ioctl_async = 0;    // hooked function
    std::uint32_t ioctlv_async = 0;   // found, not hooked (0 if unknown)
};

// `dol` describes the sections the apploader has already loaded; the text
// sections are searched in place.
bool install_resident(const DolHeader& dol, const ResidentOptions& options, ResidentInstall& out,
                      std::string& error);

}  // namespace riftwii::wii
