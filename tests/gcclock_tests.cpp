// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The GameCube loader's clocks: the speed read from HW_CLOCKS, and
// GameCube clock values built in a game's code (as lis and ori or addi
// pairs) moved to the Wii's, which a game running in Wii mode really has, and
// float or double clocks in its data found (log-only).
#include "riftwii/gcclock.hpp"

#include <cstdint>
#include <iostream>
#include <map>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (0x" << std::hex << (a) << " != 0x" << (b) << std::dec << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

// A game's text, word by word.
struct Text {
    std::map<std::uint32_t, std::uint32_t> words;
    void put(std::uint32_t address, std::initializer_list<std::uint32_t> list) {
        for (std::uint32_t w : list) {
            words[address] = w;
            address += 4;
        }
    }
    GcCodeReader reader() const {
        return [this](std::uint32_t address, std::uint32_t& word) {
            auto found = words.find(address);
            if (found == words.end()) return false;
            word = found->second;
            return true;
        };
    }
};

constexpr std::uint32_t kStart = 0x80005000;
const std::vector<GcTextRange> kText = {{kStart, kStart + 0x100}};

void reads_the_speed_bit() {
    EXPECT_EQ(gc_clocks_from_hw_clocks(0).bus, kWiiClocks.bus);
    EXPECT_EQ(gc_clocks_from_hw_clocks(0).cpu, kWiiClocks.cpu);
    EXPECT_EQ(gc_clocks_from_hw_clocks(1).bus, kGameCubeClocks.bus);
    EXPECT_EQ(gc_clocks_from_hw_clocks(1).cpu, kGameCubeClocks.cpu);
    // Only SPEED counts; the register's other bits (FX) say nothing of it.
    EXPECT_EQ(gc_clocks_from_hw_clocks(0xFFFFFFFE).bus, kWiiClocks.bus);
    EXPECT_EQ(gc_time_base_hz(kGameCubeClocks), 40500000u);
    EXPECT_EQ(gc_time_base_hz(kWiiClocks), 60750000u);
}

void moves_the_bus_clock_built_with_addi() {
    // lis r3,0x09a8; addi r3,r3,-0x1380: 162000000.
    Text text;
    text.put(kStart, {0x3C6009A8, 0x3863EC80});
    const auto fixes = find_gc_clock_values(text.reader(), kText, kWiiClocks);
    EXPECT_EQ(fixes.size(), 1u);
    if (fixes.size() != 1) return;
    EXPECT_EQ(fixes[0].high_at, kStart);
    EXPECT_EQ(fixes[0].low_at, kStart + 4);
    EXPECT_EQ(fixes[0].old_value, 162000000u);
    EXPECT_EQ(fixes[0].new_value, 243000000u);
    // 243000000 is 0x0e7be2c0: the lower half's top bit is set, so addi
    // needs 0x0e7c above it.
    EXPECT_EQ(fixes[0].high_word, 0x3C600E7Cu);
    EXPECT_EQ(fixes[0].low_word, 0x3863E2C0u);
}

void moves_the_cpu_clock_built_with_ori() {
    // lis r5,0x1cf7; ori r5,r5,0xc580: 486000000.
    Text text;
    text.put(kStart, {0x3CA01CF7, 0x60A5C580});
    const auto fixes = find_gc_clock_values(text.reader(), kText, kWiiClocks);
    EXPECT_EQ(fixes.size(), 1u);
    if (fixes.size() != 1) return;
    EXPECT_EQ(fixes[0].new_value, 729000000u);
    EXPECT_EQ(fixes[0].high_word, 0x3CA02B73u);
    EXPECT_EQ(fixes[0].low_word, 0x60A5A840u);
}

