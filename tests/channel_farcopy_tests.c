// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The far copy's checks and plan (channel/common/dolboot.c): a DOL that
// lands on the running program, as WiiSX and Wii64 land on RiftWii, is
// started by a routine at the top of MEM2's free part. Nothing here runs
// PowerPC code or touches Wii addresses.
#include <stdio.h>
#include <string.h>
#include "../channel/common/dolboot.c"

u8 __app_start[1];
u8 __bss_end[1];
const u8 dolfar_code[1];
const u8 dolfar_code_end[1];

static int failures;

static void expect(bool ok, const char* what) {
    if (!ok) {
        printf("FAILED: %s\n", what);
        ++failures;
    }
}

// RiftWii 2610-783 (__app_start to __bss_end), and MEM2's free part
// under the usual IOS.
#define SELF_LO 0x80a00000u
#define SELF_HI 0x80f56400u
#define FREE_LO 0x90a00000u
#define FREE_HI 0x933e0000u
#define FILE_AT 0x90900000u
#define FILE_SIZE 0x240000u

static bool fits(const DolHeader* h, u32 file_at, u32 free_lo, u32 free_hi) {
    return dol_fits(h, FILE_SIZE, file_at, SELF_LO, SELF_HI, free_lo, free_hi);
}

// WiiSX with the adapter library: its sections from 0x80003f00, its BSS
// (the emulated PlayStation's memory) ending at 0x80b70b60, inside RiftWii.
static DolHeader wiisx(void) {
    DolHeader h = {0};
    h.offset[0] = 0x100;
    h.address[0] = 0x80003f00;
    h.size[0] = 0x100;
    h.offset[1] = 0x200;
    h.address[1] = 0x80004000;
    h.size[1] = 0x1f0000;
    h.offset[7] = 0x1f0200;
    h.address[7] = 0x801f4000;
    h.size[7] = 0x40000;
    h.bss_address = 0x80238ee0;
    h.bss_size = 0x937c80;
    h.entry = 0x80004000;
    return h;
}

// YAWMM: wholly below RiftWii.
static DolHeader yawmm(void) {
    DolHeader h = {0};
    h.offset[0] = 0x100;
    h.address[0] = 0x80004000;
    h.size[0] = 0xe0000;
    h.bss_address = 0x800edc40;
    h.bss_size = 0x7df00;
    h.entry = 0x80004000;
    return h;
}

static void test_checks(void) {
    const DolHeader big = wiisx();
    expect(reaches(&big, 0, 0, SELF_LO, SELF_HI), "WiiSX's BSS reaches RiftWii");
    expect(fits(&big, FILE_AT, FREE_LO, FREE_HI), "WiiSX is accepted with room for the far copy");
    expect(!fits(&big, FILE_AT, FREE_LO, FREE_LO + FAR_BYTES - 32), "WiiSX is refused without room for the far copy");
    expect(!fits(&big, FREE_HI - 0x1000, FREE_LO, FREE_HI), "the far copy may not go over the file copy");

    const DolHeader small = yawmm();
    expect(!reaches(&small, 0, 0, SELF_LO, SELF_HI), "YAWMM lands clear of RiftWii");
    expect(fits(&small, FILE_AT, FREE_LO, FREE_LO), "YAWMM needs no far copy, so no MEM2 room");

    DolHeader over_file = yawmm();
    expect(!dol_fits(&over_file, FILE_SIZE, 0x80100000u, SELF_LO, SELF_HI, FREE_LO, FREE_HI),
           "a DOL landing on its own file copy is refused");

    DolHeader too_high = wiisx();
    too_high.bss_address = 0x81600000;
    too_high.bss_size = 0x200000;
    expect(!fits(&too_high, FILE_AT, FREE_LO, FREE_HI), "a BSS past MEM1's free part is refused");

    DolHeader no_entry = wiisx();
    no_entry.entry = 0x80b00000;  // in the BSS: nothing loaded there
    expect(!fits(&no_entry, FILE_AT, FREE_LO, FREE_HI), "an entry outside the sections is refused");
}

