// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// WiiWare and Virtual Console games installed on the Wii: which of the
// Wii's titles they are, and where their files lie in its memory (the
// NAND). Plain data, host-tested; wii/storage/nandchannels.cpp lists the
// titles and reads the files.
//
// Sources (no code taken from any of them): wiibrew's public pages
// "Title database" (title types, and the first letter of a channel's
// four-letter code: W for WiiWare, F, J, N and so on for each Virtual
// Console system), "Title metadata" (the TMD's layout) and "NAND"
// (/title/<upper>/<lower>/content/<content id>.app).
namespace riftwii {

// Titles whose upper half is this are channels installed from the Wii
// Shop or a WAD: WiiWare, Virtual Console, Nintendo's own channels, and
// homebrew channels. Discs' data and the system's channels have others.
constexpr std::uint32_t kDownloadedChannels = 0x00010001;

enum class ChannelKind {
    None,           // not a game: a Nintendo channel, a homebrew channel, RiftWii's own
    WiiWare,
    VirtualConsole,
};

struct ChannelClass {
    ChannelKind kind = ChannelKind::None;
    // For Virtual Console: the system it comes from, in English ("NES",
    // "Nintendo 64"); empty otherwise.
    std::string system;
};

// The four-letter code of a title's lower half ("WBME"), or empty when
// any of its bytes is not an upper-case letter or a digit.
std::string channel_code(std::uint64_t title_id);

// Whether a title is a game RiftWii lists as a channel, by its upper half
// and the first letter of its code. Known homebrew channels whose codes
// start with a Virtual Console letter (the Homebrew Channel's LULZ and
// JODI, RiftWii's UFTW) are left out.
ChannelClass classify_channel(std::uint64_t title_id);

// One of a TMD's content records.
struct TmdContent {
    std::uint32_t id = 0;     // the file's name on the NAND, %08x.app
    std::uint16_t index = 0;  // 0 is the banner, the boot index the program
    std::uint16_t type = 0;   // 1 normal, 0x4001 and 0x8001 shared
    std::uint64_t size = 0;
};

struct TmdContents {
    std::uint64_t title_id = 0;
    std::uint32_t required_ios = 0;  // 0 when the TMD names no IOS
    std::uint16_t boot_index = 0;    // the content the system starts
    // The two-letter maker code (the TMD's group ID, "01" for Nintendo),
    // empty when its bytes are not letters or digits.
    std::string maker;
    std::vector<TmdContent> contents;
};

// The content records of a TMD as the Wii stores it (with its signature
// in front, as ES_GetStoredTMD returns it). False when it is too small
// for its own content count.
bool parse_tmd_contents(const std::uint8_t* tmd, std::size_t size, TmdContents& out, std::string& error);

// The six-character ID RiftWii keys a channel by, as a disc game's is its
// four-letter code and its maker ("WBME01"): favourites, play history,
// the stored banner and covers. The code alone gets "01" without a maker.
std::string channel_game_id(const std::string& code, const std::string& maker);

// The record with `index`, or nullptr.
const TmdContent* tmd_content_by_index(const TmdContents& tmd, std::uint16_t index);

// The NAND path of a title's content: "/title/00010001/57424d45/content/00000000.app".
// A shared content (type bit 0x8000) lives in /shared1 under a name that
// only /shared1/content.map knows, so this is only for the title's own ones.
std::string nand_content_path(std::uint64_t title_id, std::uint32_t content_id);

}  // namespace riftwii
