// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The GameCube loader's MEM1 layout: the game's stack top found in a
// synthetic __init_registers (as Mario Golf's and Mario Power Tennis's
// set it, at the top of MEM1, and as Super Mario Sunshine's SDK sets it,
// after the sections), the SDK's ArenaLo found in a synthetic OSInit, and
// RiftWii's MEM1 pieces packed under the FST around the DI stand-in.
#include "riftwii/gclayout.hpp"

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

std::uint32_t bl(std::uint32_t from, std::uint32_t to) { return 0x48000001 | ((to - from) & 0x03FFFFFC); }

constexpr std::uint32_t kEntry = 0x80003154;
constexpr std::uint32_t kInitRegisters = 0x80003304;

// __start: bl __init_registers, bl __init_hardware, li r0,-1, stwu r1,-8(r1).
void put_start(Text& text) {
    text.put(kEntry, {bl(kEntry, kInitRegisters), bl(kEntry + 4, 0x80003400), 0x3800FFFF, 0x9421FFF8});
    text.put(0x80003400, {0x4E800020});
}

void finds_a_stack_at_the_top_of_mem1() {
    Text text;
    put_start(text);
    // Mario Golf's __init_registers: registers zeroed, then
    // lis r1,0x817d; ori r1,r1,0xc000; lis r2; ori r2; lis r13; ori r13; blr.
    text.put(kInitRegisters, {0x38000000, 0x38600000, 0x38800000, 0x3C20817D, 0x6021C000, 0x3C408031, 0x6042A000,
                              0x3DA08031, 0x61AD8000, 0x4E800020});
    const GcStackTop top = find_gc_stack_top(text.reader(), kEntry);
    EXPECT_EQ(top.address, 0x817dc000u);
    EXPECT_EQ(top.set_at, kInitRegisters + 16);
}

void finds_a_stack_after_the_sections() {
    Text text;
    put_start(text);
    // Super Mario Sunshine's SDK: lis r1,0x8042; ori r1,r1,0x77e8.
    text.put(kInitRegisters, {0x3C208042, 0x602177E8, 0x4E800020});
    EXPECT_EQ(find_gc_stack_top(text.reader(), kEntry).address, 0x804277e8u);
}

void finds_an_addi_lower_half() {
    Text text;
    put_start(text);
    // lis r1,0x8043; addi r1,r1,-0x7000 (0x80429000), the other spelling.
    text.put(kInitRegisters, {0x3C208043, 0x38219000, 0x4E800020});
    EXPECT_EQ(find_gc_stack_top(text.reader(), kEntry).address, 0x80429000u);
}

void finds_a_stack_set_in_start() {
    Text text;
    text.put(kEntry, {0x3C20817F, 0x60210000, bl(kEntry + 8, 0x80003400)});
    text.put(0x80003400, {0x4E800020});
    EXPECT_EQ(find_gc_stack_top(text.reader(), kEntry).address, 0x817f0000u);
}

void ignores_what_is_not_the_pair() {
    Text text;
    put_start(text);
    // An epilogue's addi r1,r1,0x18 with no lis r1 before it, then blr:
    // the stack top stays unknown.
    text.put(kInitRegisters, {0x38210018, 0x4E800020});
    EXPECT_EQ(find_gc_stack_top(text.reader(), kEntry).address, 0u);
    // A value outside MEM1 is no stack top either.
    Text outside;
    put_start(outside);
    outside.put(kInitRegisters, {0x3C20CC00, 0x60210000, 0x4E800020});
    EXPECT_EQ(find_gc_stack_top(outside.reader(), kEntry).address, 0u);
    // No code at the entry point.
    EXPECT_EQ(find_gc_stack_top(Text{}.reader(), kEntry).address, 0u);
}

