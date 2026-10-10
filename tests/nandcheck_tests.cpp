// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The NAND permission patch (wii/boot.cpp) on a copy of memory: the first
// match only, and the 32-bit store changing just the branch.
#include "riftwii/nandcheck.hpp"

#include <cstring>
#include <iostream>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
void put32(std::uint8_t* p, std::uint32_t v) {
    p[0] = std::uint8_t(v >> 24); p[1] = std::uint8_t(v >> 16); p[2] = std::uint8_t(v >> 8); p[3] = std::uint8_t(v);
}

// Patches `mem` as the console does (base 0x13400000) and returns the
// match's offset, or -1.
long patch(std::vector<std::uint8_t>& mem) {
    std::size_t at = 0;
    if (!find_nand_check(mem.data(), mem.size(), at)) return -1;
    const std::uint32_t base = 0x13400000;
    const NandCheckStore s = nand_check_store(base + static_cast<std::uint32_t>(at));
    std::uint8_t* w = mem.data() + (s.word - base);
    const std::uint32_t after = nand_check_opened(be32(w), s.shift);
    put32(w, after);
    EXPECT_EQ(nand_check_half(be32(w), s.shift), 0xE001u);
    return static_cast<long>(at);
}

}  // namespace

int main() {
    // The match at a word boundary and at a halfword one: only the beq's
    // two bytes change, the rest of the word is kept.
    for (std::size_t at : {std::size_t(0x100), std::size_t(0x102)}) {
        std::vector<std::uint8_t> mem(0x400);
        for (std::size_t i = 0; i < mem.size(); ++i) mem[i] = static_cast<std::uint8_t>(i * 7 + 3);
        std::memcpy(mem.data() + at, kNandCheck, sizeof kNandCheck);
        std::vector<std::uint8_t> before = mem;
        EXPECT_EQ(patch(mem), static_cast<long>(at));
        EXPECT_EQ(mem[at + 2], 0xE0);
        EXPECT_EQ(mem[at + 3], 0x01);
        before[at + 2] = 0xE0;
        before[at + 3] = 0x01;
        EXPECT_TRUE(mem == before);
    }
    // Two copies (one in a heap, say): only the first is written.
    {
        std::vector<std::uint8_t> mem(0x400, 0);
        std::memcpy(mem.data() + 0x40, kNandCheck, sizeof kNandCheck);
        std::memcpy(mem.data() + 0x200, kNandCheck, sizeof kNandCheck);
        EXPECT_EQ(patch(mem), 0x40L);
        EXPECT_EQ(mem[0x202], 0xD0);
    }
    // At an odd offset it is not Thumb code: not found. Nor past the end.
    {
        std::vector<std::uint8_t> mem(0x40, 0);
        std::memcpy(mem.data() + 0x11, kNandCheck, sizeof kNandCheck);
        EXPECT_EQ(patch(mem), -1L);
        std::vector<std::uint8_t> tail(kNandCheck, kNandCheck + 5);
        EXPECT_EQ(patch(tail), -1L);
    }
    if (g_failures == 0) {
        std::cout << "nandcheck tests passed" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
