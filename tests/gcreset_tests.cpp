// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// A GameCube game's restart: OSResetSystem and the reset routine as the
// loader finds them (riftwii/gcreset.hpp). The code here is made up
// instruction by instruction in the shape the SDK's functions have.
#include "riftwii/gcreset.hpp"

#include <cstdint>
#include <iostream>
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

int main() {
    test_restart_plan();
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