void moves_the_time_base_with_an_instruction_between() {
    // lis r6,0x026a; li r3,0; addi r6,r6,-0x4e0: 40500000, the time base.
    Text text;
    text.put(kStart, {0x3CC0026A, 0x38600000, 0x38C6FB20});
    const auto fixes = find_gc_clock_values(text.reader(), kText, kWiiClocks);
    EXPECT_EQ(fixes.size(), 1u);
    if (fixes.size() != 1) return;
    EXPECT_EQ(fixes[0].low_at, kStart + 8);
    EXPECT_EQ(fixes[0].new_value, 60750000u);
    EXPECT_EQ(fixes[0].high_word, 0x3CC0039Fu);
    EXPECT_EQ(fixes[0].low_word, 0x38C6F8B0u);
}

void leaves_pairs_it_cannot_be_sure_of() {
    Text text;
    // lis r3,0x09a8; mr r3,r4; addi r3,r3,-0x1380: r3 changed between.
    text.put(kStart, {0x3C6009A8, 0x7C832378, 0x3863EC80});
    // lis r3,0x09a8; b +8; addi r3,r3,-0x1380: a branch between.
    text.put(kStart + 0x20, {0x3C6009A8, 0x48000008, 0x3863EC80});
    // lis r3,0x09a8; addi r3,r3,0: not a clock.
    text.put(kStart + 0x40, {0x3C6009A8, 0x38630000});
    // lis r0,0x09a8; li r0,-0x1380 (addi r0,0,...): r0 replaced, not finished.
    text.put(kStart + 0x80, {0x3C0009A8, 0x3800EC80});
    // lis r3,0x09a8 and its lower half five instructions on: too far.
    text.put(kStart + 0x60, {0x3C6009A8, 0x60000000, 0x60000000, 0x60000000, 0x60000000, 0x3863EC80});
    EXPECT_EQ(find_gc_clock_values(text.reader(), kText, kWiiClocks).size(), 0u);
}

void moves_a_value_built_in_r0_with_ori() {
    // lis r0,0x1cf7; ori r0,r0,0xc580: 486000000 in r0, which ori does read.
    Text text;
    text.put(kStart, {0x3C001CF7, 0x6000C580});
    const auto fixes = find_gc_clock_values(text.reader(), kText, kWiiClocks);
    EXPECT_EQ(fixes.size(), 1u);
    if (fixes.size() != 1) return;
    EXPECT_EQ(fixes[0].new_value, 729000000u);
}

void leaves_everything_at_gamecube_speed() {
    Text text;
    text.put(kStart, {0x3C6009A8, 0x3863EC80});
    EXPECT_EQ(find_gc_clock_values(text.reader(), kText, kGameCubeClocks).size(), 0u);
}

void finds_float_and_double_clocks_in_data() {
    // 162e6 as a float, 486e6 as a double, 40.5e6 as a float, and a word
    // that is only the first half of a double.
    Text data;
    data.put(kStart, {0x4D1A7EC8, 0x41BCF7C5, 0x80000000, 0x4C1A7EC8, 0x41834FD9, 0x12345678});
    const auto found = find_gc_clock_constants(data.reader(), kText);
    EXPECT_EQ(found.size(), 3u);
    if (found.size() != 3) return;
    EXPECT_EQ(found[0].address, kStart);
    EXPECT_TRUE(!found[0].is_double);
    EXPECT_EQ(found[0].hz, 162000000u);
    EXPECT_EQ(found[1].address, kStart + 4);
    EXPECT_TRUE(found[1].is_double);
    EXPECT_EQ(found[1].hz, 486000000u);
    EXPECT_EQ(found[2].address, kStart + 12);
    EXPECT_TRUE(!found[2].is_double);
    EXPECT_EQ(found[2].hz, 40500000u);
}

}  // namespace

int main() {
    reads_the_speed_bit();
    moves_the_bus_clock_built_with_addi();
    moves_the_cpu_clock_built_with_ori();
    moves_the_time_base_with_an_instruction_between();
    leaves_pairs_it_cannot_be_sure_of();
    moves_a_value_built_in_r0_with_ori();
    leaves_everything_at_gamecube_speed();
    finds_float_and_double_clocks_in_data();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "gcclock tests passed" << std::endl;
    return 0;
}
