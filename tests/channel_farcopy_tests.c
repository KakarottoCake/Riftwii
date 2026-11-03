// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The far copy's checks and plan (channel/common/dolboot.c): a DOL that
// lands on the running program, as WiiSX and Wii64 land on RiftWii, is
// started by a routine at the top of MEM2's free part. The routine itself
// (dolfar.S) runs here from its assembled bytes, tests/fixtures/dolfar.bin,
// in a PowerPC interpreter of just the instructions it uses, over two
// small windows standing for MEM1 and MEM2.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

    // Whether the line has a place, or the app is refused (it would start
    // without its arguments).
    const u32 file_hi = FILE_AT + FILE_SIZE;
    expect(line_fits(&big, text0, 32, FILE_AT, file_hi, SELF_LO, SELF_HI, FREE_LO, FREE_HI),
           "WiiSX's line, on RiftWii, is written by the far copy");
    expect(!line_fits(&big, text0, 32, FILE_AT, file_hi, SELF_LO, SELF_HI, FREE_LO, FREE_LO),
           "without the far copy's room, WiiSX's line has no place");
    expect(line_fits(&below, text0, 0x40, FILE_AT, file_hi, SELF_LO, SELF_HI, FREE_LO, FREE_HI),
           "a line that alone reaches RiftWii goes through the far copy");
    expect(!line_fits(&below, text0, 0x40, FILE_AT, file_hi, SELF_LO, SELF_HI, FREE_LO, FREE_LO),
           "a line that alone reaches RiftWii, without the far copy's room, has no place");
    const DolHeader clear_of_us = yawmm();
    expect(line_fits(&clear_of_us, text0, 0x40, FILE_AT, file_hi, SELF_LO, SELF_HI, FREE_LO, FREE_LO),
           "a line clear of RiftWii needs no far copy");
    expect(!line_fits(&top, text0, 0x20, FILE_AT, file_hi, SELF_LO, SELF_HI, FREE_LO, FREE_HI),
           "a line past MEM1's free part has no place");

    memset(text0 + 4, 0, 4);
    expect(!takes_args(&big, text0), "no magic, no argument block");
    expect(line_fits(&top, text0, 0x20, FILE_AT, file_hi, SELF_LO, SELF_HI, FREE_LO, FREE_LO),
           "an app that reads no arguments needs no place for them");
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

// ------------------------------------------------- running dolfar.S ----
// Two windows of Wii memory; any access outside them fails the run.
#define WIN1_AT 0x80003000u
#define WIN2_AT 0x93300000u
#define WIN_BYTES 0x10000u
#define LINE_BYTES 32u
#define WIN_LINES (WIN_BYTES / LINE_BYTES)

typedef struct {
    u8 mem[2][WIN_BYTES];
    // Per cache line, the step of its last store, dcbf and icbi (0: none).
    u32 stored[2][WIN_LINES], flushed[2][WIN_LINES], invalidated[2][WIN_LINES];
    u32 gpr[32];
    u32 ctr, pc, cr0;  // cr0: LT 8, GT 4, EQ 2 (CR bits 0, 1, 2)
    u32 step;
    u32 jumped_to;
    const char* fault;
    u32 fault_at;
} Ppc;

static u8* ppc_byte(Ppc* p, u32 address, int* window) {
    for (int w = 0; w < 2; ++w) {
        const u32 base = w ? WIN2_AT : WIN1_AT;
        if (address - base < WIN_BYTES) {
            if (window) *window = w;
            return &p->mem[w][address - base];
        }
    }
    if (!p->fault) {
        p->fault = "an access outside the test's memory";
        p->fault_at = address;
    }
    return NULL;
}

static u32 ppc_read32(Ppc* p, u32 address) {
    u32 value = 0;
    for (u32 i = 0; i < 4; ++i) {
        const u8* b = ppc_byte(p, address + i, NULL);
        value = value << 8 | (b ? *b : 0);
    }
    return value;
}

