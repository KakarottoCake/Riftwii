// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// CTGP-R's channel program, decoded from the player's own install
// (docs/CTGPR.md, "The cup layouts"). The default cup layout exists only in
// the channel program, which /PACKAGES/CHAN/MAIN.DOL on the package disk
// carries packed: MAIN.DOL builds a 256-byte substitution table when it runs,
// maps the packed bytes through it and inflates them. RiftWii runs that
// table-building code in a small sandboxed interpreter (only the instructions
// it uses, a hard step limit, memory limited to the DOL's own sections, a
// stack and one low-memory handler), then maps and inflates the payload itself
// and keeps only the channel's cup-layout table.
namespace riftwii {

// Why a decode stopped. Unsupported: code or a layout the decoder does not
// know (a CTGP-R version RiftWii cannot start yet). Damaged: data that does
// not decode (a damaged install).
enum class CtgpStage2Result { Ok, Unsupported, Damaged };

// A DOL's sections, as offsets into the file the caller keeps.
struct CtgpDolSection {
    std::uint32_t address = 0;
    std::uint32_t size = 0;
    std::uint32_t offset = 0;  // in the file
    bool text = false;
};
struct CtgpDolLayout {
    std::vector<CtgpDolSection> sections;  // text then data, empty ones left out
    std::uint32_t bss_address = 0;
    std::uint32_t bss_size = 0;
};
bool ctgp_parse_dol(const std::uint8_t* dol, std::size_t size, CtgpDolLayout& out, std::string& error);

// The interpreter, public for the tests. Its memory is the DOL's sections
// (the file's bytes, never changed), its BSS (zeros), a stack below
// kCtgpPpcStackTop, and the system-call handler CTGP-R's loader leaves at
// 0x80000c00. Stores go to an overlay. The model keeps what the table builder
// depends on:
//   - a data view (every store) and a memory view (only what a data-cache
//     flush, dcbf or dcbst, has written back); instruction fetches read the
//     memory view;
//   - an instruction cache: a 32-byte line, once fetched, keeps its bytes
//     until icbi invalidates it, so code stored over a cached line is not run;
//   - the breakpoint registers as storage: IABR keeps bits 0-30 (bit 31 reads
//     back as 0), DABR all 32 bits;
//   - sc runs that handler: r3 = HID0, r4 = HID0 | 8.
constexpr std::uint32_t kCtgpPpcStackTop = 0x81000000;
constexpr std::uint32_t kCtgpPpcStackBytes = 64 * 1024;
constexpr std::uint32_t kCtgpPpcHid0 = 0x0011c264;
constexpr std::uint32_t kCtgpPpcMaxLines = 8192;  // overlay cap: 256 KB per view

class CtgpPpc {
public:
    CtgpPpc(const std::uint8_t* dol, const CtgpDolLayout& layout);

    std::uint32_t r[32] = {};
    std::uint32_t lr = 0, ctr = 0, xer = 0, cr = 0, pc = 0;

    // Steps until pc == stop. False (with `error`) on an instruction it does
    // not know, an access outside its memory, or `max_steps` reached.
    bool run_until(std::uint32_t stop, std::uint32_t max_steps, std::string& error);
    std::uint32_t steps() const { return steps_; }
    // The data view, as a load sees it. False outside its memory.
    bool read(std::uint32_t address, std::uint8_t* out, std::uint32_t length) const;
    // True when a store changed any line of [address, address + length).
    bool wrote_into(std::uint32_t address, std::uint32_t length) const;

private:
    using Line = std::array<std::uint8_t, 32>;
    bool mapped(std::uint32_t address) const;  // inside the decoder's memory
    bool backing(std::uint32_t line, Line& out) const;  // the start state of a line
    bool view(std::uint32_t line, const std::map<std::uint32_t, Line>& layer, Line& out) const;
    bool load(std::uint32_t address, std::uint32_t length, std::uint32_t& value);
    bool store(std::uint32_t address, std::uint32_t length, std::uint32_t value);
    bool flush(std::uint32_t address);
    bool fetch(std::uint32_t address, std::uint32_t& word);
    bool step(std::string& error);

    const std::uint8_t* dol_;
    CtgpDolLayout layout_;
    std::map<std::uint32_t, Line> data_;    // stored lines (the data view)
    std::map<std::uint32_t, Line> memory_;  // written-back lines (the memory view)
    std::map<std::uint32_t, Line> icache_;
    std::uint32_t iabr_ = 0, dabr_ = 0;
    std::uint32_t steps_ = 0;
    std::string fault_;
};

// What the table builder left: the table, and the packed payload it maps.
struct CtgpStage2Table {
    std::array<std::uint8_t, 256> table{};
    std::uint32_t payload_address = 0;
    std::uint32_t payload_length = 0;
    std::uint32_t payload_offset = 0;  // in the DOL file
    std::uint32_t steps = 0;           // instructions the builder ran
};

// The builder's step limit. CTGP-R 1.03's builder runs about 1,400.
constexpr std::uint32_t kCtgpStage2MaxSteps = 200000;

// Finds MAIN.DOL's substitution loop and the function that holds it, runs that
// function in the interpreter up to the loop, and returns the table it built
// (which must hold every byte value once) and the payload the loop maps.
CtgpStage2Result ctgp_stage2_table(const std::uint8_t* dol, std::size_t size, CtgpStage2Table& out,
                                   std::string& error);

// The channel's cup-layout table: three layouts of kCtgpCupEntryBytes (a group
// count word, then 216 track bytes), as the channel's settings step indexes
// them by cup_course_layout_using.
constexpr std::uint32_t kCtgpCupEntryBytes = 0xdc;
constexpr std::uint32_t kCtgpChannelBase = 0x80800000;  // where the channel program is placed
struct CtgpChannelCups {
    std::uint32_t address = 0;  // of the table, in the channel program
    std::array<std::uint8_t, 3 * kCtgpCupEntryBytes> entries{};
    std::uint32_t inflated = 0;  // bytes the payload inflated to
};

// Maps the payload of `dol` (changed in place) through the table and inflates
// it a chunk at a time, keeping only the cup-layout table. The table is found
// through the channel's settings step: the one place that loads a word from
// `table + layout * 0xdc` and stores it to the game's layout count
// (0x816ffed8). The deflate stream must end cleanly, exactly one such place
// must be found, and the table must lie inside what was inflated.
CtgpStage2Result ctgp_stage2_cups(std::uint8_t* dol, std::size_t size, const CtgpStage2Table& table,
                                  CtgpChannelCups& out, std::string& error);

}  // namespace riftwii
