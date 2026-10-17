// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// A GameCube game's picture settings: what the loader finds in its code
// (riftwii/gcvideo.hpp). The code here is made up instruction by
// instruction in the shape the SDK's functions have.
#include "riftwii/gcvideo.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

constexpr std::uint32_t kBase = 0x80200000;
constexpr std::uint32_t kBlr = 0x4E800020;
constexpr std::uint32_t kNop = 0x60000000;

std::uint32_t lis(unsigned rd, std::uint16_t value) { return (15u << 26) | (rd << 21) | value; }
std::uint32_t addi(unsigned rd, unsigned ra, std::uint16_t value) { return (14u << 26) | (rd << 21) | (ra << 16) | value; }
std::uint32_t beq(std::uint32_t from, std::uint32_t to) { return (16u << 26) | (12u << 21) | (2u << 16) | ((to - from) & 0xFFFC); }
std::uint32_t bne(std::uint32_t from, std::uint32_t to) { return (16u << 26) | (4u << 21) | (2u << 16) | ((to - from) & 0xFFFC); }

// Code at kBase, as big-endian bytes.
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

// GXSetCopyFilter's shape: the test of vf, the sample pattern's writes,
// `beq` to the sharp weights, the weights from vfilter, then the sharp
// ones. `sharp_first` puts the sharp weights' halves in the other order.
// Returns where the `beq` is.
std::uint32_t add_copy_filter(Text& t, bool sharp_first = false) {
    t.add(kBlr);  // the function before
    t.add(0x54A0063F);  // clrlwi. r0,r5,24 (vf)
    for (int i = 0; i < 8; ++i) t.add(kNop);  // the sample pattern's writes
    const std::uint32_t branch = t.here();
    t.add(0);  // the beq, once the target is known
    for (int i = 0; i < 6; ++i) t.add(kNop);  // vfilter's weights
    t.add(0x48000014);  // b past the sharp weights
    const std::uint32_t sharp = t.here();
    t.add(lis(4, 0x5359));
    t.add(lis(3, 0x5400));
    if (sharp_first) {
        t.add(addi(7, 3, 0x0015));
        t.add(addi(6, 4, 0x5000));
    } else {
        t.add(addi(6, 4, 0x5000));
        t.add(addi(7, 3, 0x0015));
    }
    t.add(kBlr);
    t.words[(branch - kBase) / 4] = beq(branch, sharp);
    return branch;
}

void test_copy_filter() {
    {
        Text t;
        const std::uint32_t branch = add_copy_filter(t);
        const riftwii::GcCopyFilterSwitch found = riftwii::find_gc_copy_filter_switch(t.ranges());
        EXPECT_EQ(found.branch, branch);
        EXPECT_EQ(found.sharp, branch + 4 * 8);
    }
    {
        Text t;
        const std::uint32_t branch = add_copy_filter(t, true);
        EXPECT_EQ(riftwii::find_gc_copy_filter_switch(t.ranges()).branch, branch);
    }
    {
        // Two of them: which is the game's is not known, so neither.
        Text t;
        add_copy_filter(t);
        add_copy_filter(t);
        EXPECT_EQ(riftwii::find_gc_copy_filter_switch(t.ranges()).branch, 0u);
    }
    {
        // The weights reached by a bne, or by nothing: not this function.
        Text t;
        const std::uint32_t branch = add_copy_filter(t);
        t.words[(branch - kBase) / 4] = bne(branch, branch + 4 * 8);
        EXPECT_EQ(riftwii::find_gc_copy_filter_switch(t.ranges()).branch, 0u);
    }
    {
        // The sharp words alone (only one half made): not it.
        Text t;
        const std::uint32_t branch = add_copy_filter(t);
        t.words[(branch - kBase) / 4 + 10] = kNop;
        EXPECT_EQ(riftwii::find_gc_copy_filter_switch(t.ranges()).branch, 0u);
    }
    {
        Text t;
        t.add(kBlr);
        EXPECT_EQ(riftwii::find_gc_copy_filter_switch(t.ranges()).branch, 0u);
    }
}

}  // namespace

int main() {
    test_copy_filter();
    if (g_failures == 0) std::cout << "gcvideo tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