void finds_the_arena_lo() {
    Text text;
    // Elsewhere: lwz r3,0x30(r31); cmplwi r3,0; beq; a call (not OSInit).
    text.put(0x800bb1b4, {0x807F0030, 0x28030000, 0x41820020, 0x48207DF1, 0x7C641B78});
    // OSInit: lwz r3,0x30(r30); cmplwi r3,0; bne; lis r3,0x817e; ori r3,r3,0x0000.
    text.put(0x80341e68, {0x807E0030, 0x28030000, 0x40820010, 0x3C60817E, 0x60630000, 0x4800152D});
    const std::vector<GcTextRange> ranges = {{0x800bb000, 0x800bb200}, {0x80341e00, 0x80341f00}};
    EXPECT_EQ(find_gc_arena_lo(text.reader(), ranges), 0x817e0000u);
    // Super Mario Sunshine's SDK: lis r3,0x8042; ori r3,r3,0xd380.
    Text sunshine;
    sunshine.put(0x80341e68, {0x807E0030, 0x28030000, 0x40820010, 0x3C608042, 0x6063D380});
    EXPECT_EQ(find_gc_arena_lo(sunshine.reader(), {{0x80341e68, 0x80341e7c}}), 0x8042d380u);
    EXPECT_EQ(find_gc_arena_lo(Text{}.reader(), {{0x80341e68, 0x80341e7c}}), 0u);
}

void packs_under_the_fst() {
    // Super Mario Eclipse's FST at 0x817fcee0: the stand-in at 0x817f6000,
    // the small pieces above it, the blob under it.
    std::vector<GcMem1Piece> pieces = {{0x5d00, 0}, {0x440, 0}, {0x100, 0}, {0x100, 0}, {0x40, 0}};
    const GcMem1Plan plan = plan_gc_mem1(0x817fcee0, pieces, 0x2000);
    EXPECT_EQ(plan.di_regs, 0x817f6000u);
    EXPECT_EQ(pieces[0].address, 0x817f71e0u);
    for (const GcMem1Piece& p : pieces) {
        const bool clear_of_di = p.address + p.bytes <= plan.di_regs || p.address >= plan.di_regs + kGcDiRegsBytes;
        EXPECT_TRUE(clear_of_di);
        EXPECT_TRUE(p.address + p.bytes <= 0x817fcee0u);
        EXPECT_TRUE(p.address >= plan.bi2 + 0x2000);
        EXPECT_EQ(p.address & 31u, 0u);
    }
    // No two pieces overlap.
    for (std::size_t i = 0; i < pieces.size(); ++i)
        for (std::size_t j = i + 1; j < pieces.size(); ++j) {
            const bool apart = pieces[i].address + pieces[i].bytes <= pieces[j].address ||
                               pieces[j].address + pieces[j].bytes <= pieces[i].address;
            EXPECT_TRUE(apart);
        }
    EXPECT_EQ(plan.arena_hi, plan.bi2);
}

void leaves_mario_golf_its_arena() {
    // Mario Golf's stack top and ArenaLo at 0x817dc000, with an FST of
    // 0x10000 bytes: everything of RiftWii's in MEM1 fits above it.
    std::vector<GcMem1Piece> pieces = {{0x5d00, 0}, {0x440, 0}, {0x100, 0}, {0x100, 0}, {0x40, 0}};
    const GcMem1Plan plan = plan_gc_mem1(0x817f0000, pieces, 0x2000);
    EXPECT_EQ(plan.di_regs, 0x817e6000u);
    EXPECT_TRUE(plan.arena_hi >= 0x817dc000u);
    // The old layout's 0x2c000 would not have fitted; what is left over
    // stays the game's.
    EXPECT_TRUE(plan.arena_hi - 0x817dc000u >= 0x2000u);
}

void the_stand_in_ends_in_6000() {
    for (std::uint32_t fst = 0x817c0000; fst < 0x81800000; fst += 0x1320) {
        std::vector<GcMem1Piece> pieces = {{0x5d00, 0}, {0x440, 0}};
        const GcMem1Plan plan = plan_gc_mem1(fst, pieces, 0x2000);
        EXPECT_EQ(plan.di_regs & 0xFFFFu, 0x6000u);
        EXPECT_TRUE(plan.di_regs + kGcDiRegsBytes <= fst);
        EXPECT_TRUE(fst - plan.di_regs < 0x10000u + kGcDiRegsBytes);
    }
}

}  // namespace

int main() {
    finds_a_stack_at_the_top_of_mem1();
    finds_a_stack_after_the_sections();
    finds_an_addi_lower_half();
    finds_a_stack_set_in_start();
    ignores_what_is_not_the_pair();
    finds_the_arena_lo();
    packs_under_the_fst();
    leaves_mario_golf_its_arena();
    the_stand_in_ends_in_6000();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "gclayout tests passed" << std::endl;
    return 0;
}
