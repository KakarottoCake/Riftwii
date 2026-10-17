// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace riftwii {

// GameCube memory card images, as the native GameCube loader keeps them
// (sd:/riftwii/gc/cards/<ID>.raw) and as Dolphin and Nintendont read
// them: the card's flash, byte for byte, in 8 KB blocks. Block 0 is the
// header, blocks 1 and 2 two copies of the directory, blocks 3 and 4 two
// copies of the block allocation table (BAT); saves take the rest.
//
// A card image is untrusted (a tester's SD card, a file from the web):
// every function here checks what it reads and answers false with a plain
// sentence in `error` instead of reading or writing out of bounds.

constexpr std::uint32_t kGcCardBlockBytes = 0x2000;
constexpr std::uint32_t kGcCardSystemBlocks = 5;    // header, 2 directories, 2 BATs
constexpr std::uint32_t kGcCardEntryBytes = 64;     // one directory entry, also a .gci's header
constexpr std::uint32_t kGcCardMaxEntries = 127;    // entries in a directory block
constexpr std::uint32_t kGcIconSide = 32;           // icons are 32x32

// What a new card's header is made from. On a Wii the loader reads these
// from the console (gcnative.cpp's format_card); a test sets them by hand.
struct GcCardFormat {
    std::array<std::uint8_t, 12> flash_id{};  // SRAM's flash ID for the card's slot
    std::uint32_t counter_bias = 0;           // SRAM's clock bias
    std::uint32_t language = 0;               // SRAM's language
    std::uint64_t format_time = 0;            // OS time base ticks at the format
    std::uint32_t dtv = 0;                    // the video interface's DTV register (0xCC00206E)
    std::uint16_t size_mbits = 16;            // 16 Mbit: 2 MB, 251 blocks for saves
    bool shift_jis = false;                   // the card's text encoding: Shift-JIS (Japan) or Windows-1252
};

// A formatted, empty card, as the SDK's CARDFormat leaves one: the header
// with its serial made from the flash ID and the format time, two empty
// directories and two empty block tables. `card` is resized to the card.
void gc_card_format(std::vector<std::uint8_t>& card, const GcCardFormat& format);

// The card's 16-bit checksum pair over `bytes` (an even count), stored as
// two big-endian words at `out`: the sum of the words and the sum of
// their complements, each 0xFFFF written as 0.
void gc_card_checksum(const std::uint8_t* data, std::uint32_t bytes, std::uint8_t* out);

// One save in the directory.
struct GcSave {
    unsigned slot = 0;                  // directory index, 0..126
    std::string game_code;              // 4 characters ("GALE")
    std::string maker;                  // 2 characters ("01")
    std::string file_name;              // up to 32 bytes, in the card's encoding
    std::string comment_title;          // the comment's first line, up to 32 bytes
    std::string comment_detail;         // its second line
    std::uint16_t first_block = 0;
    std::uint16_t block_count = 0;
    std::uint32_t modified = 0;         // seconds since 2000-01-01 00:00
    std::uint8_t banner_format = 0;     // entry byte 7: bits 0-1 banner (0 none, 1 CI8, 2 RGB5A3), bit 2 animation
    std::uint32_t image_offset = 0;     // the banner and icons, bytes into the save
    std::uint16_t icon_format = 0;      // 2 bits per icon frame, 8 frames
    std::uint16_t icon_speed = 0;       // 2 bits per icon frame
    std::uint8_t permissions = 0;
    std::uint8_t copy_count = 0;
    std::uint32_t comment_offset = 0;   // bytes into the save
    // False when the save's blocks are out of range, loop, are shared with
    // another save or do not add up to its block count. It can still be
    // deleted, but not exported, and its comment is left empty.
    bool intact = false;
    std::array<std::uint8_t, kGcCardEntryBytes> entry{};  // the raw directory entry
};

