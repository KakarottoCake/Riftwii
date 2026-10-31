// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// A sound engine's ARAM pool clamped to ARGetSize() (riftwii/gcaram.hpp).
// The code here is made up instruction by instruction in the shape MusyX's
// ARAM setup has in Doshin the Giant PAL (GKDP01).
#include "riftwii/gcaram.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

constexpr std::uint32_t kBase = 0x8008e300;
constexpr std::uint32_t kBlr = 0x4E800020;
constexpr std::uint32_t kNop = 0x60000000;

std::uint32_t bl(std::uint32_t from, std::uint32_t to) { return 0x48000001u | ((to - from) & 0x03FFFFFC); }
std::uint32_t lwz_r13(unsigned reg, std::int16_t offset) {
    return (32u << 26) | (reg << 21) | (13u << 16) | static_cast<std::uint16_t>(offset);
}
std::uint32_t stw_r13(unsigned reg, std::int16_t offset) {
    return (36u << 26) | (reg << 21) | (13u << 16) | static_cast<std::uint16_t>(offset);
}
std::uint32_t cmplw(unsigned a, unsigned b) { return 0x7C000040u | (a << 16) | (b << 11); }

struct Text {
    std::vector<std::uint32_t> words;
    std::vector<std::uint8_t> bytes;
    std::uint32_t here() const { return kBase + 4 * static_cast<std::uint32_t>(words.size()); }
    void add(std::uint32_t w) { words.push_back(w); }
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

constexpr std::int16_t kSizeVar = -29620;  // __AR_Size, as Doshin keeps it
constexpr std::int16_t kTopVar = -30120;   // the pool's top

// ARGetSize; returns its address.
std::uint32_t add_getter(Text& t) {
    const std::uint32_t at = t.here();
    t.add(lwz_r13(3, kSizeVar));
    t.add(kBlr);
    return at;
}
// The clamp of the pool's top `top` (loaded into `top_reg`) to what
// `getter` answers; returns its first call's address.
std::uint32_t add_clamp(Text& t, std::uint32_t getter, unsigned top_reg, std::int16_t top) {
    const std::uint32_t at = t.here();
    t.add(bl(t.here(), getter));
    t.add(lwz_r13(top_reg, top));
    t.add(cmplw(top_reg, 3));
    t.add(0x4081000C);  // ble +12
    t.add(bl(t.here(), getter));
    t.add(stw_r13(3, top));
    return at;
}

// Doshin's own words at 0x8008e384-0x8008e398, and its ARGetSize at
// 0x80098a60, read from the game: the finder must take exactly these.
void test_doshin_words() {
    std::vector<std::uint32_t> words((0x80098a68 - kBase) / 4, kNop);
    std::vector<std::uint8_t> bytes;
    auto put = [&](std::uint32_t at, std::uint32_t w) { words[(at - kBase) / 4] = w; };
    put(0x8008e384, 0x4800A6DD);
    put(0x8008e388, 0x800D8A58);
    put(0x8008e38c, 0x7C001840);
    put(0x8008e390, 0x4081000C);
    put(0x8008e394, 0x4800A6CD);
    put(0x8008e398, 0x906D8A58);
    put(0x80098a60, 0x806D8C4C);
    put(0x80098a64, kBlr);
    for (std::uint32_t w : words) {
        bytes.push_back(static_cast<std::uint8_t>(w >> 24));
        bytes.push_back(static_cast<std::uint8_t>(w >> 16));
        bytes.push_back(static_cast<std::uint8_t>(w >> 8));
        bytes.push_back(static_cast<std::uint8_t>(w));
    }
    const auto found = riftwii::find_gc_aram_size_clamps({riftwii::CodeRange{kBase, bytes.data(), bytes.size()}});
    EXPECT_EQ(found.size(), 1u);
    if (found.size() == 1) {
        EXPECT_EQ(found[0].first_call, 0x8008e384u);
        EXPECT_EQ(found[0].second_call, 0x8008e394u);
    }
}

void test_finds_clamps() {
    Text t;
    const std::uint32_t getter = add_getter(t);
    t.add(kNop);
    const std::uint32_t first = add_clamp(t, getter, 0, kTopVar);
    t.add(kNop);
    const std::uint32_t second = add_clamp(t, getter, 5, -100);  // another register and variable
    const auto found = riftwii::find_gc_aram_size_clamps(t.ranges());
    EXPECT_EQ(found.size(), 2u);
    if (found.size() == 2) {
        EXPECT_EQ(found[0].first_call, first);
        EXPECT_EQ(found[0].second_call, first + 16);
        EXPECT_EQ(found[1].first_call, second);
    }
}

// Shapes that only look alike are left as they are.
void test_leaves_other_code() {
    {
        // The stored variable is not the one compared.
        Text t;
        const std::uint32_t getter = add_getter(t);
        add_clamp(t, getter, 0, kTopVar);
        t.words.back() = stw_r13(3, kTopVar + 4);
        EXPECT_TRUE(riftwii::find_gc_aram_size_clamps(t.ranges()).empty());
    }
    {
        // The second call goes to another function.
        Text t;
        const std::uint32_t getter = add_getter(t);
        const std::uint32_t other = add_getter(t);
        const std::uint32_t at = add_clamp(t, getter, 0, kTopVar);
        t.words[(at + 16 - kBase) / 4] = bl(at + 16, other);
        EXPECT_TRUE(riftwii::find_gc_aram_size_clamps(t.ranges()).empty());
    }
    {
        // The called function is not a plain r13 getter.
        Text t;
        const std::uint32_t getter = t.here();
        t.add(lwz_r13(4, kSizeVar));  // r4, not r3
        t.add(kBlr);
        add_clamp(t, getter, 0, kTopVar);
        EXPECT_TRUE(riftwii::find_gc_aram_size_clamps(t.ranges()).empty());
    }
    {
        // The comparison the other way round: not this clamp.
        Text t;
        const std::uint32_t getter = add_getter(t);
        const std::uint32_t at = add_clamp(t, getter, 0, kTopVar);
        t.words[(at + 8 - kBase) / 4] = cmplw(3, 0);
        EXPECT_TRUE(riftwii::find_gc_aram_size_clamps(t.ranges()).empty());
    }
    {
        // Cut short by the end of the code.
        Text t;
        const std::uint32_t getter = add_getter(t);
        add_clamp(t, getter, 0, kTopVar);
        t.words.pop_back();
        EXPECT_TRUE(riftwii::find_gc_aram_size_clamps(t.ranges()).empty());
    }
}

// 007: NightFire's (Nightfire.elf 0x80033d64-0x80033d70, its ARGetSize at
// 0x8016cf64, read from the game): the top of EA's ARAM allocator.
void test_finds_ea_top() {
    Text t;
    const std::uint32_t getter = t.here();
    t.add(0x806D9D34);  // lwz r3,-0x62cc(r13)
    t.add(kBlr);
    t.add(kNop);
    const std::uint32_t call = t.here();
    t.add(bl(t.here(), getter));
    t.add(0x3803C000);  // addi r0,r3,-0x4000
    t.add(0x7C030378);  // mr r3,r0
    t.add(0x900D9590);  // stw r0,-0x6a70(r13)
    std::vector<std::uint32_t> found = riftwii::find_gc_aram_tops_below_size(t.ranges());
    EXPECT_EQ(found.size(), 1u);
    if (found.size() == 1) EXPECT_EQ(found[0], call);

    // The store right after the addi is taken too.
    t.words[(call + 8 - kBase) / 4] = 0x900D9590;
    EXPECT_EQ(riftwii::find_gc_aram_tops_below_size(t.ranges()).size(), 1u);

    // Left alone: another register stored, an addition, a step not a
    // multiple of 32, more than 64 KB below, no store, another getter shape.
    Text other_store = t;
    other_store.words[(call + 8 - kBase) / 4] = 0x7C030378;
    other_store.words[(call + 12 - kBase) / 4] = 0x906D9590;  // stw r3
    EXPECT_TRUE(riftwii::find_gc_aram_tops_below_size(other_store.ranges()).empty());
    Text added = t;
    added.words[(call + 4 - kBase) / 4] = 0x38034000;  // addi r0,r3,0x4000
    EXPECT_TRUE(riftwii::find_gc_aram_tops_below_size(added.ranges()).empty());
    Text odd = t;
    odd.words[(call + 4 - kBase) / 4] = 0x3803FFFC;  // addi r0,r3,-4
    EXPECT_TRUE(riftwii::find_gc_aram_tops_below_size(odd.ranges()).empty());
    Text from_r4 = t;
    from_r4.words[(call + 4 - kBase) / 4] = 0x3804C000;  // addi r0,r4,-0x4000
    EXPECT_TRUE(riftwii::find_gc_aram_tops_below_size(from_r4.ranges()).empty());
    Text unstored = t;
    unstored.words[(call + 8 - kBase) / 4] = kNop;
    unstored.words[(call + 12 - kBase) / 4] = kNop;
    EXPECT_TRUE(riftwii::find_gc_aram_tops_below_size(unstored.ranges()).empty());
    Text not_getter = t;
    not_getter.words[(getter - kBase) / 4] = lwz_r13(4, -0x62cc);
    EXPECT_TRUE(riftwii::find_gc_aram_tops_below_size(not_getter.ranges()).empty());
}

// 007: NightFire's driving.elf, its sound code at 0x80211320-0x80211334
// and its ARGetBaseAddress at 0x801b41ac, read from the game.
void test_finds_end_after_base() {
    Text t;
    t.add(kBlr);
    const std::uint32_t getter = t.here();
    t.add(0x38604000);  // li r3,0x4000
    t.add(kBlr);
    const std::uint32_t call = t.here();
    t.add(bl(t.here(), getter));
    t.add(0x38C30100);  // addi r6,r3,0x100
    const std::uint32_t lis = t.here();
    t.add(0x3C000080);  // lis r0,0x80
    t.add(0x39004000);  // li r8,0x4000
    t.add(0x38E0C000);  // li r7,-0x4000
    t.add(0x7C060050);  // subf r0,r6,r0
    std::vector<std::uint32_t> found = riftwii::find_gc_aram_ends_after_base(t.ranges());
    EXPECT_EQ(found.size(), 1u);
    if (found.size() == 1) EXPECT_EQ(found[0], lis);

    // Left alone: the end as the subtrahend, an end past 16 MB, no
    // subtraction, and a call to another function.
    Text swapped = t;
    swapped.words[(lis + 12 - kBase) / 4] = 0x7C003050;  // subf r0,r0,r6: r6 - r0
    EXPECT_TRUE(riftwii::find_gc_aram_ends_after_base(swapped.ranges()).empty());
    Text far_end = t;
    far_end.words[(lis - kBase) / 4] = 0x3C000180;  // lis r0,0x180
    EXPECT_TRUE(riftwii::find_gc_aram_ends_after_base(far_end.ranges()).empty());
    Text no_subf = t;
    no_subf.words[(lis + 12 - kBase) / 4] = kNop;
    EXPECT_TRUE(riftwii::find_gc_aram_ends_after_base(no_subf.ranges()).empty());
    // The subtrahend not from the base: subf r0,r5,r0 with r5 never set
    // from r3, or r6 written again after its addi.
    Text not_base = t;
    not_base.words[(lis + 12 - kBase) / 4] = 0x7C050050;  // subf r0,r5,r0
    EXPECT_TRUE(riftwii::find_gc_aram_ends_after_base(not_base.ranges()).empty());
    Text rewritten = t;
    rewritten.words[(lis + 4 - kBase) / 4] = 0x38C00010;  // li r6,0x10
    EXPECT_TRUE(riftwii::find_gc_aram_ends_after_base(rewritten.ranges()).empty());
    // The end's register written again before the subtraction.
    Text end_changed = t;
    end_changed.words[(lis + 8 - kBase) / 4] = 0x38000010;  // li r0,0x10
    EXPECT_TRUE(riftwii::find_gc_aram_ends_after_base(end_changed.ranges()).empty());
    // Anything else between (a compare, a branch, a store) ends the search.
    Text compared = t;
    compared.words[(lis + 4 - kBase) / 4] = 0x2C030000;  // cmpwi r3,0
    EXPECT_TRUE(riftwii::find_gc_aram_ends_after_base(compared.ranges()).empty());
    Text branched = t;
    branched.words[(lis + 4 - kBase) / 4] = 0x41820008;  // beq +8
    EXPECT_TRUE(riftwii::find_gc_aram_ends_after_base(branched.ranges()).empty());
    Text stored = t;
    stored.words[(lis + 4 - kBase) / 4] = 0x90010008;  // stw r0,8(r1)
    EXPECT_TRUE(riftwii::find_gc_aram_ends_after_base(stored.ranges()).empty());
    // The base taken by mr instead: still found.
    Text moved = t;
    moved.words[(call + 4 - kBase) / 4] = 0x7C661B78;  // mr r6,r3
    EXPECT_EQ(riftwii::find_gc_aram_ends_after_base(moved.ranges()).size(), 1u);
    Text other_getter = t;
    other_getter.words[(getter - kBase) / 4] = 0x38608000;  // li r3,-0x8000
    EXPECT_TRUE(riftwii::find_gc_aram_ends_after_base(other_getter.ranges()).empty());
    (void)call;
}

void test_raised_end_word() {
    // lis r3,0x1100: addis r3,0,0x1100.
    EXPECT_EQ(riftwii::kGcAramRaisedEndWord, (15u << 26) | (3u << 21) | 0x1100u);
}

}  // namespace

int main() {
    test_doshin_words();
    test_finds_clamps();
    test_leaves_other_code();
    test_finds_ea_top();
    test_finds_end_after_base();
    test_raised_end_word();
    if (g_failures == 0) std::cout << "gcaram tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
