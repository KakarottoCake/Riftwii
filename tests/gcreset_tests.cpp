// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// A GameCube game's restart: OSResetSystem and the reset routine as the
// loader finds them (riftwii/gcreset.hpp). The code here is made up
// instruction by instruction in the shape the SDK's functions have.
#include "riftwii/gcreset.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

constexpr std::uint32_t kBase = 0x80340000;
constexpr std::uint32_t kBlr = 0x4E800020;
constexpr std::uint32_t kNop = 0x60000000;
constexpr std::uint32_t kMflrR0 = 0x7C0802A6;

std::uint32_t bl(std::uint32_t from, std::uint32_t to) { return 0x48000001u | ((to - from) & 0x03FFFFFC); }
std::uint32_t b(std::uint32_t from, std::uint32_t to) { return 0x48000000u | ((to - from) & 0x03FFFFFC); }
std::uint32_t mr(unsigned to, unsigned from) { return (31u << 26) | (from << 21) | (to << 16) | (from << 11) | (444u << 1); }
std::uint32_t cmpwi(unsigned reg, std::uint16_t value) { return (11u << 26) | (reg << 16) | value; }

struct Text {
    std::vector<std::uint32_t> words;
    std::vector<std::uint8_t> bytes;
    std::uint32_t here() const { return kBase + 4 * static_cast<std::uint32_t>(words.size()); }
    void add(std::uint32_t w) { words.push_back(w); }
    void put(std::uint32_t at, std::uint32_t w) { words[(at - kBase) / 4] = w; }
    std::vector<riftwii::CodeRange> ranges() {
        bytes.clear();
        for (std::uint32_t w : words) {
            bytes.push_back(static_cast<std::uint8_t>(w >> 24));
            bytes.push_back(static_cast<std::uint8_t>(w >> 16));
            bytes.push_back(static_cast<std::uint8_t>(w >> 8));
            bytes.push_back(static_cast<std::uint8_t>(w));
        }
        return {riftwii::CodeRange{kBase, bytes.data(), bytes.size()}};
    }
};

// The reset routine: branches about, the PI writes, then a loop of its
// own (no blr). Returns its first word.
std::uint32_t add_routine(Text& t) {
    t.add(kBlr);  // the function before
    const std::uint32_t start = t.here();
    t.add(b(t.here(), t.here() + 8));
    t.add(kNop);
    t.add(0x3D00CC00);  // lis r8,0xCC00
    t.add(0x61083000);  // ori r8,r8,0x3000
    t.add(0x38800003);  // li r4,3
    t.add(0x90880024);  // stw r4,0x24(r8)
    t.add(0x90680024);  // stw r3,0x24(r8)
    const std::uint32_t loop = t.here();
    t.add(kNop);
    t.add(b(t.here(), loop));
    return start;
}

// __OSDoHotReset: right after the routine, called by `mflr r0` only.
std::uint32_t add_hot_reset(Text& t, std::uint32_t routine) {
    const std::uint32_t start = t.here();
    t.add(kMflrR0);
    t.add(kNop);
    t.add(bl(t.here(), routine));
    t.add(kBlr);
    return start;
}

// OSResetSystem: keeps r3 in r26, compares it with 2 and 1, calls `target`.
std::uint32_t add_reset_system(Text& t, std::uint32_t target, bool compare_one = true) {
    t.add(kBlr);
    const std::uint32_t start = t.here();
    t.add(kMflrR0);
    t.add(kNop);
    t.add(mr(26, 3));
    t.add(cmpwi(26, 2));
    t.add(kNop);
    if (compare_one) t.add(cmpwi(26, 1));
    t.add(bl(t.here(), target));
    t.add(kBlr);
    return start;
}

void test_inline_hot_reset() {
    // Super Mario Sunshine's SDK: OSResetSystem calls the routine itself.
    Text t;
    const std::uint32_t routine = add_routine(t);
    add_hot_reset(t, routine);  // unused there
    const std::uint32_t reset_system = add_reset_system(t, routine);
    const riftwii::GcResetFunctions found = riftwii::find_gc_reset_functions(t.ranges());
    EXPECT_EQ(found.hardware_reset, routine);
    EXPECT_EQ(found.reset_system, reset_system);
}

