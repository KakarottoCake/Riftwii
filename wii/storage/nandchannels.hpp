// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/bnr.hpp"
#include "riftwii/nandchannel.hpp"

namespace riftwii::wii {

// A WiiWare or Virtual Console game installed on the Wii (riftwii/nandchannel.hpp
// decides which titles those are).
struct NandChannel {
    std::uint64_t title_id = 0;
    std::string code;  // the four letters of its title ID ("WBME"), also its game ID for covers and cheats
    ChannelClass kind;
    TmdContents tmd;
    // Its names from the banner's IMET header, one per BannerLanguage
    // (some empty); all empty when the banner could not be read.
    std::array<std::string, kBnrLanguages> names;
    // The banner file: /title/00010001/xxxxxxxx/content/<index 0>.app.
    std::string banner_path;
    // Whether the program the system starts (the boot index's content) is
    // there: a channel whose contents were deleted to make room keeps its
    // TMD, and cannot start.
    bool playable = false;
};

// The WiiWare and Virtual Console games installed on this Wii, sorted by
// code. `status` says in a plain sentence how many were found and why any
// were left out. Reads through ES and ISFS, opening the IOS's NAND
// permission check (boot.hpp) when the running IOS refuses the files, and
// closing it again before returning.
bool list_nand_channels(std::vector<NandChannel>& out, std::string& status);

// The name a channel is shown by: its banner's name in `language` (a
// BannerLanguage), then English, then the first one there is, then its code.
std::string nand_channel_name(const NandChannel& channel, int language);

}  // namespace riftwii::wii