static void ppc_write8(Ppc* p, u32 address, u8 value) {
    int w = 0;
    u8* b = ppc_byte(p, address, &w);
    if (!b) return;
    *b = value;
    p->stored[w][(address - (w ? WIN2_AT : WIN1_AT)) / LINE_BYTES] = p->step;
}

static void ppc_cache_op(Ppc* p, u32 address, bool invalidate) {
    int w = 0;
    if (!ppc_byte(p, address, &w)) return;
    const u32 line = (address - (w ? WIN2_AT : WIN1_AT)) / LINE_BYTES;
    if (invalidate) p->invalidated[w][line] = p->step;
    else p->flushed[w][line] = p->step;
}

static void ppc_compare(Ppc* p, int32_t a, int32_t b, bool is_unsigned) {
    const bool lt = is_unsigned ? (u32)a < (u32)b : a < b;
    const bool gt = is_unsigned ? (u32)a > (u32)b : a > b;
    p->cr0 = lt ? 8 : gt ? 4 : 2;
}

static u32 ppc_mask(u32 mb, u32 me) {
    const u32 from = 0xffffffffu >> mb, to = 0xffffffffu << (31 - me);
    return mb <= me ? from & to : from | to;
}

// One instruction; false once it jumps out (bctr) or faults.
static bool ppc_step(Ppc* p) {
    const u32 w = ppc_read32(p, p->pc);
    if (p->fault) return false;
    ++p->step;
    const u32 op = w >> 26, d = (w >> 21) & 31, a = (w >> 16) & 31, b = (w >> 11) & 31;
    const int32_t simm = (int16_t)(w & 0xffff);
    const u32 base = a ? p->gpr[a] : 0;
    u32 next = p->pc + 4;
    switch (op) {
    case 14:  // addi (li)
        p->gpr[d] = base + (u32)simm;
        break;
    case 11:  // cmpwi to cr0
        if (d != 0) p->fault = "a compare into another field than cr0";
        ppc_compare(p, (int32_t)p->gpr[a], simm, false);
        break;
    case 32:  // lwz
        p->gpr[d] = ppc_read32(p, base + (u32)simm);
        break;
    case 35: {  // lbzu
        const u32 at = p->gpr[a] + (u32)simm;
        const u8* byte = ppc_byte(p, at, NULL);
        p->gpr[d] = byte ? *byte : 0;
        p->gpr[a] = at;
        break;
    }
    case 37: {  // stwu
        const u32 at = p->gpr[a] + (u32)simm;
        for (u32 i = 0; i < 4; ++i) ppc_write8(p, at + i, (u8)(p->gpr[d] >> (24 - 8 * i)));
        p->gpr[a] = at;
        break;
    }
    case 39: {  // stbu
        const u32 at = p->gpr[a] + (u32)simm;
        ppc_write8(p, at, (u8)p->gpr[d]);
        p->gpr[a] = at;
        break;
    }
    case 21: {  // rlwinm
        const u32 rotated = (p->gpr[d] << b) | (b ? p->gpr[d] >> (32 - b) : 0);
        p->gpr[a] = rotated & ppc_mask((w >> 6) & 31, (w >> 1) & 31);
        break;
    }
    case 18:  // b
        if (w & 3) p->fault = "an absolute or linking branch";
        next = p->pc + (u32)(((int32_t)(w << 6) >> 6) & ~3);
        break;
    case 16: {  // bc
        if (w & 3) p->fault = "an absolute or linking branch";
        const u32 bo = d;
        if (!(bo & 4)) --p->ctr;
        const bool ctr_ok = (bo & 4) || ((p->ctr != 0) != ((bo & 2) != 0));
        const bool cr_bit = a < 3 && (p->cr0 >> (3 - a)) & 1;
        if (a >= 3 && !(bo & 16)) p->fault = "a branch on another field than cr0";
        const bool cond_ok = (bo & 16) || (cr_bit == ((bo & 8) != 0));
        if (ctr_ok && cond_ok) next = p->pc + (u32)((int32_t)(int16_t)(w & 0xfffc));
        break;
    }
    case 19:
        if (((w >> 1) & 1023) == 150) break;  // isync
        if (((w >> 1) & 1023) == 528 && d == 20 && !(w & 1)) {  // bctr
            p->jumped_to = p->ctr;
            return false;
        }
        p->fault = "an unexpected opcode 19 instruction";
        break;
    case 31:
        switch ((w >> 1) & 1023) {
        case 444:  // or (mr)
            p->gpr[a] = p->gpr[d] | p->gpr[b];
            break;
        case 266:  // add
            p->gpr[d] = p->gpr[a] + p->gpr[b];
            break;
        case 467:  // mtspr, CTR only
            if ((a | b << 5) != 9) p->fault = "a move to another register than CTR";
            p->ctr = p->gpr[d];
            break;
        case 32:  // cmplw to cr0
            if (d != 0) p->fault = "a compare into another field than cr0";
            ppc_compare(p, (int32_t)p->gpr[a], (int32_t)p->gpr[b], true);
            break;
        case 86:  // dcbf
            ppc_cache_op(p, base + p->gpr[b], false);
            break;
        case 982:  // icbi
            ppc_cache_op(p, base + p->gpr[b], true);
            break;
        case 598:  // sync
            break;
        default:
            p->fault = "an unexpected opcode 31 instruction";
        }
        if (w & 1) p->fault = "an instruction that records into cr0";
        break;
    default:
        p->fault = "an instruction the interpreter doesn't know";
    }
    if (p->fault) {
        p->fault_at = p->pc;
        return false;
    }
    p->pc = next;
    return true;
}