void test_through_hot_reset() {
    // An SDK whose OSResetSystem goes through __OSDoHotReset.
    Text t;
    const std::uint32_t routine = add_routine(t);
    const std::uint32_t hot = add_hot_reset(t, routine);
    const std::uint32_t reset_system = add_reset_system(t, hot);
    const riftwii::GcResetFunctions found = riftwii::find_gc_reset_functions(t.ranges());
    EXPECT_EQ(found.hardware_reset, routine);
    EXPECT_EQ(found.reset_system, reset_system);
}

void test_not_found() {
    // No routine: nothing.
    {
        Text t;
        t.add(kBlr);
        add_reset_system(t, kBase);
        const riftwii::GcResetFunctions found = riftwii::find_gc_reset_functions(t.ranges());
        EXPECT_EQ(found.hardware_reset, 0u);
        EXPECT_EQ(found.reset_system, 0u);
    }
    // A caller that does not compare its argument with 1: neither, as one
    // without the other cannot be hooked.
    {
        Text t;
        const std::uint32_t routine = add_routine(t);
        add_reset_system(t, routine, false);
        const riftwii::GcResetFunctions found = riftwii::find_gc_reset_functions(t.ranges());
        EXPECT_EQ(found.hardware_reset, 0u);
        EXPECT_EQ(found.reset_system, 0u);
    }
    // Two routines: neither.
    {
        Text t;
        const std::uint32_t first = add_routine(t);
        add_routine(t);
        add_reset_system(t, first);
        const riftwii::GcResetFunctions found = riftwii::find_gc_reset_functions(t.ranges());
        EXPECT_EQ(found.hardware_reset, 0u);
    }
}

void test_restart_plan() {
    const std::uint32_t window = 0x91500000;
    // Zeroed BSS first, then the copies, as given; the table's 4 entries
    // (48 bytes) take the window's first 64 bytes.
    std::vector<riftwii::GcRestartPiece> pieces = {
        {0x80300000, 0x1000, true}, {0x80003100, 0x100, false}, {0x80300100, 0x21, false}, {0x80000000, 0x3100, false}};
    riftwii::GcRestartPlan plan = riftwii::plan_gc_restart(pieces, window, 0x10000, 4);
    EXPECT_EQ(plan.regions.size(), 4u);
    EXPECT_EQ(plan.table, window);
    if (plan.regions.size() == 4) {
        EXPECT_EQ(plan.regions[0].source, 0u);
        EXPECT_EQ(plan.regions[0].length, 0x1000u);
        EXPECT_EQ(plan.regions[1].source, window + 0x40);
        EXPECT_EQ(plan.regions[2].source, window + 0x140);
        EXPECT_EQ(plan.regions[3].source, window + 0x180);  // 0x21 bytes rounded up to 0x40
        EXPECT_EQ(plan.end, window + 0x180 + 0x3100);
    }
    // Too many pieces, a piece outside MEM1 or an empty one, or no room.
    EXPECT_TRUE(riftwii::plan_gc_restart(pieces, window, 0x10000, 3).regions.empty());
    EXPECT_TRUE(riftwii::plan_gc_restart({{0x817FFF00, 0x200, false}}, window, 0x10000, 4).regions.empty());
    EXPECT_TRUE(riftwii::plan_gc_restart({{0x80003100, 0, false}}, window, 0x10000, 4).regions.empty());
    EXPECT_TRUE(riftwii::plan_gc_restart(pieces, window, 0x3000, 4).regions.empty());
    // A zeroed piece takes no room.
    EXPECT_EQ(riftwii::plan_gc_restart({{0x80400000, 0x01000000, true}}, window, 0x100, 4).regions.size(), 1u);
}

}  // namespace

