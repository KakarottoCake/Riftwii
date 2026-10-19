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
    std::string code;     // the four letters of its title ID ("WBME")
    std::string game_id;  // what RiftWii keys it by (riftwii/nandchannel.hpp's channel_game_id: "WBME01")
    ChannelClass kind;
    // Its name in GameTDB's title list, in the menu's language (empty
    // without a list).
    std::string listed_name;
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
    // The emulated NAND folder on the SD card it is installed in
    // ("sd:/nands/01"), as USB Loader GX keeps channels; empty for the
    // Wii's own. Its files are read from the card, and it runs under d2x's
    // NAND emulation of that folder, its saves kept there.
    std::string emu_nand;
};

// The WiiWare and Virtual Console games installed on this Wii, sorted by
// code. `status` says in a plain sentence how many were found and why any
// were left out. Reads through ES and ISFS, opening the IOS's NAND
// permission check (boot.hpp) when the running IOS refuses the files, and
// closing it again before returning.
bool list_nand_channels(std::vector<NandChannel>& out, std::string& status);

// The WiiWare and Virtual Console games installed in the emulated NAND
// `folder` (sd:/...), added to `out`; a title the Wii itself has (already
// in `out`) is not listed twice. `status` says how many, in a plain
// sentence. False when the folder holds no channels.
bool list_emu_channels(const std::string& folder, std::vector<NandChannel>& out, std::string& status);

// The whole banner file (content index 0), for the Channels view's banner
// store (wii/menu/banners.hpp). False with `error` saying why.
bool read_nand_channel_banner(const NandChannel& channel, std::vector<std::uint8_t>& out, std::string& error);

// The name a channel is shown by: GameTDB's name, else its banner's name in `language` (a
// BannerLanguage), then English, then the first one there is, then its code.
std::string nand_channel_name(const NandChannel& channel, int language);

// What RiftWii needs to start a channel with its own loader (boot.hpp's
// boot_channel): the program to load, and the title's TMD, ticket and the
// system's certificates, for ES to run the game as that title (its saves
// are its own title's).
struct ChannelBootFiles {
    std::uint64_t title_id = 0;
    std::string emu_nand;  // NandChannel::emu_nand
    std::string game_id;
    std::uint32_t required_ios = 0;
    // The IOSes to try, in order: ES runs a game as its own title
    // (ES_Identify) only for the Wii Menu on Nintendo's IOSes, and for
    // anyone on a d2x cIOS, so the d2x slots image games would take come
    // first, then the IOS the title asks for. In Dolphin only that one.
    std::vector<int> ios_order;
    ChannelProgram program;
    std::vector<std::uint8_t> dol;  // the program content, whole
    std::vector<std::uint8_t> tmd;
    std::vector<std::uint8_t> ticket;
    std::vector<std::uint8_t> certs;
};

// Reads them for `title_id`, under the running IOS (opening its NAND
// permission check as the listing does). False with `error` saying why:
// the system then starts the title as the Wii Menu does.
// `emu_nand`: the emulated NAND folder the channel is in, empty for the
// Wii's own.
bool read_channel_boot(std::uint64_t title_id, const std::string& emu_nand, ChannelBootFiles& out, std::string& error);

// The channel game the menu is launching (MENU_PLAY_CHANNEL, wii/main.cpp's
// StartChannel): set by the menu when Start is pressed on one.
void SetChannelToStart(const NandChannel& channel);
std::uint64_t ChannelToStart();
// Its emulated NAND folder, empty for the Wii's own.
const std::string& ChannelToStartNand();

}  // namespace riftwii::wii