struct GcCardInfo {
    std::uint32_t blocks = 0;           // the whole card, system blocks included
    std::uint32_t free_blocks = 0;      // blocks the BAT marks free
    std::uint32_t free_entries = 0;     // unused directory slots
    bool shift_jis = false;
    unsigned directory_copy = 0;        // which directory (0: block 1, 1: block 2) is in use
    unsigned bat_copy = 0;              // which BAT (0: block 3, 1: block 4) is in use
    std::vector<GcSave> saves;          // in directory order
};

// Reads the card: the header, and of each pair of directories and BATs
// the copy whose checksum holds, the newer by update counter when both do.
bool gc_card_read(const std::vector<std::uint8_t>& card, GcCardInfo& out, std::string& error);

// How full a card is, read from its first five blocks alone (the header,
// the directories and the block tables), `card_bytes` being the whole
// card's length: what the game page shows without reading a whole card.
struct GcCardSpace {
    std::uint32_t blocks = 0;        // the whole card, system blocks included
    std::uint32_t free_blocks = 0;
    std::uint32_t free_entries = 0;
    // No block or no directory slot left: no game can make a new save on
    // it (one that only rewrites its own save may still manage).
    bool full() const { return free_blocks == 0 || free_entries == 0; }
};
bool gc_card_space(const std::vector<std::uint8_t>& system_blocks, std::uint64_t card_bytes, GcCardSpace& out,
                   std::string& error);

// Frees the save in directory `slot`, clears its entry and writes the
// changed directory and BAT into the copies not in use, with update
// counters one past the current ones, so an update cut off halfway leaves
// the old copies in charge.
bool gc_card_delete(std::vector<std::uint8_t>& card, unsigned slot, std::string& error);

// The save in `slot` as a .gci: its 64-byte directory entry, then its
// blocks in file order.
bool gc_card_export(const std::vector<std::uint8_t>& card, unsigned slot, std::vector<std::uint8_t>& gci,
                    std::string& error);

// Adds a .gci's save to the card. Refused when the same game code, maker
// and file name are already there, or blocks or directory slots run out.
// The save's blocks are taken after the BAT's last allocated block, as
// the SDK takes them; `slot_out`, when given, gets its directory slot.
bool gc_card_import(std::vector<std::uint8_t>& card, const std::vector<std::uint8_t>& gci, std::string& error,
                    unsigned* slot_out = nullptr);

// The save's first icon frame as 32x32 RGBA8 (4096 bytes, rows top
// down). False when the save has no icon or its icon does not fit in it.
bool gc_card_icon(const std::vector<std::uint8_t>& card, unsigned slot, std::vector<std::uint8_t>& rgba,
                  std::string& error);

// A 32x32 RGBA8 icon (gc_card_icon's) as a GX RGB5A3 texture, 4x4 tiles,
// 2048 bytes, for the menu to draw. Empty when `rgba` is not 4096 bytes.
std::vector<std::uint8_t> gc_icon_rgb5a3(const std::vector<std::uint8_t>& rgba);

// A card's text (a GcSave's file name or comment, the card's raw bytes)
// as UTF-8 for the menu: Shift-JIS when the card says so (GcCardInfo::
// shift_jis; its symbols, kana and first-level kanji, the characters the
// menu font has), else Windows-1252. What is not a character, or not in
// the table, comes out as '?'.
std::string gc_card_text_utf8(const std::string& raw, bool shift_jis);

// Which file is a game's memory card, by its ID (letters and digits kept;
// anything else is dropped so an ID never climbs out of the folder).
// Not shared: sd:/riftwii/gc/cards/<ID>.raw. Shared: one card for all the
// games of the same text encoding, shared.raw or, for a Japanese game
// (region letter J, the ID's fourth character, the one the loader
// formats as Shift-JIS), shared-jp.raw. A card holds one encoding's
// file names, so Western and Japanese games cannot share one file.
std::string gc_card_path(const std::string& game_id, bool shared);

// Whether the game's card is Shift-JIS (a Japanese game's region letter).
bool gc_card_shift_jis(const std::string& game_id);

}  // namespace riftwii
