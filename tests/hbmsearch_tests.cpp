// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The HOME Menu's functions found by their shape (riftwii/hbmsearch.hpp),
// on made-up code laid out as the SDKs lay it out: the KPAD read, WPADProbe,
// the small-data base and HBMInit with HBMCalc after it.
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/hbmsearch.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))

namespace {

// A few PowerPC encodings, enough for these functions.
std::uint32_t d_form(std::uint32_t op, std::uint32_t d, std::uint32_t a, std::int32_t imm) {
    return (op << 26) | (d << 21) | (a << 16) | (static_cast<std::uint32_t>(imm) & 0xFFFFu);
}
std::uint32_t stwu_r1(std::int32_t frame) { return d_form(37, 1, 1, -frame); }
std::uint32_t mulli(std::uint32_t d, std::uint32_t a, std::int32_t v) { return d_form(7, d, a, v); }
std::uint32_t addi(std::uint32_t d, std::uint32_t a, std::int32_t v) { return d_form(14, d, a, v); }
std::uint32_t li(std::uint32_t d, std::int32_t v) { return addi(d, 0, v); }
std::uint32_t lis(std::uint32_t d, std::uint32_t v) { return d_form(15, d, 0, static_cast<std::int32_t>(v)); }
std::uint32_t ori(std::uint32_t a, std::uint32_t s, std::uint32_t v) { return d_form(24, s, a, static_cast<std::int32_t>(v)); }
std::uint32_t lwz(std::uint32_t d, std::uint32_t a, std::int32_t v) { return d_form(32, d, a, v); }
std::uint32_t lbz(std::uint32_t d, std::uint32_t a, std::int32_t v) { return d_form(34, d, a, v); }
std::uint32_t stw(std::uint32_t s, std::uint32_t a, std::int32_t v) { return d_form(36, s, a, v); }
std::uint32_t stb(std::uint32_t s, std::uint32_t a, std::int32_t v) { return d_form(38, s, a, v); }
std::uint32_t cmplwi(std::uint32_t a, std::uint32_t v) { return d_form(10, 0, a, static_cast<std::int32_t>(v)); }
std::uint32_t mr(std::uint32_t a, std::uint32_t s) { return (31u << 26) | (s << 21) | (a << 16) | (s << 11) | (444u << 1); }
std::uint32_t lwzx(std::uint32_t d, std::uint32_t a, std::uint32_t b) { return (31u << 26) | (d << 21) | (a << 16) | (b << 11) | (23u << 1); }
std::uint32_t bl(std::uint32_t from, std::uint32_t to) { return (18u << 26) | ((to - from) & 0x03FFFFFCu) | 1u; }
std::uint32_t b(std::uint32_t from, std::uint32_t to) { return (18u << 26) | ((to - from) & 0x03FFFFFCu); }
constexpr std::uint32_t kBlr = 0x4E800020u;
constexpr std::uint32_t kSlwiR0R3By2 = 0x5460103Au;
constexpr std::uint32_t kMflrR0 = 0x7C0802A6u;

// A section built word by word, kept alive for the CodeRange that points at it.
struct Section {
    std::uint32_t address;
    std::vector<std::uint8_t> bytes;
    std::uint32_t here() const { return address + static_cast<std::uint32_t>(bytes.size()); }
    void put(std::uint32_t w) {
        bytes.push_back(static_cast<std::uint8_t>(w >> 24));
        bytes.push_back(static_cast<std::uint8_t>(w >> 16));
        bytes.push_back(static_cast<std::uint8_t>(w >> 8));
        bytes.push_back(static_cast<std::uint8_t>(w));
    }
    void put_text(const char* s) { bytes.insert(bytes.end(), s, s + std::strlen(s) + 1); }
    riftwii::CodeRange range() const { return {address, bytes.data(), bytes.size()}; }
};

// A ring walker over `size`-byte statuses, with the device type and error
// stores when `stores`, keeping r6 when `keeps_error`.
void put_ring_walker(Section& t, std::int32_t size, bool stores, bool keeps_error) {
    t.put(stwu_r1(0x40));
    t.put(kMflrR0);
    t.put(mr(27, 3));
    t.put(mr(28, 4));
    if (keeps_error) t.put(mr(30, 6));
    t.put(mulli(0, 27, size));
    for (int i = 0; i < 3; ++i) t.put(addi(31, 31, -size));
    if (stores) {
        t.put(stb(0, 31, 0x5C));
        t.put(stb(0, 31, 0x5D));
    }
    t.put(kBlr);
}

// The 2006 KPAD's read: the ring walk over 136-byte statuses, the device
// type copied as a word (`lwz`/`stw` at 0x5C) and the error stored
// (`stb` at 0x85), as in Excite Truck's.
void put_2006_read(Section& t, bool stores = true) {
    t.put(stwu_r1(0x40));
    t.put(kMflrR0);
    t.put(mr(27, 3));
    t.put(mulli(0, 27, 136));
    for (int i = 0; i < 4; ++i) t.put(addi(31, 31, -136));
    if (stores) {
        t.put(lwz(0, 29, 0x5C));
        t.put(stw(0, 3, 0x5C));
        t.put(stb(0, 29, 0x85));
    }
    t.put(kBlr);
}

void TestKpadRead() {
    {
        // A 2006 read that branches round a framed early return, makes its
        // stores, undoes its frame and tail-calls a leaf placed after it
        // (CodeRabbit on #616): it ends at that tail call, not at the early
        // return, so its stores count.
        Section t{0x80010000, {}};
        const std::uint32_t read = t.here();
        t.put(stwu_r1(0x40));
        t.put(kMflrR0);
        t.put(mr(27, 3));
        t.put(mulli(0, 27, 136));
        for (int i = 0; i < 4; ++i) t.put(addi(31, 31, -136));
        t.put(0x40820014u);  // bne +20: round the early return
        t.put(lwz(0, 1, 0x44));
        t.put(0x7C0803A6u);  // mtlr r0
        t.put(addi(1, 1, 0x40));
        t.put(kBlr);
        t.put(lwz(0, 29, 0x5C));
        t.put(stw(0, 3, 0x5C));
        t.put(stb(0, 29, 0x85));
        t.put(lwz(0, 1, 0x44));
        t.put(0x7C0803A6u);  // mtlr r0
        t.put(addi(1, 1, 0x40));
        t.put(0x48000008u);  // b +8: the leaf
        t.put(0x60000000u);
        for (int i = 0; i < 5; ++i) t.put(0x60000000u);  // the leaf, its blr far from the restore
        t.put(kBlr);
        const riftwii::KpadRead found = riftwii::find_kpad_read({t.range()});
        EXPECT_EQ(found.address, read);
        EXPECT_EQ(found.status_bytes, 136u);
    }
    {
        // The newer read, with a decoy walker (Brawl's 144-byte one) next to it.
        Section t{0x80010000, {}};
        put_ring_walker(t, 144, false, false);
        const std::uint32_t read = t.here();
        put_ring_walker(t, 240, true, true);
        const riftwii::KpadRead found = riftwii::find_kpad_read({t.range()});
        EXPECT_EQ(found.address, read);
        EXPECT_EQ(found.status_bytes, 240u);
        EXPECT_TRUE(found.takes_error);
    }
    {
        // The 2007 read takes three arguments: r6 isn't kept.
        Section t{0x80010000, {}};
        put_ring_walker(t, 132, true, false);
        const riftwii::KpadRead found = riftwii::find_kpad_read({t.range()});
        EXPECT_EQ(found.status_bytes, 132u);
        EXPECT_TRUE(!found.takes_error);
    }
    {
        // Two candidates: unsure, none.
        Section t{0x80010000, {}};
        put_ring_walker(t, 176, true, true);
        put_ring_walker(t, 176, true, true);
        EXPECT_EQ(riftwii::find_kpad_read({t.range()}).address, 0u);
    }
    {
        // A ring walker over 136-byte statuses without the 2006 read's own
        // stores either: not a read.
        Section t{0x80010000, {}};
        put_ring_walker(t, 136, false, true);
        EXPECT_EQ(riftwii::find_kpad_read({t.range()}).address, 0u);
    }
    {
        // The 2006 read (Excite Truck): 136-byte statuses, the device type
        // copied as a word at 0x5C and the error stored at 0x85.
        Section t{0x80010000, {}};
        put_ring_walker(t, 144, false, false);  // a decoy
        const std::uint32_t read = t.here();
        put_2006_read(t);
        const riftwii::KpadRead found = riftwii::find_kpad_read({t.range()});
        EXPECT_EQ(found.address, read);
        EXPECT_EQ(found.status_bytes, 136u);
        EXPECT_TRUE(!found.takes_error);
    }
    {
        // A later read beside it wins (the 2006 shape is looked for only
        // when there's none).
        Section t{0x80010000, {}};
        put_2006_read(t);
        const std::uint32_t read = t.here();
        put_ring_walker(t, 132, true, false);
        EXPECT_EQ(riftwii::find_kpad_read({t.range()}).address, read);
    }
    {
        // Two 2006 candidates: unsure, none.
        Section t{0x80010000, {}};
        put_2006_read(t);
        put_2006_read(t);
        EXPECT_EQ(riftwii::find_kpad_read({t.range()}).address, 0u);
    }
    {
        // A 136-byte ring walker without the stores, followed by a leaf
        // function that has them: not the read.
        Section t{0x80010000, {}};
        put_2006_read(t, false);
        t.put(stw(0, 3, 0x5C));
        t.put(stb(0, 29, 0x85));
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_kpad_read({t.range()}).address, 0u);
    }
    {
        // A ring walker without the stores whose frame is undone, then a
        // leaf with the stores: the leaf's return doesn't take the walker's
        // restore for its own, so the stores aren't the walker's.
        Section t{0x80010000, {}};
        t.put(stwu_r1(0x40));
        t.put(kMflrR0);
        t.put(mulli(0, 27, 136));
        for (int i = 0; i < 4; ++i) t.put(addi(31, 31, -136));
        t.put(addi(1, 1, 0x40));
        t.put(kBlr);
        t.put(stw(0, 3, 0x5C));
        t.put(stb(0, 29, 0x85));
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_kpad_read({t.range()}).address, 0u);
    }
    {
        // A ring walker without the stores that undoes its frame and ends
        // with a tail call, then a leaf with the stores: the leaf's return
        // doesn't take the walker's restore for its own.
        Section t{0x80010000, {}};
        t.put(stwu_r1(0x40));
        t.put(kMflrR0);
        t.put(mulli(0, 27, 136));
        for (int i = 0; i < 4; ++i) t.put(addi(31, 31, -136));
        t.put(addi(1, 1, 0x40));
        t.put(b(t.here(), 0x80000100));  // the tail call
        t.put(stw(0, 3, 0x5C));
        t.put(stb(0, 29, 0x85));
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_kpad_read({t.range()}).address, 0u);
    }
    {
        // A read whose frame is undone before a branch inside it to its
        // return: still all of it, with its stores.
        Section t{0x80010000, {}};
        const std::uint32_t read = t.here();
        t.put(stwu_r1(0x40));
        t.put(kMflrR0);
        t.put(mulli(0, 27, 136));
        for (int i = 0; i < 4; ++i) t.put(addi(31, 31, -136));
        t.put(stw(0, 3, 0x5C));
        t.put(stb(0, 29, 0x85));
        t.put(addi(1, 1, 0x40));
        t.put(b(t.here(), t.here() + 4));  // to the blr right after
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_kpad_read({t.range()}).address, read);
    }
    {
        // A read with an early return and its frame undone at the end:
        // all of it counts (the stores after the early return too).
        Section t{0x80010000, {}};
        const std::uint32_t read = t.here();
        t.put(stwu_r1(0x40));
        t.put(kMflrR0);
        t.put(mulli(0, 27, 136));
        for (int i = 0; i < 4; ++i) t.put(addi(31, 31, -136));
        t.put(kBlr);  // an early return
        t.put(stw(0, 3, 0x5C));
        t.put(stb(0, 29, 0x85));
        t.put(addi(1, 1, 0x40));
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_kpad_read({t.range()}).address, read);
    }
}

