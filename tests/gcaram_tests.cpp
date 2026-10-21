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

void test_raised_end_word() {
    // lis r3,0x1100: addis r3,0,0x1100.
    EXPECT_EQ(riftwii::kGcAramRaisedEndWord, (15u << 26) | (3u << 21) | 0x1100u);
}

}  // namespace

int main() {
    test_doshin_words();
    test_finds_clamps();
    test_leaves_other_code();
    test_raised_end_word();
    if (g_failures == 0) std::cout << "gcaram tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
