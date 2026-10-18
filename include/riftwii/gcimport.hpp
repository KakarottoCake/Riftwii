// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace riftwii {

// Bringing the saves from Dolphin's GC folder into a GameCube memory card
// (riftwii/gccard.hpp). The player copies Dolphin's "GC" folder to
// sd:/riftwii/gc/dolphin/ and RiftWii reads from it:
//
//   <folder>/USA/Card A/*.gci     a "GCI folder" card: one file per save
//   <folder>/EUR/Card A/*.gci     (Dolphin names them "<maker>-<game>-<name>.gci"
//   <folder>/JAP/Card A/*.gci      and keeps one folder per region and slot)
//   <folder>/MemoryCardA.USA.raw  a "memory card file" card, per region
//
// The folder may also hold the GC folder itself (<folder>/GC/USA/...), as
// copying the folder from a PC makes it. Everything in it is untrusted: the
// number and the size of the files are bounded, and only plain file names
// are ever joined onto a folder.

constexpr std::size_t kGcDolphinMaxGci = 400;    // save files read in one go
constexpr std::size_t kGcDolphinMaxRaw = 8;      // memory card files read in one go
constexpr std::size_t kGcDolphinMaxEntries = 4000;  // directory entries looked at in one folder
// The largest a .gci can be: its 64-byte entry and a whole card's blocks.
constexpr std::uint64_t kGcDolphinMaxGciBytes = 64 + 2043ull * 0x2000;
constexpr std::uint64_t kGcDolphinMaxRawBytes = 16u << 20;

// Dolphin keeps a card per region, under these names.
enum class GcDolphinRegion { Usa, Eur, Jap };

// "USA", "EUR" or "JAP": the folder's name and the infix of a raw card's.
const char* gc_dolphin_region_name(GcDolphinRegion region);

// The regions whose saves belong on the card a game uses. A game's own
// card takes its own region (the ID's fourth letter: E USA, J and K Japan,
// the rest Europe). A shared card takes every region of its text encoding:
// Japanese for shared-jp, USA and Europe for shared.
std::vector<GcDolphinRegion> gc_dolphin_regions(const std::string& game_id, bool shared);

// Whether `name` is one plain file or folder name that may be joined onto a
// folder: not empty, no leading dot (so not "." or "..", and not the "._"
// files macOS leaves beside copies, which are never saves), no slash,
// backslash or colon, no control character, at most 255 bytes.
bool gc_import_name_ok(const std::string& name);

// What a scan of the folder found: full paths, each list sorted.
struct GcDolphinFiles {
    bool folder_exists = false;
    std::vector<std::string> gci;  // save files of the wanted regions
    std::vector<std::string> raw;  // memory card files of the wanted regions
    bool truncated = false;        // more files or entries than the limits above: some were left out
};

// Whether the folder is there to read from.
bool gc_dolphin_folder_exists(const std::string& folder);

// Looks for the saves of `regions` in `folder`: .gci files in
// <region>/Card A and Card B (here or under GC/), and .raw cards named for
// the region or for none, in the folder, GC/ or the region's folder.
GcDolphinFiles gc_dolphin_scan(const std::string& folder, const std::vector<GcDolphinRegion>& regions);

// What bringing saves into a card came to.
struct GcImportCount {
    unsigned added = 0;
    unsigned skipped = 0;     // already on the card: same game, maker and file name
    unsigned unusable = 0;    // not a save, damaged, or could not be read
    bool full = false;        // the card ran out of blocks or directory slots: no more are tried
};

// Adds one .gci's save to `card`. A save already there is counted as
// skipped and a bad file as unusable. When the card has no room, `full` is
// set and nothing is added; the caller stops there. Does nothing once
// `count.full` is set.
void gc_import_one(std::vector<std::uint8_t>& card, const std::vector<std::uint8_t>& gci, GcImportCount& count);

// Adds every save of the memory card image `raw` (a Dolphin .raw card) to
// `card`, each as gc_import_one counts it; a card that cannot be read, and
// a save of it whose blocks are damaged, count as unusable. Input over
// kGcDolphinMaxGciBytes / kGcDolphinMaxRawBytes is unusable too.
void gc_import_card(std::vector<std::uint8_t>& card, const std::vector<std::uint8_t>& raw, GcImportCount& count);

}  // namespace riftwii