// WPADGetInfoAsync's marks: the channel index, the busy answer, the status
// request (report 0x15) in a 48-byte command, and its info (r4) and callback
// (r5) kept and stored into the command, unless `keeps_callback` is false
// (another request function with the rest of the marks).
void put_get_info(Section& t, bool keeps_callback, bool asks_status = true) {
    t.put(stwu_r1(0x80));
    t.put(kMflrR0);
    t.put(kSlwiR0R3By2);
    t.put(mr(27, 4));
    if (keeps_callback) t.put(mr(28, 5));
    t.put(li(30, -2));
    if (asks_status) t.put(li(4, 0x15));
    t.put(mulli(0, 0, 48));
    t.put(stw(27, 1, 48));
    if (keeps_callback) t.put(stw(28, 1, 52));
    t.put(kBlr);
}

void TestWpadGetInfo() {
    {
        Section t{0x80030000, {}};
        put_get_info(t, false);  // a decoy
        const std::uint32_t info = t.here();
        put_get_info(t, true);
        EXPECT_EQ(riftwii::find_wpad_get_info({t.range()}), info);
    }
    {
        // Two: unsure, none.
        Section t{0x80030000, {}};
        put_get_info(t, true);
        put_get_info(t, true);
        EXPECT_EQ(riftwii::find_wpad_get_info({t.range()}), 0u);
    }
    {
        // Every mark but the status report, which only a leaf function
        // after it has: not it.
        Section t{0x80030000, {}};
        put_get_info(t, true, false);
        t.put(li(3, 0x15));
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_get_info({t.range()}), 0u);
    }
    {
        // r27 and r28 saved by the prologue before the copies, never the
        // copies themselves (CodeRabbit on #616): not it.
        Section t{0x80030000, {}};
        t.put(stwu_r1(0x80));
        t.put(kMflrR0);
        t.put(stw(27, 1, 0x6C));
        t.put(stw(28, 1, 0x70));
        t.put(kSlwiR0R3By2);
        t.put(mr(27, 4));
        t.put(mr(28, 5));
        t.put(li(30, -2));
        t.put(li(4, 0x15));
        t.put(mulli(0, 0, 48));
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_get_info({t.range()}), 0u);
    }
    {
        // The info's copy written over (`li r27,0`) before the store: the
        // command doesn't get the info, so not it (CodeRabbit CLI, #616).
        Section t{0x80030000, {}};
        t.put(stwu_r1(0x80));
        t.put(kMflrR0);
        t.put(kSlwiR0R3By2);
        t.put(mr(27, 4));
        t.put(mr(28, 5));
        t.put(li(27, 0));
        t.put(li(30, -2));
        t.put(li(4, 0x15));
        t.put(mulli(0, 0, 48));
        t.put(stw(27, 1, 48));
        t.put(stw(28, 1, 52));
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_get_info({t.range()}), 0u);
    }
    {
        // An early return restoring r27/r28, branched around: the copies
        // still hold the arguments at the stores after it (CodeRabbit CLI).
        Section t{0x80030000, {}};
        const std::uint32_t info = t.here();
        t.put(stwu_r1(0x80));
        t.put(kMflrR0);
        t.put(kSlwiR0R3By2);
        t.put(mr(27, 4));
        t.put(mr(28, 5));
        t.put(0x41820014u);  // beq +20, past the early return
        t.put(lwz(27, 1, 0x6C));
        t.put(lwz(28, 1, 0x70));
        t.put(addi(1, 1, 0x80));
        t.put(kBlr);
        t.put(li(30, -2));
        t.put(li(4, 0x15));
        t.put(mulli(0, 0, 48));
        t.put(stw(27, 1, 48));
        t.put(stw(28, 1, 52));
        t.put(addi(1, 1, 0x80));
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_get_info({t.range()}), info);
    }
}

// The 2006 WPADSetConnectCallback: the channel's control block from a table
// (`lis`/`addi`, `lwzx`), the callback at +2020 swapped (`lwz`, `stw`), or a
// decoy setter swapping another slot.
void put_connect_setter(Section& t, std::int32_t slot) {
    t.put(stwu_r1(0x10));
    t.put(kMflrR0);
    t.put(mr(30, 4));
    t.put(lis(4, 0x8039));
    t.put(addi(4, 4, -2832));  // the table at 0x8038F4F0
    t.put(lwzx(4, 4, 0));
    t.put(lwz(31, 4, slot));
    t.put(stw(30, 4, slot));
    t.put(kBlr);
}

