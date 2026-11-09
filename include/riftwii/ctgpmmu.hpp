// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/ctgpimage.hpp"

// The native CTGP-R launch's memory map: the one the CTGP-R Channel gives
// the game (chan_internal 0x80957504.. builds the page table with the PTE
// insert at 0x809557a4; the entry routine 0x80874c14 loads SDR1, the segment
// registers and the BATs). Pure code: the launcher writes the table and the
// registers right before the jump.
//
// Why the game needs it: CTGP's core (BOAT 0x81797ff4) copies the first
// 8 KB of low memory to 0x81700000 and rewrites the exception vectors to keep
// the OS globals at physical 0x01700000. The channel's page table maps
// 0x80000000-0x80001FFF there, and the only BAT over low memory at the jump
// is DBAT7 (0x80000000, 32 MB), which the core replaces with the loader words
// at 0x816FFACC/D0 once its copy is made. With a BAT over low memory instead,
// the OS writes physical 0 while the vectors read 0x01700000 and thread
// switching breaks (the main thread stays READY).
//
// Differences from the channel: no random MMU bias, so no mirror of the game
// at the bias and fixed VSIDs (the channel draws them at random); the BATs
// for 0x9, 0xC, 0xD, 0xE and 0xF stay as RiftWii sets them (they win over the
// page table for those segments).
namespace riftwii {

constexpr std::uint32_t kCtgpHtabBase = 0x81600000;  // physical 0x01600000, 128 KB (SDR1 0x01600001)
constexpr std::uint32_t kCtgpHtabSize = 0x20000;
constexpr std::uint32_t kCtgpLowCopy = 0x01700000;   // where the core keeps low memory (physical)
constexpr std::uint32_t kCtgpPageCopy = 0x01702000;  // the core's copy of one game page (physical)

struct CtgpMmu {
    std::vector<std::uint8_t> htab;   // kCtgpHtabSize bytes for kCtgpHtabBase
    std::uint32_t sdr1 = 0;
    std::array<std::uint32_t, 16> sr{};
    // DBAT0 and DBAT7 at the jump (DBAT7 is the start-up mapping the core swaps out).
    std::uint32_t dbat0u = 0, dbat0l = 0, dbat7u = 0, dbat7l = 0;
    // What the core then loads into DBAT7 (loader words 0x816FFACC/D0).
    std::uint32_t core_dbat7u = 0, core_dbat7l = 0;
    std::size_t entries = 0;          // PTEs written
};

// `low` = the row table's 54 region words (CtgpRowTable::low). Fails when the
// table is missing or a PTE finds no free slot.
bool ctgp_build_mmu(const std::vector<std::uint32_t>& low, CtgpRegion region, CtgpMmu& out, std::string& error);

// Where a data access to `ea` lands through `mmu`'s page table (the BATs
// left out), or false if no PTE maps it. For the tests.
bool ctgp_mmu_translate(const CtgpMmu& mmu, std::uint32_t ea, std::uint32_t& pa, bool& read_only);

}  // namespace riftwii
