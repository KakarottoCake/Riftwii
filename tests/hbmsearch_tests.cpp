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

void TestKpadRead() {
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
        // A 2006 read: a ring walker without the stores at 0x5C/0x5D.
        Section t{0x80010000, {}};
        put_ring_walker(t, 136, false, true);
        EXPECT_EQ(riftwii::find_kpad_read({t.range()}).address, 0u);
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
    TestKpadRead();
    TestWpadProbe();
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