void TestWpadConnect2006() {
    {
        // The block register replaced (`lwz r4,8(r1)`) between the old
        // callback's load and the store: the store isn't through the table's
        // block, so no swap (CodeRabbit on #616).
        Section t{0x80040000, {}};
        t.put(stwu_r1(0x10));
        t.put(kMflrR0);
        t.put(mr(30, 4));
        t.put(lis(4, 0x8039));
        t.put(addi(4, 4, -2832));
        t.put(lwzx(4, 4, 0));
        t.put(lwz(31, 4, 2020));
        t.put(lwz(4, 1, 8));
        t.put(stw(30, 4, 2020));
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_2006({t.range()}).table, 0u);
    }
    {
        Section t{0x80040000, {}};
        put_connect_setter(t, 2024);  // another callback's setter
        put_connect_setter(t, 2020);
        const riftwii::WpadConnect2006 found = riftwii::find_wpad_connect_2006({t.range()});
        EXPECT_EQ(found.table, 0x8038F4F0u);
        EXPECT_EQ(found.offset, 2020u);
    }
    {
        // A 40-word setter that undoes its frame, then a two-word leaf: the
        // leaf's return doesn't stretch the setter past its limit.
        Section t{0x80040000, {}};
        const std::uint32_t setter = t.here();
        t.put(stwu_r1(0x10));
        t.put(kMflrR0);
        t.put(mr(30, 4));
        t.put(lis(4, 0x8039));
        t.put(addi(4, 4, -2832));
        t.put(lwzx(4, 4, 0));
        t.put(lwz(31, 4, 2020));
        t.put(stw(30, 4, 2020));
        for (int i = 0; i < 30; ++i) t.put(li(5, i));
        t.put(addi(1, 1, 0x10));
        t.put(kBlr);  // word 40
        t.put(li(3, 0));
        t.put(kBlr);
        const riftwii::WpadConnect2006 found = riftwii::find_wpad_connect_2006({t.range()});
        EXPECT_EQ(found.table, 0x8038F4F0u);
        (void)setter;
    }
    {
        // The callback swapped through one table, then a lookup through
        // another: the first table is the one found.
        Section t{0x80040000, {}};
        t.put(stwu_r1(0x10));
        t.put(kMflrR0);
        t.put(mr(30, 4));
        t.put(lis(4, 0x8039));
        t.put(addi(4, 4, -2832));  // table A, 0x8038F4F0
        t.put(lwzx(4, 4, 0));
        t.put(lwz(31, 4, 2020));
        t.put(stw(30, 4, 2020));
        t.put(lis(5, 0x8040));
        t.put(addi(5, 5, 0x100));  // table B, 0x80400100
        t.put(lwzx(6, 5, 0));
        t.put(kBlr);
        const riftwii::WpadConnect2006 found = riftwii::find_wpad_connect_2006({t.range()});
        EXPECT_EQ(found.table, 0x8038F4F0u);
        EXPECT_EQ(found.offset, 2020u);
    }
    {
        // A later WPAD (no such setter): none.
        Section t{0x80040000, {}};
        put_connect_setter(t, 2024);
        EXPECT_EQ(riftwii::find_wpad_connect_2006({t.range()}).table, 0u);
    }
    {
        // Leaf code (no frame of its own) right after the setter, longer
        // than a setter may be: the setter still ends at its return.
        Section t{0x80040000, {}};
        put_connect_setter(t, 2020);
        for (int i = 0; i < 48; ++i) t.put(li(3, i));
        t.put(kBlr);
        const riftwii::WpadConnect2006 found = riftwii::find_wpad_connect_2006({t.range()});
        EXPECT_EQ(found.table, 0x8038F4F0u);
        EXPECT_EQ(found.offset, 2020u);
    }
    {
        // The old callback loaded, then the slot cleared (`li r30,0`), not
        // given the new one: not the setter (CodeRabbit CLI, #616).
        Section t{0x80040000, {}};
        t.put(stwu_r1(0x10));
        t.put(kMflrR0);
        t.put(mr(30, 4));
        t.put(lis(4, 0x8039));
        t.put(addi(4, 4, -2832));
        t.put(lwzx(4, 4, 0));
        t.put(lwz(31, 4, 2020));
        t.put(li(30, 0));
        t.put(stw(30, 4, 2020));
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_2006({t.range()}).table, 0u);
    }
    {
        // The callback swapped through table A, then through table B too:
        // which one is WPAD's isn't guessed (CodeRabbit CLI, #616).
        Section t{0x80040000, {}};
        t.put(stwu_r1(0x10));
        t.put(kMflrR0);
        t.put(mr(30, 4));
        t.put(lis(4, 0x8039));
        t.put(addi(4, 4, -2832));  // table A, 0x8038F4F0
        t.put(lwzx(4, 4, 0));
        t.put(lwz(31, 4, 2020));
        t.put(stw(30, 4, 2020));
        t.put(lis(5, 0x8040));
        t.put(addi(5, 5, 0x100));  // table B, 0x80400100
        t.put(lwzx(6, 5, 0));
        t.put(lwz(31, 6, 2020));
        t.put(stw(30, 6, 2020));
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_2006({t.range()}).table, 0u);
    }
}

std::uint32_t slwi(std::uint32_t a, std::uint32_t s, std::uint32_t n) {
    return (21u << 26) | (s << 21) | (a << 16) | (n << 11) | ((31u - n) << 1);
}
std::uint32_t cmpwi(std::uint32_t a, std::int32_t v) { return d_form(11, 0, a, v); }
std::uint32_t cmplw(std::uint32_t a, std::uint32_t b) { return (31u << 26) | (a << 16) | (b << 11) | (32u << 1); }
constexpr std::uint32_t kMtctrR12 = 0x7D8903A6u;
constexpr std::uint32_t kBctrl = 0x4E800421u;
// `beq` over the next `words` words: past the call when the slot is empty
// (CodeRabbit on #616: the guard skips the call, never lands before it).
std::uint32_t beq_over(std::uint32_t words) { return 0x41820000u | (4u * (words + 1u)); }
constexpr std::uint32_t kBeqPlus8Skip = 0x41820008u;  // over one word only

// A 2008 WPAD setter (Punch-Out!!'s shape): the channel's control block from
// the table at 0x803CAF10, the slot at `slot` swapped for the second argument.
void put_later_setter(Section& t, std::int32_t slot) {
    t.put(stwu_r1(0x20));
    t.put(kMflrR0);
    t.put(lis(5, 0x803D));
    t.put(slwi(3, 3, 2));
    t.put(addi(5, 5, -20720));
    t.put(mr(29, 4));
    t.put(lwzx(31, 5, 3));
    t.put(lwz(30, 31, slot));
    t.put(stw(29, 31, slot));
    t.put(kBlr);
}

// WPAD calling the slot at `slot`, with `li r4,reason` first unless `reason`
// is 1 (a call with the channel alone, as the sampling callback's).
void put_slot_call(Section& t, std::int32_t slot, std::int32_t reason) {
    t.put(lwz(12, 29, slot));
    t.put(cmpwi(12, 0));
    t.put(beq_over(reason != 1 ? 4u : 3u));
    t.put(mr(3, 30));
    if (reason != 1) t.put(li(4, reason));
    t.put(kMtctrR12);
    t.put(kBctrl);
}

// The later WPAD's WPADGetInfoAsync through its blocking caller: the table's
// address made, then `bl X; cmpwi r3,-2` (find_wpad_get_info_later).
void TestWpadGetInfoLater() {
    const std::uint32_t table = 0x805D6170u, async = 0x80100000u;
    const auto wrapper = [&](Section& t, std::uint32_t overwrite_high, std::uint32_t second_target) {
        t.put(stwu_r1(0x10));
        t.put(kMflrR0);
        t.put(bl(t.here(), async));
        t.put(0x2C03FFFEu);  // cmpwi r3,-2
        if (second_target != 0) {
            t.put(bl(t.here(), second_target));
            t.put(0x2C03FFFEu);
        }
        t.put(lis(3, 0x805D));
        if (overwrite_high) t.put(li(3, 0));  // r3 written again: the high half is gone
        t.put(addi(3, 3, 0x6170));
        t.put(addi(1, 1, 0x10));
        t.put(kBlr);
    };
    // The calls' targets, in another section of the game's code.
    Section callees{async, {}};
    for (int i = 0; i < 0x100; ++i) callees.put(kBlr);
    {
        Section t{0x803CD2A4, {}};
        wrapper(t, 0, 0);
        EXPECT_EQ(riftwii::find_wpad_get_info_later({t.range(), callees.range()}, table), async);
        EXPECT_EQ(riftwii::find_wpad_get_info_later({t.range(), callees.range()}, 0), 0u);
        // A target in none of the game's code isn't a function to hook
        // (CodeRabbit CLI, #616).
        EXPECT_EQ(riftwii::find_wpad_get_info_later({t.range()}, table), 0u);
    }
    {
        // `lis r3; li r3,0; addi r3,r3,lo` makes lo, not the table (CodeRabbit CLI, #616).
        Section t{0x803CD2A4, {}};
        wrapper(t, 1, 0);
        EXPECT_EQ(riftwii::find_wpad_get_info_later({t.range(), callees.range()}, table), 0u);
    }
    {
        // Two different busy-checked calls in one function: not guessed (CodeRabbit CLI, #616).
        Section t{0x803CD2A4, {}};
        wrapper(t, 0, 0x80100100u);
        EXPECT_EQ(riftwii::find_wpad_get_info_later({t.range(), callees.range()}, table), 0u);
    }
    {
        // An unrelated function with two busy-checked calls and no table
        // doesn't stop the real wrapper being found (CodeRabbit CLI, #616).
        Section t{0x803CD000, {}};
        t.put(stwu_r1(0x10));
        t.put(kMflrR0);
        t.put(bl(t.here(), 0x80100200u));
        t.put(0x2C03FFFEu);
        t.put(bl(t.here(), 0x80100300u));
        t.put(0x2C03FFFEu);
        t.put(addi(1, 1, 0x10));
        t.put(kBlr);
        wrapper(t, 0, 0);
        EXPECT_EQ(riftwii::find_wpad_get_info_later({t.range(), callees.range()}, table), async);
    }
}

