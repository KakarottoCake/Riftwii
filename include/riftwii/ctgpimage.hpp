// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The image CTGP-R's launch step builds in memory (docs/CTGPR.md): the
// package code files linked at their load addresses and the branches that
// hook the game. Pure functions.
namespace riftwii {

constexpr std::uint32_t kCtgpBootBase = 0x81620000;  // core/BOOT.BIN (data)
constexpr std::uint32_t kCtgpBoatBase = 0x81760000;  // core/BOAT.BIN (code)
constexpr std::uint32_t kCtgpMem1End = 0x81800000;   // the top of MEM1 (24 MB)
// The real placement constraint: BOOT.BIN is placed at kCtgpBootBase and must
// end before BOAT.BIN's base, and BOAT.BIN is placed at kCtgpBoatBase and must
// end before MEM1's top. A file larger than its window cannot be placed, so it
// is refused before it is read (real sizes are BOOT ~0x121c34, BOAT ~0x5a418;
// these caps sit generously above those but safely below the overflow).
constexpr std::uint32_t kCtgpBootMax = kCtgpBoatBase - kCtgpBootBase;  // 0x140000
constexpr std::uint32_t kCtgpBoatMax = kCtgpMem1End - kCtgpBoatBase;   // 0xa0000
constexpr std::size_t kCtgpRowCount = 2570;

// 0 NTSC-U, 1 NTSC-J, 2 PAL (the column of the row table).
enum class CtgpRegion : unsigned { E = 0, J = 1, P = 2 };

// The channel's table of game addresses: rows of three words (E, J, P). Built
// into RiftWii (src/ctgpr/ctgprowdata.cpp, made by tools/ctgpr/gen_rowdata.py)
// and parsed from that data in this layout: "CRT0", the row count, the rows and
// optionally 54 more words, all big endian.
struct CtgpRowTable {
    std::vector<std::uint32_t> words;  // 3 per row
    // 54 words that precede the rows in the channel (0x809e36c4..): game
    // addresses by region (E, J, P in threes) for the entry routine's patches. May be absent.
    std::vector<std::uint32_t> low;
    std::uint32_t row(std::size_t index, CtgpRegion region) const {
        return words[index * 3 + static_cast<unsigned>(region)];
    }
    std::size_t rows() const { return words.size() / 3; }
};
bool ctgp_parse_row_table(const std::uint8_t* data, std::size_t size, CtgpRowTable& out, std::string& error);

// Applies the relocation records of BEAT.BIN to BOOT.BIN and BOAT.BIN in
// place. Records are 8 bytes (u16 skip, u8 type, u8 section, u32 addend); the
// code is linked at kCtgpBoatBase + bias (the channel randomises it with the
// anti-cheat layer; the offline launch uses 0). Fails, with the images
// unspecified, on a record outside its section or on a branch/branch-
// conditional relocation whose target instruction is not a branch.
bool ctgp_link(std::vector<std::uint8_t>& boot, std::vector<std::uint8_t>& boat, const std::uint8_t* beat,
               std::size_t beat_size, std::uint32_t bias, std::string& error);

// One branch written over a game function.
struct CtgpHook {
    std::uint32_t row = 0;    // row of the game address table
    std::uint32_t site = 0;   // game address patched
    std::uint32_t dest = 0;   // where it branches (inside BOAT)
    bool link = false;        // bl
    std::uint32_t word = 0;   // the instruction written at `site`
};

struct CtgpImage {
    std::vector<std::uint8_t> boot;  // linked, slot tables filled in
    std::vector<std::uint8_t> boat;  // linked
    std::vector<CtgpHook> hooks;     // 37 of them, in the channel's order
    // Only installed when the HID flag (0x816ff9e8) is set; never offline.
    std::vector<CtgpHook> optional_hooks;
};

// Links, fills BOOT's slot tables (slot k = hook site k + 4, then the 2534
// game addresses from row 31) and works out the hooks for `region`.
bool ctgp_build_image(const std::vector<std::uint8_t>& boot_bin, const std::vector<std::uint8_t>& boat_bin,
                      const std::vector<std::uint8_t>& beat_bin, const CtgpRowTable& rows, CtgpRegion region,
                      CtgpImage& out, std::string& error);

}  // namespace riftwii
