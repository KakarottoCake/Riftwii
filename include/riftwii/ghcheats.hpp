// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

#include "riftwii/zipread.hpp"

// GameHacking.org's archive of Gecko codes (https://gamehacking.org/text/),
// where the codes went after geckocodes.org shut down. RiftWii asks it only
// when the player wants cheats for a Wii game that codes.rc24.xyz has none
// for (or does not answer): one request for the directory listing and one
// for the newest text_wii_YYYYMMDD.zip, then at most once in about 30 days
// (the site asks not to be crawled: robots.txt sets Crawl-delay: 30). The
// zip is kept on the SD card with an index of which entry holds which game,
// and every game after that is answered from there.
//
// All of it comes from the internet and is untrusted: the listing, the zip
// and the text files in it are bounded and checked before they are used.
namespace riftwii {

constexpr const char* kGameHackingName = "GameHacking.org";
constexpr const char* kGameHackingTextUrl = "https://gamehacking.org/text/";

// What RiftWii takes from the archive. The real Wii zip (2026-09) is 0.7 MB
// packed, 4 MB unpacked, 464 files, the biggest 269 KB bar one of 2.2 MB
// (Monster Hunter Tri, USA), which is left out; the bounds are well above
// that and well below anything that could hurt a Wii.
struct GameHackingLimits {
    std::size_t listing_bytes = 128u << 10;       // the directory page (43 KB with its ads)
    std::size_t zip_bytes = 4u << 20;             // the download
    std::size_t entries = 4000;                   // in the zip
    std::uint64_t total_unpacked = 16u << 20;     // all entries together, as the zip declares
    std::uint32_t entry_unpacked = 1u << 20;      // one entry: a bigger one is left out, like rc24's 1 MB
};

// A cached copy older than this is looked for again (when a game needs it).
constexpr std::int64_t kGameHackingMaxAge = 30 * 24 * 60 * 60;

// The newest "text_wii_YYYYMMDD.zip" the listing links to, as its name.
// Dates must be real (month 1-12, day 1-31); a listing without one fails.
bool gamehacking_newest_wii_zip(const std::string& listing, std::string& name, std::string& error);
// Whether `name` is exactly "text_wii_YYYYMMDD.zip" (and may be put in an address
// or a file name).
bool gamehacking_zip_name_ok(const std::string& name);

// Whether a cache fetched at `fetched` (seconds since 1970) is due for
// another look at `now`. A clock that went backwards never makes it due: the
// archive is asked less often that way, not more.
bool gamehacking_stale(std::int64_t fetched, std::int64_t now);

// Whether an entry name is a plain file name: no folder, no "..", no
// control character, not hidden, not too long. A zip with a name that is
// not is refused whole.
bool gamehacking_entry_name_ok(const std::string& name);

// The game ID on the first line of a cheat text ("RMCE01", or a channel's
// four letters): 4 or 6 capitals and digits, after an optional UTF-8 mark.
bool gamehacking_text_id(const char* text, std::size_t size, std::string& id);

// Which entry of the cached zip answers which game, and which zip that is.
struct GameHackingIndex {
    std::string zip;                          // the file's name on the SD card and on the site
    std::int64_t fetched = 0;                 // seconds since 1970, when it was fetched or last confirmed
    std::map<std::string, std::size_t> entry; // game ID -> index in the zip's directory
    std::size_t left_out = 0;                 // entries not indexed (too big, not .txt, no ID line)
};

// Reads every entry of the zip in `data` once (through `inflate`, CRC
// checked) for its ID line. A game with several files (revisions) gets the
// biggest. Fails for a zip over a bound or with a name that is not plain.
bool gamehacking_build_index(const std::uint8_t* data, std::size_t size, const GameHackingLimits& limits,
                             ZipInflater inflate, GameHackingIndex& index, std::string& error);

// The index as the text file kept beside the zip, and back (false for
// anything that is not exactly that format).
std::string gamehacking_index_text(const GameHackingIndex& index);
bool gamehacking_parse_index(const std::string& text, GameHackingIndex& index, std::string& error);

// The cheat text of `id` out of the zip in `data`, by `index`. The entry is
// read again and its first line must name `id`.
bool gamehacking_read_cheats(const std::uint8_t* data, std::size_t size, const GameHackingIndex& index,
                             const std::string& id, const GameHackingLimits& limits, ZipInflater inflate,
                             std::string& text, std::string& error);

// The note kept beside a game's cheat file (sd:/riftwii/cheats/<ID>.source)
// when its codes came from GameHacking.org, so the cheats page can say so
// every time it opens. The cheat file itself is left as it came: it is the
// player's to edit and the merge of their own cheats works on it. A download
// from the other archive removes the note.
std::string gamehacking_source_text(const std::string& zip);
// The zip name in such a note; false for anything else.
bool gamehacking_source_parse(const std::string& text, std::string& zip);

}  // namespace riftwii