void TestWpadConnectLater() {
    {
        // The table built in r31, an early error return restoring r29-r31
        // branched over, then the lookup through r31 and the swap: the
        // bypassed restore doesn't lose the table (CodeRabbit CLI, #616).
        Section t{0x80218000, {}};
        put_later_setter(t, 2208);
        t.put(stwu_r1(0x20));
        t.put(kMflrR0);
        t.put(lis(31, 0x803D));
        t.put(slwi(3, 3, 2));
        t.put(addi(31, 31, -20720));
        t.put(mr(29, 4));
        t.put(0x41820010u);  // beq +16, to the lookup
        t.put(0xBBA10008u);  // lmw r29,8(r1)
        t.put(addi(1, 1, 0x20));
        t.put(kBlr);
        t.put(lwzx(31, 31, 3));
        t.put(lwz(30, 31, 2212));
        t.put(stw(29, 31, 2212));
        t.put(addi(1, 1, 0x20));
        t.put(kBlr);
        t.put(stwu_r1(0x40));
        put_slot_call(t, 2212, -1);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).offset, 2212u);
    }
    {
        // The setter's table register replaced (`mr r5,r6`) before its
        // lookup: the block isn't the table's, so no setter for 2212 and no
        // slot (CodeRabbit on #616).
        Section t{0x80218000, {}};
        put_later_setter(t, 2208);
        t.put(stwu_r1(0x20));
        t.put(kMflrR0);
        t.put(lis(5, 0x803D));
        t.put(slwi(3, 3, 2));
        t.put(addi(5, 5, -20720));
        t.put(mr(29, 4));
        t.put(mr(5, 6));
        t.put(lwzx(31, 5, 3));
        t.put(lwz(30, 31, 2212));
        t.put(stw(29, 31, 2212));
        t.put(kBlr);
        t.put(stwu_r1(0x40));
        put_slot_call(t, 2212, -1);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).offset, 0u);
    }
    {
        // The extension, connect and sampling setters side by side; WPAD
        // calls the connect slot with "no controller" as a Wii Remote leaves.
        Section t{0x80218000, {}};
        put_later_setter(t, 2208);
        put_later_setter(t, 2212);
        put_later_setter(t, 2216);
        t.put(stwu_r1(0x40));
        put_slot_call(t, 2208, 0);
        put_slot_call(t, 2216, 1);
        put_slot_call(t, 2212, -1);
        t.put(kBlr);
        const riftwii::WpadConnect2006 found = riftwii::find_wpad_connect_later({t.range()});
        EXPECT_EQ(found.table, 0x803CAF10u);
        EXPECT_EQ(found.offset, 2212u);
    }
    {
        // The block register reloaded from the stack between the old
        // callback's load and the store (`lwz r0,2212(r31); lwz r31,8(r1);
        // stw r29,2212(r31)`): the store isn't through the table's block, so
        // no setter, no slot (CodeRabbit CLI, #15).
        Section t{0x80218000, {}};
        t.put(stwu_r1(0x20));
        t.put(kMflrR0);
        t.put(lis(5, 0x803D));
        t.put(slwi(3, 3, 2));
        t.put(addi(5, 5, -20720));
        t.put(mr(29, 4));
        t.put(lwzx(31, 5, 3));
        t.put(lwz(30, 31, 2212));
        t.put(lwz(31, 1, 8));
        t.put(stw(29, 31, 2212));
        t.put(addi(1, 1, 0x20));
        t.put(kBlr);
        t.put(stwu_r1(0x40));
        put_slot_call(t, 2212, -1);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).offset, 0u);
    }
    {
        // A call whose CTR comes from another register (`mtctr r11`), not
        // the loaded slot: not the slot's call, so no slot (CodeRabbit CLI, #15).
        Section t{0x80218000, {}};
        put_later_setter(t, 2208);
        put_later_setter(t, 2212);
        t.put(stwu_r1(0x40));
        t.put(lwz(12, 29, 2212));
        t.put(cmpwi(12, 0));
        t.put(beq_over(4));
        t.put(mr(3, 30));
        t.put(li(4, -1));
        t.put(0x7D6903A6u);  // mtctr r11
        t.put(kBctrl);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).offset, 0u);
    }
    {
        // The loaded slot copied before CTR takes it (`mr r11,r12; mtctr
        // r11`): still the slot's call (CodeRabbit CLI, #15).
        Section t{0x80218000, {}};
        put_later_setter(t, 2208);
        put_later_setter(t, 2212);
        t.put(stwu_r1(0x40));
        t.put(lwz(12, 29, 2212));
        t.put(cmpwi(12, 0));
        t.put(beq_over(5));
        t.put(mr(11, 12));
        t.put(mr(3, 30));
        t.put(li(4, -1));
        t.put(0x7D6903A6u);  // mtctr r11
        t.put(kBctrl);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).offset, 2212u);
    }
    {
        // Another field read between the old callback's load and the store
        // (`lwz r0,2232(r31)`): still the setter for 2212 (CodeRabbit CLI,
        // #616).
        Section t{0x80218000, {}};
        put_later_setter(t, 2208);
        t.put(stwu_r1(0x20));
        t.put(kMflrR0);
        t.put(lis(5, 0x803D));
        t.put(slwi(3, 3, 2));
        t.put(addi(5, 5, -20720));
        t.put(mr(29, 4));
        t.put(lwzx(31, 5, 3));
        t.put(lwz(30, 31, 2212));
        t.put(lwz(0, 31, 2232));
        t.put(stw(29, 31, 2212));
        t.put(kBlr);
        t.put(stwu_r1(0x40));
        put_slot_call(t, 2212, -1);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).offset, 2212u);
    }
    {
        // A branch around `mtctr r12` that still reaches the call: CTR
        // there may be anything, so not the slot's call (CodeRabbit CLI,
        // #616).
        Section t{0x80218000, {}};
        put_later_setter(t, 2208);
        put_later_setter(t, 2212);
        t.put(stwu_r1(0x40));
        t.put(lwz(12, 29, 2212));
        t.put(mr(3, 30));
        t.put(li(4, -1));
        t.put(cmpwi(5, 0));
        t.put(kBeqPlus8Skip);
        t.put(kMtctrR12);
        t.put(kBctrl);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).offset, 0u);
    }
    {
        // CTR already the slot's before the branch, so the `mtctr` it skips
        // changes nothing: both paths call the slot (CodeRabbit CLI).
        Section t{0x80218000, {}};
        put_later_setter(t, 2208);
        put_later_setter(t, 2212);
        t.put(stwu_r1(0x40));
        t.put(lwz(12, 29, 2212));
        t.put(mr(3, 30));
        t.put(li(4, -1));
        t.put(kMtctrR12);
        t.put(cmpwi(5, 0));
        t.put(kBeqPlus8Skip);
        t.put(kMtctrR12);
        t.put(kBctrl);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).offset, 2212u);
    }
    {
        // A branch around the copy (`mr r11,r12`) that CTR takes: on that
        // path CTR is r11's old value, so not the slot's call (CodeRabbit
        // CLI).
        Section t{0x80218000, {}};
        put_later_setter(t, 2208);
        put_later_setter(t, 2212);
        t.put(stwu_r1(0x40));
        t.put(li(11, 0));
        t.put(lwz(12, 29, 2212));
        t.put(li(4, -1));
        t.put(kBeqPlus8Skip);
        t.put(mr(11, 12));
        t.put(0x7D6903A6u);  // mtctr r11
        t.put(kBctrl);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).offset, 0u);
    }
    {
        // `lwzu r5,4(r11)` moves r11 off the slot's copy before `mtctr r11`:
        // not the slot's call (CodeRabbit CLI, rf10).
        Section t{0x80218000, {}};
        put_later_setter(t, 2208);
        put_later_setter(t, 2212);
        t.put(stwu_r1(0x40));
        t.put(lwz(12, 29, 2212));
        t.put(mr(11, 12));
        t.put(0x84AB0004u);  // lwzu r5,4(r11)
        t.put(li(4, -1));
        t.put(0x7D6903A6u);  // mtctr r11
        t.put(kBctrl);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).offset, 0u);
    }
    {
        // Two branches, each around one `li r4,-1`: with both taken, r4 is
        // 0 at the call, so not a "no controller" call (CodeRabbit CLI).
        Section t{0x80218000, {}};
        put_later_setter(t, 2208);
        put_later_setter(t, 2212);
        t.put(stwu_r1(0x40));
        t.put(lwz(12, 29, 2212));
        t.put(li(4, 0));
        t.put(kMtctrR12);
        t.put(kBeqPlus8Skip);
        t.put(li(4, -1));
        t.put(kBeqPlus8Skip);
        t.put(li(4, -1));
        t.put(kBctrl);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).offset, 0u);
    }
    {
        // A branch that skips `li r4,-1` and still reaches the call (r4 0
        // there): not a "no controller" call (CodeRabbit on #616).
        Section t{0x80218000, {}};
        put_later_setter(t, 2208);
        put_later_setter(t, 2212);
        t.put(stwu_r1(0x40));
        t.put(lwz(12, 29, 2212));
        t.put(li(4, 0));
        t.put(cmpwi(4, 0));
        t.put(kBeqPlus8Skip);
        t.put(li(4, -1));
        t.put(kMtctrR12);
        t.put(kBctrl);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).offset, 0u);
    }
    {
        // An early error return that leaves by a tail call (`lmw r29,8(r1);
        // addi r1,r1,0x20; b` out) before the swap the branch around it
        // reaches: still a setter (CodeRabbit CLI, #15).
        Section t{0x80218000, {}};
        t.put(stwu_r1(0x20));
        t.put(kMflrR0);
        t.put(lis(5, 0x803D));
        t.put(slwi(3, 3, 2));
        t.put(addi(5, 5, -20720));
        t.put(mr(29, 4));
        t.put(lwzx(31, 5, 3));
        t.put(0x41820010u);  // beq +16, to the swap
        t.put(0xBBA10008u);  // lmw r29,8(r1)
        t.put(addi(1, 1, 0x20));
        t.put(0x4BFF0000u);  // b, far back: out of the function
        t.put(lwz(30, 31, 2212));
        t.put(stw(29, 31, 2212));
        t.put(addi(1, 1, 0x20));
        t.put(kBlr);
        t.put(stwu_r1(0x40));
        put_slot_call(t, 2212, -1);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).offset, 2212u);
    }
    {
        // An early error return restores r29 from the stack before the
        // store the branch around it reaches: still a setter, its slot found.
        Section t{0x80218000, {}};
        t.put(stwu_r1(0x20));
        t.put(kMflrR0);
        t.put(lis(5, 0x803D));
        t.put(slwi(3, 3, 2));
        t.put(addi(5, 5, -20720));
        t.put(mr(29, 4));
        t.put(lwzx(31, 5, 3));
        t.put(0x41820010u);  // beq +16, to the store
        t.put(0xBBA10008u);  // lmw r29,8(r1): the error return's epilogue
        t.put(addi(1, 1, 0x20));
        t.put(kBlr);
        t.put(lwz(30, 31, 2212));
        t.put(stw(29, 31, 2212));
        t.put(addi(1, 1, 0x20));
        t.put(kBlr);
        t.put(stwu_r1(0x40));
        put_slot_call(t, 2212, -1);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).offset, 2212u);
    }
    {
        // The same, with the return address reloaded after the frame's
        // undoing (`addi r1,r1,0x20; lwz r0,4(r1); mtlr r0; blr`): still an
        // epilogue, the setter's slot found.
        Section t{0x80218000, {}};
        t.put(stwu_r1(0x20));
        t.put(kMflrR0);
        t.put(lis(5, 0x803D));
        t.put(slwi(3, 3, 2));
        t.put(addi(5, 5, -20720));
        t.put(mr(29, 4));
        t.put(lwzx(31, 5, 3));
        t.put(0x41820018u);  // beq +24, to the store
        t.put(0xBBA10008u);  // lmw r29,8(r1)
        t.put(addi(1, 1, 0x20));
        t.put(lwz(0, 1, 4));
        t.put(0x7C0803A6u);  // mtlr r0
        t.put(kBlr);
        t.put(lwz(30, 31, 2212));
        t.put(stw(29, 31, 2212));
        t.put(addi(1, 1, 0x20));
        t.put(kBlr);
        t.put(stwu_r1(0x40));
        put_slot_call(t, 2212, -1);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).offset, 2212u);
    }
    {
        // A load off r1 and the frame undone, then a `b` further into the
        // same function (`lwz r29,8(r1); addi r1,r1,0x20; b +4`): not an
        // epilogue, so the load overwrites the argument and there's no setter.
        Section t{0x80218000, {}};
        t.put(stwu_r1(0x20));
        t.put(kMflrR0);
        t.put(lis(5, 0x803D));
        t.put(slwi(3, 3, 2));
        t.put(addi(5, 5, -20720));
        t.put(mr(29, 4));
        t.put(lwz(29, 1, 8));
        t.put(addi(1, 1, 0x20));
        t.put(0x48000004u);  // b +4
        t.put(lwzx(31, 5, 3));
        t.put(lwz(30, 31, 2212));
        t.put(stw(29, 31, 2212));
        t.put(kBlr);
        t.put(stwu_r1(0x40));
        put_slot_call(t, 2212, -1);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).table, 0u);
    }
    {
        // The copy of r4 overwritten through the stack (`li r0,0; stw r0,8(r1);
        // lwz r29,8(r1)`), no epilogue: it stores 0, so not a setter.
        Section t{0x80218000, {}};
        t.put(stwu_r1(0x20));
        t.put(kMflrR0);
        t.put(lis(5, 0x803D));
        t.put(slwi(3, 3, 2));
        t.put(addi(5, 5, -20720));
        t.put(mr(29, 4));
        t.put(li(0, 0));
        t.put(stw(0, 1, 8));
        t.put(lwz(29, 1, 8));
        t.put(lwzx(31, 5, 3));
        t.put(lwz(30, 31, 2212));
        t.put(stw(29, 31, 2212));
        t.put(kBlr);
        t.put(stwu_r1(0x40));
        put_slot_call(t, 2212, -1);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).table, 0u);
    }
    {
        // The setter's copy of r4 overwritten before its store (`mr r29,r4;
        // li r29,0`): it stores 0, not the callback, so it isn't a setter
        // and the slot isn't found.
        Section t{0x80218000, {}};
        t.put(stwu_r1(0x20));
        t.put(kMflrR0);
        t.put(lis(5, 0x803D));
        t.put(slwi(3, 3, 2));
        t.put(addi(5, 5, -20720));
        t.put(mr(29, 4));
        t.put(li(29, 0));
        t.put(lwzx(31, 5, 3));
        t.put(lwz(30, 31, 2212));
        t.put(stw(29, 31, 2212));
        t.put(kBlr);
        t.put(stwu_r1(0x40));
        put_slot_call(t, 2212, -1);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).table, 0u);
    }
    {
        // No slot called with "no controller": none.
        Section t{0x80218000, {}};
        put_later_setter(t, 2212);
        put_later_setter(t, 2216);
        t.put(stwu_r1(0x40));
        put_slot_call(t, 2212, 0);
        put_slot_call(t, 2216, 1);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).table, 0u);
    }
    {
        // Setters for the called slot in two tables: which table WPAD's
        // call goes through can't be told, so none.
        Section t{0x80218000, {}};
        put_later_setter(t, 2212);
        t.put(stwu_r1(0x20));
        t.put(kMflrR0);
        t.put(lis(5, 0x8040));
        t.put(slwi(3, 3, 2));
        t.put(addi(5, 5, 0x100));  // another table, 0x80400100
        t.put(mr(29, 4));
        t.put(lwzx(31, 5, 3));
        t.put(lwz(30, 31, 2212));
        t.put(stw(29, 31, 2212));
        t.put(kBlr);
        t.put(stwu_r1(0x40));
        put_slot_call(t, 2212, -1);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).table, 0u);
    }
    {
        // Two slots called with it: unsure, none.
        Section t{0x80218000, {}};
        put_later_setter(t, 2208);
        put_later_setter(t, 2212);
        t.put(stwu_r1(0x40));
        put_slot_call(t, 2208, -1);
        put_slot_call(t, 2212, -1);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).table, 0u);
    }
    {
        // `li r4,-1` then `li r4,1` before the call: the callback is told 1,
        // not "no controller", so none.
        Section t{0x80218000, {}};
        put_later_setter(t, 2212);
        t.put(stwu_r1(0x40));
        t.put(lwz(12, 29, 2212));
        t.put(li(4, -1));
        t.put(li(4, 1));
        t.put(kMtctrR12);
        t.put(kBctrl);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).table, 0u);
    }
    {
        // r4 read, not written, after `li r4,-1` (`mr r5,r4`): still -1 at
        // the call, so found; a rotate into r4 (`rlwinm r4,r5,0,0,31`) is a
        // write, so not.
        Section t{0x80218000, {}};
        put_later_setter(t, 2212);
        t.put(stwu_r1(0x40));
        t.put(lwz(12, 29, 2212));
        t.put(li(4, -1));
        t.put(mr(5, 4));
        t.put(kMtctrR12);
        t.put(kBctrl);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).offset, 2212u);
        Section u{0x80218000, {}};
        put_later_setter(u, 2212);
        u.put(stwu_r1(0x40));
        u.put(lwz(12, 29, 2212));
        u.put(li(4, -1));
        u.put(0x54A4003Eu);  // rlwinm r4,r5,0,0,31
        u.put(kMtctrR12);
        u.put(kBctrl);
        u.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({u.range()}).table, 0u);
        // `lmw r3,8(r1)` loads r3 through r31, r4 among them: a write.
        Section v{0x80218000, {}};
        put_later_setter(v, 2212);
        v.put(stwu_r1(0x40));
        v.put(lwz(12, 29, 2212));
        v.put(li(4, -1));
        v.put(0xB8610008u);  // lmw r3,8(r1)
        v.put(kMtctrR12);
        v.put(kBctrl);
        v.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({v.range()}).table, 0u);
    }
    {
        // The -1 belongs to a later call, past a jump: not this slot's.
        Section t{0x80218000, {}};
        put_later_setter(t, 2212);
        t.put(stwu_r1(0x40));
        t.put(lwz(12, 29, 2212));
        t.put(0x48000010u);  // b
        t.put(li(4, -1));
        t.put(kMtctrR12);
        t.put(kBctrl);
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_connect_later({t.range()}).table, 0u);
    }
}

