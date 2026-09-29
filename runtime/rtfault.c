/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A game's crash record (rtfault.h). Freestanding: no libc, no constant
 * data, like the other blobs' code. */
#include "rtfault.h"

#include "rtshot.h"

typedef char rtfault_record_size_check[sizeof(struct rtfault_record) == RTFAULT_RECORD_BYTES ? 1 : -1];

int rtfault_is_crash(uint32_t exception, uint32_t srr1) {
    switch (exception) {
    case RTFAULT_MACHINE_CHECK:
    case RTFAULT_DSI:
    case RTFAULT_ISI:
    case RTFAULT_ALIGNMENT:
    case RTFAULT_FLOATING_POINT:
        return 1;
    case RTFAULT_PROGRAM:
        return (srr1 & RTFAULT_SRR1_FPE) == 0;
    default:
        return 0;
    }
}

int rtfault_in_ram(uint32_t address) {
    if (address & 3u) return 0;
    return (address >= 0x80000000u && address < 0x81800000u) || (address >= 0x90000000u && address < 0x94000000u);
}

static uint32_t load(uint32_t address) { return *(const volatile uint32_t*)(uintptr_t)address; }

void rtfault_fill(struct rtfault_record* r, uint32_t exception, const uint32_t* context, uint32_t dsisr, uint32_t dar,
                  const char* game, uint32_t tb_hi, uint32_t tb_lo, uint32_t ticks_per_second, int memory_ok) {
    uint32_t i;
    uint32_t* w = (uint32_t*)(void*)r;
    for (i = 0; i < RTFAULT_RECORD_BYTES / 4u; ++i) w[i] = 0;
    r->magic = RTFAULT_MAGIC;
    r->version = RTFAULT_VERSION;
    for (i = 0; i < 8u; ++i) r->game[i] = game[i];
    r->exception = exception;
    r->srr0 = context[RTFAULT_CTX_SRR0];
    r->srr1 = context[RTFAULT_CTX_SRR1];
    r->dsisr = dsisr;
    r->dar = dar;
    r->lr = context[RTFAULT_CTX_LR];
    r->cr = context[RTFAULT_CTX_CR];
    r->ctr = context[RTFAULT_CTX_CTR];
    r->xer = context[RTFAULT_CTX_XER];
    for (i = 0; i < 32u; ++i) r->gpr[i] = context[i];
    r->tb_hi = tb_hi;
    r->tb_lo = tb_lo;
    r->ticks_per_second = ticks_per_second;
    if (!memory_ok) return;
    /* The saved return addresses: each frame's back chain at 0, the LR
     * its callee saved at 4 of it. Up the stack only, in RAM only. */
    {
        uint32_t sp = r->gpr[1];
        for (i = 0; i < RTFAULT_FRAMES && rtfault_in_ram(sp); ++i) {
            const uint32_t next = load(sp);
            if (!rtfault_in_ram(next) || next <= sp) break;
            r->frames[r->frame_count++] = load(next + 4u);
            sp = next;
        }
    }
    if (rtfault_in_ram(r->srr0 - 16u) && rtfault_in_ram(r->srr0 + 4u * (RTFAULT_CODE_WORDS - 4u) - 4u)) {
        for (i = 0; i < RTFAULT_CODE_WORDS; ++i) r->code[i] = load(r->srr0 - 16u + 4u * i);
        r->code_count = RTFAULT_CODE_WORDS;
    }
}

void rtfault_path(char* out) {
    char* p;
    rtshot_dir(out);
    for (p = out; *p; ++p) {
    }
    *p++ = '/'; *p++ = 'c'; *p++ = 'r'; *p++ = 'a'; *p++ = 's'; *p++ = 'h';
    *p++ = '.'; *p++ = 'b'; *p++ = 'i'; *p++ = 'n';
    *p = 0;
}