static void test_place(void) {
    const DolHeader big = wiisx();
    const u32 at = far_block_in(FREE_LO, FREE_HI + 7, &big, FILE_AT, FILE_AT + FILE_SIZE, 0, 0);
    expect(at == FREE_HI - FAR_BYTES, "the far copy sits at the top of MEM2's free part, 32-byte aligned");
    expect(at % 32 == 0 && FAR_BYTES % 32 == 0, "the far copy is in whole cache lines");
    expect(far_block_in(FREE_HI, FREE_LO, &big, FILE_AT, FILE_AT + FILE_SIZE, 0, 0) == 0,
           "an empty free part has no room");
    // A plan that would write over the block itself (a damaged header
    // can't get here past dol_fits, but the block checks anyway).
    DolHeader into_block = wiisx();
    into_block.bss_address = FREE_HI - 0x100;
    into_block.bss_size = 0x100;
    expect(far_block_in(FREE_LO, FREE_HI, &into_block, FILE_AT, FILE_AT + FILE_SIZE, 0, 0) == 0,
           "the far copy is never where the DOL lands");
}

static void test_line(void) {
    const DolHeader big = wiisx();
    expect(line_address(&big, 32) == 0x80b70b60u, "WiiSX's argument line goes right after its BSS");
    DolHeader top = yawmm();
    top.bss_address = MEM1_HI - 0x40;
    top.bss_size = 0x30;
    expect(line_address(&top, 0x20) == 0, "a line that would pass MEM1's free part has no place");

    // A DOL clear of RiftWii whose line alone would land on it.
    DolHeader below = yawmm();
    below.bss_address = SELF_LO - 0x100;
    below.bss_size = 0xf0;
    const u32 line_at = line_address(&below, 0x40);
    expect(!reaches(&below, 0, 0, SELF_LO, SELF_HI) && reaches(&below, line_at, 0x40, SELF_LO, SELF_HI),
           "a line that reaches RiftWii is seen even when the sections don't");

    u8 text0[8 + sizeof(struct __argv)] = {0};
    const u32 magic = ARGV_MAGIC;
    memcpy(text0 + 4, &magic, 4);
    expect(takes_args(&big, text0), "the \"_arg\" magic is read from the file copy");
    memset(text0 + 4, 0, 4);
    expect(!takes_args(&big, text0), "no magic, no argument block");
}

static void test_plan(void) {
    const DolHeader big = wiisx();
    static FarPlan plan;
    const u32 plan_at = 0x933dfa00u;
    const char line[] = "sd:/apps/wiiSX-wup/boot.dol\0";
    const u32 length = sizeof(line);
    const u32 line_at = line_address(&big, length);
    far_plan_fill(&plan, plan_at, &big, FILE_AT, line_at, line, length);

    expect(plan.count == 6, "three sections, the BSS, the line and the \"_arg\" block");
    expect(plan.copy[0][0] == 0x80003f00u && plan.copy[0][1] == FILE_AT + 0x100 && plan.copy[0][2] == 0x100,
           "section 0 is copied from the file");
    expect(plan.copy[2][0] == 0x801f4000u && plan.copy[2][1] == FILE_AT + 0x1f0200,
           "the data section follows the text sections");
    expect(plan.copy[3][0] == big.bss_address && plan.copy[3][1] == 0 && plan.copy[3][2] == big.bss_size,
           "the BSS is cleared after the sections");
    expect(plan.copy[4][0] == line_at && plan.copy[4][1] == plan_at + offsetof(FarPlan, line) &&
               plan.copy[4][2] == length && memcmp(plan.line, line, length) == 0,
           "the line is staged in the plan and copied after the BSS");
    expect(plan.copy[5][0] == 0x80003f08u && plan.copy[5][1] == plan_at + offsetof(FarPlan, args) &&
               plan.copy[5][2] == 12,
           "the \"_arg\" block is written last, 8 bytes into the first section");
    expect(plan.args[0] == ARGV_MAGIC && plan.args[1] == line_at && plan.args[2] == length,
           "the \"_arg\" block holds the magic, the line's address and its length");

    far_plan_fill(&plan, plan_at, &big, FILE_AT, 0, line, length);
    expect(plan.count == 4, "without an argument block only the sections and the BSS");
    expect(FAR_PLAN_AT + sizeof(FarPlan) + FAR_STACK <= FAR_BYTES, "the routine, plan and stack fit the block");
}

int main(void) {
    test_checks();
    test_place();
    test_line();
    test_plan();
    if (!failures) puts("channel far copy: all passed");
    return failures ? 1 : 0;
}