// WPAD's extension callback slot: the setter just before the connect slot.
void TestWpadExtension() {
    {
        // The 2008 layout: extension +2208 beside connect +2212.
        Section t{0x80218000, {}};
        put_later_setter(t, 2208);
        put_later_setter(t, 2212);
        put_later_setter(t, 2216);
        riftwii::WpadConnect2006 connect;
        connect.table = 0x803CAF10u;
        connect.offset = 2212;
        const riftwii::WpadConnect2006 found = riftwii::find_wpad_extension({t.range()}, connect);
        EXPECT_EQ(found.table, 0x803CAF10u);
        EXPECT_EQ(found.offset, 2208u);
    }
    {
        // The 2006 WPAD's (Excite Truck): +2016 beside +2020.
        Section t{0x80040000, {}};
        put_connect_setter(t, 2016);
        put_connect_setter(t, 2020);
        riftwii::WpadConnect2006 connect;
        connect.table = 0x8038F4F0u;
        connect.offset = 2020;
        const riftwii::WpadConnect2006 found = riftwii::find_wpad_extension({t.range()}, connect);
        EXPECT_EQ(found.table, 0x8038F4F0u);
        EXPECT_EQ(found.offset, 2016u);
    }
    {
        // One setter that swaps the extension slot on two paths (each
        // returning on its own): still one setter, so found.
        Section t{0x80218000, {}};
        t.put(stwu_r1(0x20));
        t.put(kMflrR0);
        t.put(lis(5, 0x803D));
        t.put(slwi(3, 3, 2));
        t.put(addi(5, 5, -20720));
        t.put(mr(29, 4));
        t.put(lwzx(31, 5, 3));
        t.put(lwz(30, 31, 2208));
        t.put(stw(29, 31, 2208));
        t.put(addi(1, 1, 0x20));
        t.put(kBlr);
        t.put(lwz(30, 31, 2208));
        t.put(stw(29, 31, 2208));
        t.put(addi(1, 1, 0x20));
        t.put(kBlr);
        put_later_setter(t, 2212);
        riftwii::WpadConnect2006 connect;
        connect.table = 0x803CAF10u;
        connect.offset = 2212;
        EXPECT_EQ(riftwii::find_wpad_extension({t.range()}, connect).offset, 2208u);
    }
    {
        // A load from one table's block, then a store into another's: no
        // setter for either (the store has no load of its own).
        Section t{0x80218000, {}};
        t.put(stwu_r1(0x20));
        t.put(kMflrR0);
        t.put(lis(5, 0x803D));
        t.put(addi(5, 5, -20720));
        t.put(mr(29, 4));
        t.put(lwzx(31, 5, 3));
        t.put(lwz(30, 31, 2208));
        t.put(lis(6, 0x8040));
        t.put(addi(6, 6, 0x100));
        t.put(lwzx(28, 6, 3));
        t.put(stw(29, 28, 2208));
        t.put(kBlr);
        put_later_setter(t, 2212);
        riftwii::WpadConnect2006 connect;
        connect.table = 0x80400100u;
        connect.offset = 2212;
        EXPECT_EQ(riftwii::find_wpad_extension({t.range()}, connect).table, 0u);
    }
    {
        // No setter for the word before the connect slot: none.
        Section t{0x80218000, {}};
        put_later_setter(t, 2212);
        put_later_setter(t, 2216);
        riftwii::WpadConnect2006 connect;
        connect.table = 0x803CAF10u;
        connect.offset = 2212;
        EXPECT_EQ(riftwii::find_wpad_extension({t.range()}, connect).table, 0u);
    }
    {
        // A setter at that offset through another table: none.
        Section t{0x80218000, {}};
        put_later_setter(t, 2208);
        riftwii::WpadConnect2006 connect;
        connect.table = 0x80400000u;
        connect.offset = 2212;
        EXPECT_EQ(riftwii::find_wpad_extension({t.range()}, connect).table, 0u);
    }
    {
        // No connect slot: none.
        Section t{0x80218000, {}};
        put_later_setter(t, 2208);
        EXPECT_EQ(riftwii::find_wpad_extension({t.range()}, riftwii::WpadConnect2006{}).table, 0u);
    }
}