// __OSBootDolSimple: the one function that loads "2004/02/01"'s address.
void test_boot_dol() {
    // The data: other bytes, then the date at 0x803E8F58 (lower half
    // 0x8F58 is negative for addi, so lis takes 0x803F).
    std::vector<std::uint8_t> data(0x100, 0x41);
    const std::uint32_t data_base = 0x803E8E80;
    const std::uint32_t date = 0x803E8F58;
    std::memcpy(&data[date - data_base], "2004/02/01", 10);
    const std::vector<riftwii::CodeRange> data_ranges{riftwii::CodeRange{data_base, data.data(), data.size()}};

    Text t;
    t.add(kBlr);  // the function before
    const std::uint32_t boot_dol = t.here();
    t.add(kMflrR0);
    t.add(0x9421FFC0);  // stwu r1,-0x40(r1)
    t.add(0x7D3E4B78);  // mr r30,r9: argv, its seventh argument, kept
    for (int i = 0; i < 5; ++i) t.add(kNop);
    t.add(0x3C80803F);  // lis r4,0x803F
    t.add(kNop);
    t.add(0x38848F58);  // addi r4,r4,-0x70a8
    t.add(0x38A0000A);  // li r5,10
    t.add(kBlr);
    // Another function that loads the upper half only, for something else.
    t.add(kMflrR0);
    t.add(0x3C60803F);  // lis r3,0x803F
    t.add(0x38631234);  // addi r3,r3,0x1234
    t.add(kBlr);
    EXPECT_EQ(riftwii::find_gc_boot_dol(t.ranges(), data_ranges), boot_dol);

    // No date in the data (an SDK before 2004): nothing.
    std::vector<std::uint8_t> other(0x100, 0x41);
    const std::vector<riftwii::CodeRange> no_date{riftwii::CodeRange{data_base, other.data(), other.size()}};
    EXPECT_EQ(riftwii::find_gc_boot_dol(t.ranges(), no_date), 0u);

    // The SDK's date helper kept as a function of its own, loading it too
    // but keeping no argv (Sonic Gems Collection's sonicr.dol): passed over.
    t.add(kMflrR0);
    t.add(0x3C80803F);  // lis r4,0x803F
    t.add(0x38848F58);  // addi r4,r4,-0x70a8
    t.add(0x38A0000A);  // li r5,10
    t.add(kBlr);
    EXPECT_EQ(riftwii::find_gc_boot_dol(t.ranges(), data_ranges), boot_dol);

    // Two functions that load it and keep argv: not trusted.
    t.add(kMflrR0);
    t.add(0x7D3F4B78);  // mr r31,r9
    t.add(0x3CC0803F);  // lis r6,0x803F
    t.add(0x38C68F58);  // addi r6,r6,-0x70a8
    t.add(kBlr);
    EXPECT_EQ(riftwii::find_gc_boot_dol(t.ranges(), data_ranges), 0u);
}

// A switched-to program's arguments in BI2, where __start reads them.
std::uint32_t be32(const std::vector<std::uint8_t>& b, std::size_t at) {
    return std::uint32_t(b[at]) << 24 | std::uint32_t(b[at + 1]) << 16 | std::uint32_t(b[at + 2]) << 8 | b[at + 3];
}
void test_exec_args() {
    std::vector<std::uint8_t> bi2(0x2000, 0);
    bi2[0x24] = 7;  // a field of the disc's own, kept
    EXPECT_TRUE(riftwii::gc_pack_exec_args(bi2, {"cd", "mode=2"}));
    const std::uint32_t argc_at = be32(bi2, 8);
    EXPECT_TRUE(argc_at >= 0x1000u && argc_at < 0x2000u && argc_at % 4 == 0);
    EXPECT_EQ(be32(bi2, argc_at), 2u);
    const std::uint32_t first = be32(bi2, argc_at + 4), second = be32(bi2, argc_at + 8);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(&bi2[first])), std::string("cd"));
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(&bi2[second])), std::string("mode=2"));
    EXPECT_EQ(be32(bi2, argc_at + 12), 0u);  // the list ends in 0
    EXPECT_EQ(int(bi2[0x24]), 7);
    // None: the offset 0, so __start passes no arguments.
    EXPECT_TRUE(riftwii::gc_pack_exec_args(bi2, {}));
    EXPECT_EQ(be32(bi2, 8), 0u);
    // More than BI2's upper half holds.
    EXPECT_TRUE(!riftwii::gc_pack_exec_args(bi2, {std::string(0x1000, 'x')}));
}

int main() {
    test_restart_plan();
    test_boot_dol();
    test_exec_args();
    test_inline_hot_reset();
    test_through_hot_reset();
    test_not_found();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "gcreset tests passed" << std::endl;
    return 0;
}