static void put32(u8* to, u32 value) {
    for (u32 i = 0; i < 4; ++i) to[i] = (u8)(value >> (24 - 8 * i));
}

// The plan as the Wii holds it: dolboot.c's FarPlan, big-endian.
static void put_plan(u8* to, const FarPlan* plan) {
    put32(to + offsetof(FarPlan, count), plan->count);
    for (u32 i = 0; i < FAR_COPIES; ++i) {
        for (u32 j = 0; j < 3; ++j) put32(to + offsetof(FarPlan, copy) + (i * 3 + j) * 4, plan->copy[i][j]);
    }
    for (u32 j = 0; j < 3; ++j) put32(to + offsetof(FarPlan, args) + j * 4, plan->args[j]);
    memcpy(to + offsetof(FarPlan, line), plan->line, sizeof(plan->line));
}

static u8* read_routine(const char* dir, u32* size) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/dolfar.bin", dir);
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    static u8 code[FAR_CODE_MAX + 1];
    *size = (u32)fread(code, 1, sizeof(code), f);
    fclose(f);
    return code;
}

// A small DOL with odd sizes in the test's MEM1 window, its file copy and
// the routine's block in the MEM2 window: the routine must copy the
// sections, clear the BSS, write the line and the "_arg" block, flush and
// invalidate every line it wrote, and jump to the entry with its own stack,
// touching nothing else.
static void test_run(const char* fixtures) {
    u32 code_size = 0;
    const u8* code = read_routine(fixtures, &code_size);
    expect(code != NULL, "tests/fixtures/dolfar.bin can be read");
    if (!code) return;
    expect(code_size > 0 && code_size <= FAR_CODE_MAX, "the routine fits before its plan");

    DolHeader h = {0};
    h.offset[0] = 0x100;
    h.address[0] = 0x80003f00;
    h.size[0] = 0x100;
    h.offset[1] = 0x200;
    h.address[1] = 0x80004100;
    h.size[1] = 0x235;
    h.bss_address = 0x80004340;
    h.bss_size = 0x1c5;
    h.entry = 0x80003f00;
    const u32 file_at = WIN2_AT;
    const u32 block_at = WIN2_AT + 0xc000;
    const u32 plan_at = block_at + FAR_PLAN_AT;
    const u32 stack_top = block_at + FAR_BYTES;

    static Ppc p;
    memset(&p, 0, sizeof(p));
    memset(p.mem[0], 0xa5, WIN_BYTES);
    memset(p.mem[1], 0x5a, WIN_BYTES);
    for (u32 i = 0; i < 0x100 + 0x235; ++i) p.mem[1][0x100 + i] = (u8)(i * 7 + 3);
    put32(&p.mem[1][0x100 + 4], ARGV_MAGIC);  // the first section reads its arguments

    const char line[] = "sd:/apps/test/boot.dol\0";
    const u32 length = sizeof(line);
    const u32 line_at = line_address(&h, length);
    expect(line_at == 0x80004520u, "the test's line goes after its BSS, 32-byte aligned");
    static FarPlan plan;
    memset(&plan, 0, sizeof(plan));
    far_plan_fill(&plan, plan_at, &h, file_at, line_at, line, length);
    memcpy(&p.mem[1][block_at - WIN2_AT], code, code_size);
    put_plan(&p.mem[1][plan_at - WIN2_AT], &plan);

    // What the run must leave: MEM1 as if copied by hand, MEM2 unchanged
    // but for the routine's one stack word.
    static u8 want[2][WIN_BYTES];
    memcpy(want, p.mem, sizeof(want));
    memcpy(&want[0][0x80003f00 - WIN1_AT], &p.mem[1][0x100], 0x100);
    memcpy(&want[0][0x80004100 - WIN1_AT], &p.mem[1][0x200], 0x235);
    memset(&want[0][0x80004340 - WIN1_AT], 0, 0x1c5);
    memcpy(&want[0][line_at - WIN1_AT], line, length);
    put32(&want[0][0x80003f08 - WIN1_AT], ARGV_MAGIC);
    put32(&want[0][0x80003f0c - WIN1_AT], line_at);
    put32(&want[0][0x80003f10 - WIN1_AT], length);
    memset(&want[1][stack_top - 16 - WIN2_AT], 0, 4);

    for (int r = 0; r < 32; ++r) p.gpr[r] = 0xdeadbeefu;
    p.ctr = 0xdeadbeefu;
    p.gpr[3] = plan_at;
    p.gpr[4] = h.entry;
    p.gpr[5] = stack_top;
    p.pc = block_at;
    while (p.step < 1000000 && ppc_step(&p)) {
    }
    if (p.fault) printf("  the routine stopped on %s at 0x%08x\n", p.fault, (unsigned)p.fault_at);
    expect(!p.fault, "the routine runs to its jump");
    expect(p.jumped_to == h.entry, "the routine jumps to the entry point");
    expect(p.gpr[1] == stack_top - 16, "the program starts on the routine's own stack, one frame down");
    expect(memcmp(p.mem[0], want[0], WIN_BYTES) == 0,
           "MEM1 holds the sections, the cleared BSS, the line and the \"_arg\" block, and nothing else changed");
    expect(memcmp(p.mem[1], want[1], WIN_BYTES) == 0, "MEM2 is unchanged but for the stack's back chain");

    bool cached = true;
    for (u32 line_no = 0; line_no < WIN_LINES; ++line_no) {
        const u32 stored = p.stored[0][line_no];
        if (stored && (p.flushed[0][line_no] <= stored || p.invalidated[0][line_no] <= p.flushed[0][line_no])) {
            printf("  line 0x%08x: stored at step %u, flushed %u, invalidated %u\n",
                   (unsigned)(WIN1_AT + line_no * LINE_BYTES), (unsigned)stored, (unsigned)p.flushed[0][line_no],
                   (unsigned)p.invalidated[0][line_no]);
            cached = false;
        }
    }
    expect(cached, "every line written is flushed from the data cache, then invalidated in the instruction cache");
}

int main(int argc, char** argv) {
    test_checks();
    test_place();
    test_line();
    test_plan();
    test_run(argc > 1 ? argv[1] : "tests/fixtures");
    if (!failures) puts("channel far copy: all passed");
    return failures ? 1 : 0;
}