// WPADRead (the 2008 WPAD's shape): the channel's block, the error compared
// with -2 and -7, 42 bytes copied, the error stored into the game's status
// (through r29, its copy of r4) unless `stores_error` is false (another
// reader). `channel_copy`: the channel indexed from a copy of r3, as The
// Complete Saga's 2007 WPAD does.
void put_wpad_read(Section& t, bool stores_error, bool channel_copy) {
    t.put(stwu_r1(0x20));
    t.put(kMflrR0);
    t.put(mr(29, 4));
    if (channel_copy) {
        t.put(mr(28, 3));
        t.put(slwi(0, 28, 2));
    } else {
        t.put(kSlwiR0R3By2);
    }
    t.put(lis(5, 0x803D));
    t.put(addi(5, 5, -20720));
    t.put(lwzx(31, 5, 0));
    t.put(lbz(0, 31, 41));
    t.put(cmpwi(0, -2));
    t.put(cmpwi(0, -7));
    t.put(mr(3, 29));
    t.put(li(5, 42));
    if (stores_error) t.put(stb(0, 29, 41));
    t.put(kBlr);
}

void TestWpadRead() {
    {
        Section t{0x80218000, {}};
        put_wpad_read(t, false, false);  // a decoy
        const std::uint32_t read = t.here();
        put_wpad_read(t, true, false);
        EXPECT_EQ(riftwii::find_wpad_read({t.range()}), read);
    }
    {
        Section t{0x80333000, {}};
        put_wpad_read(t, true, true);
        EXPECT_EQ(riftwii::find_wpad_read({t.range()}), 0x80333000u);
    }
    {
        // Two: unsure, none.
        Section t{0x80218000, {}};
        put_wpad_read(t, true, false);
        put_wpad_read(t, true, false);
        EXPECT_EQ(riftwii::find_wpad_read({t.range()}), 0u);
    }
}

// WPADSetDataFormat: the block's probe result, busy flag and format read;
// -1 kept or returned (`returns_none`), busy, the format compared with the
// one given (r27, its copy of r4) and stored at +2232 unless `stores` is
// false (another request with the same checks).
void put_set_format(Section& t, bool returns_none, bool stores) {
    t.put(stwu_r1(0x20));
    t.put(kMflrR0);
    t.put(kSlwiR0R3By2);
    t.put(mr(27, 4));
    t.put(lwzx(31, 5, 0));
    t.put(lwz(30, 31, 2236));
    t.put(lwz(28, 31, 2232));
    t.put(cmpwi(30, -1));
    if (returns_none) t.put(li(3, -1));
    t.put(li(30, -2));
    t.put(cmplw(28, 27));
    if (stores) t.put(stw(27, 31, 2232));
    t.put(kBlr);
}

void TestWpadSetFormat() {
    {
        Section t{0x80218000, {}};
        put_set_format(t, true, false);  // a decoy
        const std::uint32_t set = t.here();
        put_set_format(t, true, true);
        EXPECT_EQ(riftwii::find_wpad_set_format({t.range()}), set);
    }
    {
        // The probe's -1 compared and kept (The Complete Saga's): found too.
        Section t{0x80333000, {}};
        put_set_format(t, false, true);
        EXPECT_EQ(riftwii::find_wpad_set_format({t.range()}), 0x80333000u);
    }
    {
        // Two: unsure, none.
        Section t{0x80218000, {}};
        put_set_format(t, true, true);
        put_set_format(t, true, true);
        EXPECT_EQ(riftwii::find_wpad_set_format({t.range()}), 0u);
    }
    {
        // The format copied to r11, an error block with a call that returns
        // (branched over), then r11 compared and stored: the call never runs
        // on that path, so r11 is still the format (CodeRabbit CLI, #616).
        Section t{0x80218000, {}};
        t.put(stwu_r1(0x20));
        t.put(kMflrR0);
        t.put(kSlwiR0R3By2);
        t.put(mr(11, 4));
        t.put(lwzx(31, 5, 0));
        t.put(lwz(30, 31, 2236));
        t.put(lwz(28, 31, 2232));
        t.put(cmpwi(30, -1));
        t.put(0x4082001Cu);  // bne +28, over the error block
        t.put(li(3, -1));
        t.put(bl(t.here(), 0x80200000u));
        t.put(lwz(0, 1, 0x24));
        t.put(0x7C0803A6u);  // mtlr r0
        t.put(addi(1, 1, 0x20));
        t.put(kBlr);
        t.put(li(30, -2));
        t.put(cmplw(28, 11));
        t.put(stw(11, 31, 2232));
        t.put(addi(1, 1, 0x20));
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_set_format({t.range()}), 0x80218000u);
    }
    {
        // The format copied to r11 (volatile), a call, then r11 compared and
        // stored: the call may have changed it, so not the format
        // (CodeRabbit on #616).
        Section t{0x80218000, {}};
        t.put(stwu_r1(0x20));
        t.put(kMflrR0);
        t.put(kSlwiR0R3By2);
        t.put(mr(11, 4));
        t.put(lwzx(31, 5, 0));
        t.put(lwz(30, 31, 2236));
        t.put(lwz(28, 31, 2232));
        t.put(cmpwi(30, -1));
        t.put(li(3, -1));
        t.put(li(30, -2));
        t.put(bl(t.here(), 0x80200000u));
        t.put(cmplw(28, 11));
        t.put(stw(11, 31, 2232));
        t.put(kBlr);
        EXPECT_EQ(riftwii::find_wpad_set_format({t.range()}), 0u);
    }
}

