// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
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

// ---------------------------------------------------------------------------
// Bringing the saves from Nintendont. With its memory card emulation on,
// Nintendont keeps raw card images in /saves on the device it loads from, so
// RiftWii looks in sd:/saves and usb:/saves:
//
//   <ID4>.raw                  one card for that game ("GMSE.raw": the
//                              four-letter ID, no maker code)
//   ninmem.raw, ninmemj.raw    the "Multi" card of every USA and PAL game, and
//                              of the Japanese and Korean ones
//   <ID4>_B.raw, ninmemb.raw, ninmemjb.raw   the same for slot B (rare builds)
//
// Each is an ordinary raw card image, as Dolphin's. The same folder holds
// Triforce arcade files (GP1.bin, AX.bin, *settings.bin...): not cards, so
// nothing but the names above is looked at. The folder is on a player's
// drive, so it is untrusted like Dolphin's: the files and their sizes are
// bounded and only plain names are joined onto the folder. Nintendont's
// files are only ever read.

constexpr std::size_t kGcNintendontMaxCards = 32;            // card files looked at in all the folders
constexpr std::uint64_t kGcNintendontMinBytes = 512u << 10;  // the smallest card (59 blocks, 0.5 MB)
// The largest is kGcDolphinMaxRawBytes (16 MB, 2043 blocks).

// Where the cards are, in the order they are listed.
constexpr const char* kGcNintendontFolders[] = {"sd:/saves", "usb:/saves"};

// One entry of a folder, as a lister reports it.
struct GcFolderEntry {
    std::string name;
    bool is_directory = false;
    std::uint64_t bytes = 0;
};

// Lists `folder` for the scan: false when it is not there. The Wii's
// lister reads usb: through the menu's view of the drive (it is not a
// mounted file system); gc_posix_lister reads any folder with opendir.
using GcFolderLister = std::function<bool(const std::string& folder, std::vector<GcFolderEntry>& out)>;
bool gc_posix_lister(const std::string& folder, std::vector<GcFolderEntry>& out);

// Whether `name` is one Nintendont gives a memory card, compared without
// regard to case (the drive's file system does the same): an ID of four
// letters or digits with ".raw" or "_B.raw", or ninmem, ninmemj, ninmemb
// or ninmemjb with ".raw". Nothing else, so GP1.bin is not.
bool gc_nintendont_name_ok(const std::string& name);

struct GcNintendontCard {
    std::string path;  // "sd:/saves/GMSE.raw"
    std::string name;  // "GMSE.raw"
    std::uint64_t bytes = 0;
};

struct GcNintendontFiles {
    std::vector<GcNintendontCard> cards;  // in folder order, each folder's by name
    unsigned wrong_size = 0;              // named like a card, but under 0.5 MB, over 16 MB or not whole blocks: left out
    bool truncated = false;               // more cards or entries than the limits above: some were left out
};

// The cards in `folders` (here: kGcNintendontFolders), listed with `list`.
GcNintendontFiles gc_nintendont_scan(const std::vector<std::string>& folders, const GcFolderLister& list);

// One save found in a Nintendont card, to show and to pick.
struct GcNintendontSave {
    std::size_t card = 0;       // index in GcNintendontFiles::cards
    unsigned slot = 0;          // directory slot in that card
    std::string id;             // the game's ID, code and maker (six characters), from the save's own entry
    std::string file_name;      // the save's file name, in the card's encoding
    std::string comment_title;  // the save's comment, in the card's encoding
    std::uint16_t blocks = 0;
    bool intact = false;        // false: the save's blocks are damaged and it cannot be brought
    bool shift_jis = false;     // the card's encoding
    std::array<std::uint8_t, 64> entry{};  // the directory entry as listed: a later read must still find it
};

// Appends the saves of the card image at `card` (`card_index` is its place
// in the scan) to `out`. False, with a sentence in `error`, when it cannot
// be read as a memory card. The image is only read.
bool gc_nintendont_list(std::size_t card_index, const std::uint8_t* card, std::size_t card_bytes,
                        std::vector<GcNintendontSave>& out, std::string& error);

// The RiftWii card file a save goes to, by the six-character ID of its
// own directory entry, never by the name of the Nintendont file (a
// "GMSE.raw" can hold a second maker's game, and ninmem.raw holds many):
// sd:/riftwii/gc/cards/<ID>.raw, or shared.raw / shared-jp.raw when that
// game's memory card setting is Shared (`shared`).
std::string gc_nintendont_route(const GcNintendontSave& save, bool shared);

// A new, empty RiftWii card for a save's game, formatted like the card the
// save came from (the first 0x200 bytes of `source` give its flash ID) in
// the text encoding `shift_jis`.
void gc_nintendont_blank_card(std::vector<std::uint8_t>& card, const std::uint8_t* source, std::size_t source_bytes,
                              bool shift_jis, std::uint64_t format_time);

// Adds `save` from the card image at `source` to `target` as
// gc_import_one counts it: already there is skipped; a save the card no
// longer has as listed (the file changed since), a damaged one and one
// whose card text encoding differs from `target`'s are unusable.
void gc_nintendont_import(std::vector<std::uint8_t>& target, const std::uint8_t* source, std::size_t source_bytes,
                          const GcNintendontSave& save, GcImportCount& count);

}  // namespace riftwii
