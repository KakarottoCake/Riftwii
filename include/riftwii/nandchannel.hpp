// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/dol.hpp"
#include "riftwii/sha1.hpp"

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

// Whether `code` is one of the known homebrew channels' (the Homebrew
// Channel's LULZ and JODI, RiftWii's UFTW).
bool is_homebrew_code(const std::string& code);

// A forwarder channel's program is a few tens of KB; an inject's holds an
// emulator and a game, megabytes. Programs above this are never forwarders,
// and the listing reads no more than this much of one.
constexpr std::size_t kForwarderMaxBytes = 256 * 1024;

// The app path a forwarder's program names: the run of printable text
// around the first "/apps/", "boot.dol" or "boot.elf" in `bytes`, cut to a
// bounded length, or empty when there is none.
std::string forwarder_app_path(const std::uint8_t* bytes, std::size_t size);

// Retail games and Nintendo's Virtual Console emulators (which every
// inject uses) load their program from 0x80004000 or close to it and stay
// below about 0x80900000. Homebrew loaders and forwarders often place
// themselves high, to leave room for what they load: Retro Rewind's
// channel loads at 0x81000000. A DOL with any section at or above this is
// taken for homebrew. 0x80003100 (libogc's default start) is below it on
// purpose: nothing here shows retail never uses it, so it counts as a game.
constexpr std::uint32_t kHomebrewLoadAddress = 0x80A00000;

struct ChannelVerdict {
    bool left_out = false;  // not listed on Home
    std::string reason;     // for the log, when left out
};

// One of a TMD's content records.
struct TmdContent {
    std::uint32_t id = 0;     // the file's name on the NAND, %08x.app
    std::uint16_t index = 0;  // 0 is the banner, the boot index the program
    std::uint16_t type = 0;   // 1 normal, 0x4001 and 0x8001 shared
    std::uint64_t size = 0;
    Sha1Digest sha1{};        // of its plain bytes
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

// The GeckoCodes archive keys WiiWare and Virtual Console games by their
// four-letter code alone ("NAAE" for Super Mario 64, "WGOE" for World of
// Goo; "NAAE01" is not there): that code for a channel game's
// six-character ID, empty for any other ID.
std::string channel_cheat_code(const std::string& game_id);

// The record with `index`, or nullptr.
const TmdContent* tmd_content_by_index(const TmdContents& tmd, std::uint16_t index);

// The NAND path of a title's content: "/title/00010001/57424d45/content/00000000.app".
// A shared content (type bit 0x8000) lives in /shared1 under a name that
// only /shared1/content.map knows, so this is only for the title's own ones.
std::string nand_content_path(std::uint64_t title_id, std::uint32_t content_id);

// The first bytes of one of a title's own contents (at least the DOL
// header's 0x100 when the file is that long), for choose_channel_program.
struct ContentStart {
    std::uint16_t index = 0;
    std::uint64_t size = 0;  // the whole file's
    std::vector<std::uint8_t> head;
};

// The program RiftWii loads itself in place of the system: the content
// whose DOL starts the game, and its header.
struct ChannelProgram {
    std::uint16_t index = 0;
    DolHeader dol;
    // True when it is not the boot index's content: the boot content was a
    // smaller DOL, a loader that starts this one.
    bool past_loader = false;
    std::string format;  // parse_channel_program's: "DOL", "ELF", ...
};

// Which content to load: the biggest of the title's own contents (not the
// banner, not a shared one) that is a whole, valid DOL. Many WiiWare games
// boot a small loader that reads the real program from another content;
// loading the biggest DOL skips the loader and starts the game itself, so
// RiftWii's patches go into the code that runs. Virtual Console games boot
// their emulator, and their other contents are archives, not DOLs. False
// when no content is a DOL (a compressed program, say), with `error`
// saying so: the system then starts the title as the Wii Menu does.
bool choose_channel_program(const TmdContents& tmd, const std::vector<ContentStart>& starts, ChannelProgram& out,
                            std::string& error);

// Whether a channel GameTDB does not list stays off Home, judged from the
// title itself. `starts` are the first bytes of its own contents (the
// banner and shared ones need none); the boot content's are up to
// kForwarderMaxBytes, so a small forwarder's text is in them. In order:
//   1. a known homebrew code: left out;
//   2. no readable boot content (missing, shared, or the read failed): left
//      out, as it cannot be started or judged;
//   3. the program RiftWii's loader would take (choose_channel_program) is
//      a DOL with a section at or above kHomebrewLoadAddress: a homebrew
//      loader, left out;
//   4. the boot content is at most kForwarderMaxBytes and names an app to
//      start: a forwarder, left out;
//   5. anything else is a game: a custom Virtual Console inject takes a
//      made-up code.
// An ELF or a program that is no DOL is judged by rule 4 only.
ChannelVerdict judge_unlisted_channel(const std::string& code, const TmdContents& tmd,
                                      const std::vector<ContentStart>& starts);

// The lowest MEM1 address RiftWii's extras (screenshots, the crash
// recorder, the GameCube adapter, the Wii Remote log) may take in a channel
// game RiftWii's loader starts. A channel has no FST: its MEM1 arena runs to
// the top of MEM1, and the extras' blobs are carved from that top down to
// this floor. It is above RiftWii's own memory (`loader_end`), which is in
// use until the game starts, and above every part of the program (sections
// and BSS) that loads past it, rounded up to 32 bytes. Parts below
// `loader_end` (the usual case, from 0x80003400 up) do not move it.
std::uint32_t channel_extras_floor(const DolHeader& program, std::uint32_t loader_end);

}  // namespace riftwii