void put_probe(Section& t, bool returns_busy) {
    t.put(stwu_r1(0x20));
    t.put(kMflrR0);
    t.put(kSlwiR0R3By2);
    t.put(lwzx(31, 5, 0));
    t.put(lbz(0, 31, 2309));
    t.put(cmplwi(0, 253));
    t.put(lbz(0, 31, 2309));
    t.put(stw(0, 30, 0));
    t.put(li(3, -1));
    if (returns_busy) t.put(li(3, -2));
    t.put(kBlr);
}

void TestWpadProbe() {
    {
        Section t{0x80020000, {}};
        put_probe(t, false);  // no "busy" answer: not it
        const std::uint32_t probe = t.here();
        put_probe(t, true);
        EXPECT_EQ(riftwii::find_wpad_probe({t.range()}), probe);
    }
    {
        Section t{0x80020000, {}};
        put_probe(t, true);
        put_probe(t, true);
        EXPECT_EQ(riftwii::find_wpad_probe({t.range()}), 0u);
    }
}

void TestSmallDataBase() {
    Section t{0x80004000, {}};
    t.put(lis(1, 0x8060));
    t.put(lis(13, 0x805A));
    t.put(ori(13, 13, 0x4420));
    EXPECT_EQ(riftwii::find_small_data_base({t.range()}, 0x80004000), 0x805A4420u);
    Section n{0x80004000, {}};
    n.put(lis(13, 0x805A));
    n.put(addi(13, 13, -0x7BE0));
    EXPECT_EQ(riftwii::find_small_data_base({n.range()}, 0x80004000), 0x80598420u);  // addi takes a signed low half
}

// The banner in a data section with its pointer word; HBMInit loading the
// pointer (through r13, or `lis` and `lwz`) and calling out; HBMCalc after.
void TestHbmFunctions(bool through_r13, bool calc_follows) {
    Section data{0x80300000, {}};
    const std::uint32_t banner = data.here();
    data.put_text("<< RVL_SDK - HBM \trelease build: May 15 2007 >>");
    while (data.bytes.size() % 4) data.bytes.push_back(0);
    const std::uint32_t pointer = data.here();
    data.put(banner);
    const std::uint32_t small_data = pointer + 0x100;

    Section text{0x80100000, {}};
    text.put(stwu_r1(0x10));
    text.put(kBlr);  // something before
    const std::uint32_t init = text.here();
    text.put(stwu_r1(0x10));
    text.put(kMflrR0);
    if (through_r13) {
        text.put(lwz(3, 13, static_cast<std::int32_t>(pointer - small_data)));
    } else {
        text.put(lis(4, (pointer + 0x8000u) >> 16));
        text.put(lwz(3, 4, static_cast<std::int32_t>(static_cast<std::int16_t>(pointer & 0xFFFFu))));
    }
    text.put(bl(text.here(), 0x80100000));
    text.put(kBlr);
    text.put(0);  // padding
    const std::uint32_t calc = text.here();
    std::uint32_t select = 0;
    if (calc_follows) {
        text.put(stwu_r1(0x10));
        text.put(bl(text.here(), 0x80100000));
        text.put(kBlr);
        for (int wrapper = 0; wrapper < 2; ++wrapper) {  // HBMDraw, then HBMGetSelectBtnNum
            if (wrapper == 1) select = text.here();
            text.put(stwu_r1(0x10));
            text.put(kMflrR0);
            text.put(bl(text.here(), 0x80100000));
            text.put(kBlr);
        }
    } else {
        text.put(li(3, 0));
        text.put(kBlr);
    }
    const std::vector<riftwii::CodeRange> code = {text.range()};
    const std::vector<riftwii::CodeRange> all = {text.range(), data.range()};
    const riftwii::HbmFunctions found = riftwii::find_hbm_functions(code, all, small_data);
    if (calc_follows) {
        EXPECT_EQ(found.init, init);
        EXPECT_EQ(found.calc, calc);
        EXPECT_EQ(found.select, select);
    } else {
        EXPECT_EQ(found.calc, 0u);
    }
}

// The C++ HOME Menu (Dr. Mario Online Rx): HBMCalc returns the button the
// menu ended on, from the leaf it calls last; after HBMDraw comes a wrapper
// that takes an argument (HBMSetAdjustFlag), which isn't the select.
void TestCalcAnswers(bool calc_returns) {
    Section data{0x80300000, {}};
    const std::uint32_t banner = data.here();
    data.put_text("<< RVL_SDK - HBM \trelease build: Aug 23 2008 >>");
    while (data.bytes.size() % 4) data.bytes.push_back(0);
    const std::uint32_t pointer = data.here();
    data.put(banner);
    const std::uint32_t small_data = pointer + 0x100;

    Section text{0x80100000, {}};
    text.put(stwu_r1(0x10));  // what the other calls reach
    text.put(kBlr);
    const std::uint32_t leaf = text.here();  // the inner GetSelectBtnNum
    text.put(lwz(0, 3, 20));
    text.put(cmplwi(0, 18));
    text.put(li(3, -1));
    text.put(kBlr);
    text.put(lwz(3, 3, 192));
    text.put(kBlr);
    const std::uint32_t init = text.here();
    text.put(stwu_r1(0x10));
    text.put(kMflrR0);
    text.put(lwz(3, 13, static_cast<std::int32_t>(pointer - small_data)));
    text.put(bl(text.here(), 0x80100000));
    text.put(kBlr);
    const std::uint32_t calc = text.here();
    text.put(stwu_r1(0x10));
    text.put(kMflrR0);
    text.put(mr(31, 3));
    text.put(bl(text.here(), 0x80100000));
    text.put(mr(4, 31));
    text.put(bl(text.here(), 0x80100000));
    if (calc_returns) text.put(bl(text.here(), leaf));
    text.put(lwz(0, 1, 20));
    text.put(lwz(31, 1, 12));
    text.put(0x7C0803A6u);  // mtlr r0
    text.put(addi(1, 1, 16));
    text.put(kBlr);
    text.put(stwu_r1(0x10));  // HBMDraw
    text.put(kMflrR0);
    text.put(bl(text.here(), 0x80100000));
    text.put(kBlr);
    text.put(stwu_r1(0x10));  // HBMSetAdjustFlag(BOOL)
    text.put(kMflrR0);
    text.put(mr(31, 3));
    text.put(bl(text.here(), 0x80100000));
    text.put(kBlr);
    const riftwii::HbmFunctions found = riftwii::find_hbm_functions({text.range()}, {text.range(), data.range()}, small_data);
    EXPECT_EQ(found.init, init);
    EXPECT_EQ(found.calc, calc);
    EXPECT_EQ(found.select, calc_returns ? leaf : 0u);
}

void TestNoBanner() {
    // A 2006 program: its HOME Menu registers no banner.
    Section data{0x80300000, {}};
    data.put_text("<< RVL_SDK - KPAD \trelease build: Oct  5 2006 >>");
    Section text{0x80100000, {}};
    text.put(stwu_r1(0x10));
    text.put(kBlr);
    const riftwii::HbmFunctions found = riftwii::find_hbm_functions({text.range()}, {text.range(), data.range()}, 0);
    EXPECT_EQ(found.init, 0u);
    EXPECT_EQ(found.calc, 0u);
}

