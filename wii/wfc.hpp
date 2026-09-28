// SPDX-License-Identifier: GPL-3.0-or-later
// Credit: USB Loader GX (https://github.com/wiidev/usbloadergx),
// source/patches/gamepatches.c, GPL-3.0: do_new_wiimmfi (Leseratte's Mario
// Kart Wii Wiimmfi patch, 2018, with the error 51420 fix, 2021: copied
// unchanged as data) and patch_error_codes (the Mario Kart Wii remote code
// execution fix: reimplemented). See NOTICE.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/mempatch.hpp"
#include "riftwii/wfcpatch.hpp"

// Points the loaded game at an online server (riftwii/wfcpatch.hpp), just
// before it starts. Besides the string patches:
//   - Wiimmfi on Mario Kart Wii: Wiimmfi's own patch (its code in 0x500
//     bytes below the MEM1 arena end), from do_new_wiimmfi;
//   - Wiimmfi, AltWFC or a custom server on Mario Kart Wii: the fix for its
//     remote code execution hole (patch_error_codes);
//   - WiiLink WFC: WiiLink's hook, done as their launcher does
//     (vendor-wwfc): their downloader payload below the MEM1 arena end, a
//     128 KB block below the MEM2 arena end for what it fetches, for the
//     disc titles in their list.
// The arena ends (0x80000034, 0x80003110, 0x80003128) move down past what
// is added. Needs the low-memory globals already written.
namespace riftwii::wii {

void ApplyWfc(const std::vector<MemoryRegion>& loaded, WfcServer server, const std::string& domain,
              const std::string& game_id, std::uint8_t disc_version);

}  // namespace riftwii::wii