// KPADGetUnifiedWpadStatus as Mario Kart Wii (RMCE01) has it at
// 0x8019808c, word for word: 56-byte statuses, 1,336-byte KPAD blocks.
const std::uint32_t kMkwUnified[] = {
    0x9421FFD0u, 0x7C0802A6u, 0x90010034u, 0x39610030u, 0x4BE8899Du, 0x1C030538u,
    0x3C608034u, 0x28050010u, 0x38631460u, 0x7C992378u, 0x7CBA2B78u, 0x7FA30214u,
    0x40810008u, 0x3B400010u, 0x4800E449u, 0x8B7D010Eu, 0x7C7C1B78u, 0x3BE0FFFCu,
    0x4800004Cu, 0x2C1B0000u, 0x3BC0000Fu, 0x41820008u, 0x3BDBFFFFu, 0x7FDBF378u,
    0x480274C1u, 0x2C030003u, 0x41820010u, 0x1C1E0038u, 0x7C7D0214u, 0x9BE30139u,
    0x1C1E0038u, 0x7F23CB78u, 0x38A00038u, 0x7C9D0214u, 0x38840110u, 0x4BE6DE1Du,
    0x3B390038u, 0x2C1A0000u, 0x3B5AFFFFu, 0x4082FFB0u, 0x7F83E378u, 0x4800E405u,
    0x39610030u, 0x4BE8894Du, 0x80010034u, 0x7C0803A6u, 0x38210030u, 0x4E800020u,
};

// Donkey Kong Country Returns' (SF8E01) KPADGetUnifiedWpadStatus at
// 0x804aa030, word for word: the later KPAD's, whose ring can be longer
// than 16 (`addi r0,r3,16; cmplw r5,r0`), 66-byte statuses.
const std::uint32_t kDkcrUnified[] = {
    0x9421FFD0u, 0x7C0802A6u, 0x90010034u, 0x39610030u, 0x4BFCC8CDu, 0x1C030688u,
    0x3C60805Du, 0x7C992378u, 0x3863BCE8u, 0x7FC30214u, 0x7CBA2B78u, 0x807E05A4u,
    0x38030010u, 0x7C050040u, 0x40810008u, 0x7C1A0378u, 0x4800CEC1u, 0x8B7E017Au,
    0x7C7C1B78u, 0x3BE0FFFCu, 0x48000068u, 0x2C1B0000u, 0x40820010u, 0x807E05A4u,
    0x3B63000Fu, 0x48000008u, 0x3B7BFFFFu, 0x281B0010u, 0x41800018u, 0x381BFFF0u,
    0x807E05A0u, 0x1C000042u, 0x7FA30214u, 0x48000010u, 0x1C1B0042u, 0x7C7E0214u,
    0x3BA30180u, 0x4801FA0Du, 0x2C030003u, 0x41820008u, 0x9BFD0029u, 0x7F23CB78u,
    0x7FA4EB78u, 0x38A00042u, 0x4BB59F21u, 0x3B390042u, 0x2C1A0000u, 0x3B5AFFFFu,
    0x4082FF94u, 0x7F83E378u, 0x4800CE79u, 0x39610030u, 0x4BFCC859u, 0x80010034u,
    0x7C0803A6u, 0x38210030u, 0x4E800020u,
};

// A function that also limits a count to 16 (`cmplwi r5,16`) but indexes
// no KPAD block and copies nothing: not taken.
void put_count_limiter(Section& t) {
    t.put(stwu_r1(0x20));
    t.put(kMflrR0);
    t.put(cmplwi(5, 16));
    t.put(li(5, 16));
    t.put(mulli(0, 4, 56));
    t.put(kBlr);
}

void TestKpadUnified() {
    {
        Section t{0x8019808c, {}};
        for (std::uint32_t w : kMkwUnified) t.put(w);
        put_count_limiter(t);
        const riftwii::KpadUnified u = riftwii::find_kpad_unified({t.range()});
        EXPECT_EQ(u.address, 0x8019808cu);
        EXPECT_EQ(u.status_bytes, 56u);
    }
    {
        Section t{0x804aa030, {}};
        for (std::uint32_t w : kDkcrUnified) t.put(w);
        put_count_limiter(t);
        const riftwii::KpadUnified u = riftwii::find_kpad_unified({t.range()});
        EXPECT_EQ(u.address, 0x804aa030u);
        EXPECT_EQ(u.status_bytes, 66u);
    }
    {
        // Not there (most games): nothing.
        Section t{0x80100000, {}};
        put_count_limiter(t);
        const riftwii::KpadUnified u = riftwii::find_kpad_unified({t.range()});
        EXPECT_EQ(u.address, 0u);
        EXPECT_EQ(u.status_bytes, 0u);
    }
    {
        // Two that fit: neither is taken.
        Section t{0x80100000, {}};
        for (std::uint32_t w : kMkwUnified) t.put(w);
        for (std::uint32_t w : kMkwUnified) t.put(w);
        EXPECT_EQ(riftwii::find_kpad_unified({t.range()}).address, 0u);
    }
    {
        // `cmplwi r5,16` with an `addi r0,r3,16` that isn't the later
        // KPAD's limit (no `cmplw r5,r0` after it) and no `li 16`: not
        // taken; with the `cmplw`, it is (CodeRabbit CLI, #616).
        for (bool pair : {false, true}) {
            Section t{0x80100000, {}};
            t.put(stwu_r1(0x20));
            t.put(kMflrR0);
            t.put(mulli(0, 3, 1336));
            t.put(cmplwi(5, 16));
            t.put(addi(0, 3, 16));
            if (pair) t.put(cmplw(5, 0));
            t.put(li(5, 56));
            t.put(mulli(4, 6, 56));
            t.put(kBlr);
            EXPECT_EQ(riftwii::find_kpad_unified({t.range()}).address, pair ? 0x80100000u : 0u);
        }
    }
}

// KPADSetConnectCallback as GoldenEye 007 (SJBE52) has it at 0x80508480,
// word for word: KPAD's block at 0x806b2928, 1,672 bytes a channel, the
// callback at +1596.
const std::uint32_t kGoldenEyeConnect[] = {
    0x9421FFF0u, 0x7C0802A6u, 0x90010014u, 0x93E1000Cu, 0x7C7F1B78u, 0x93C10008u,
    0x7C9E2378u, 0x4BFAF895u, 0x1C1F0688u, 0x3C80806Bu, 0x38842928u, 0x7C840214u,
    0x83E4063Cu, 0x93C4063Cu, 0x4BFAF8B9u, 0x7FE3FB78u, 0x83E1000Cu, 0x83C10008u,
    0x80010014u, 0x7C0803A6u, 0x38210010u, 0x4E800020u,
};

// A KPAD read's start that indexes its block by `stride` (`mulli r9,r3,K`).
std::uint32_t put_kpad_read_start(Section& t, std::int32_t stride) {
    const std::uint32_t at = t.here();
    t.put(stwu_r1(0x40));
    t.put(kMflrR0);
    t.put(mulli(9, 3, stride));
    t.put(kBlr);
    return at;
}

void TestKpadConnect() {
    {
        Section t{0x80507000, {}};
        const std::uint32_t read = put_kpad_read_start(t, 1672);
        while (t.here() < 0x80508480) t.put(0);
        for (std::uint32_t w : kGoldenEyeConnect) t.put(w);
        const riftwii::KpadConnect c = riftwii::find_kpad_connect({t.range()}, read);
        EXPECT_EQ(c.address, 0x80508480u);
        EXPECT_EQ(c.block, 0x806B2928u);
        EXPECT_EQ(c.stride, 1672u);
        EXPECT_EQ(c.offset, 1596u);
    }
    {
        // Another block size (an older KPAD, or another library's setter):
        // not taken.
        Section t{0x80507000, {}};
        const std::uint32_t read = put_kpad_read_start(t, 1336);
        for (std::uint32_t w : kGoldenEyeConnect) t.put(w);
        EXPECT_EQ(riftwii::find_kpad_connect({t.range()}, read).address, 0u);
    }
    {
        // Two that fit: neither is taken.
        Section t{0x80507000, {}};
        const std::uint32_t read = put_kpad_read_start(t, 1672);
        for (std::uint32_t w : kGoldenEyeConnect) t.put(w);
        for (std::uint32_t w : kGoldenEyeConnect) t.put(w);
        EXPECT_EQ(riftwii::find_kpad_connect({t.range()}, read).address, 0u);
    }
    // No KPAD read: nothing.
    Section t{0x80507000, {}};
    for (std::uint32_t w : kGoldenEyeConnect) t.put(w);
    EXPECT_EQ(riftwii::find_kpad_connect({t.range()}, 0).address, 0u);
}

}  // namespace

int main() {
    TestWpadGetInfoLater();
    TestKpadRead();
    TestWpadProbe();
    TestWpadGetInfo();
    TestWpadConnect2006();
    TestWpadConnectLater();
    TestWpadExtension();
    TestWpadRead();
    TestWpadSetFormat();
    TestSmallDataBase();
    TestHbmFunctions(true, true);
    TestHbmFunctions(false, true);
    TestHbmFunctions(true, false);
    TestCalcAnswers(true);
    TestCalcAnswers(false);
    TestNoBanner();
    TestKpadUnified();
    TestKpadConnect();
    if (g_failures == 0) std::cout << "hbmsearch: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
