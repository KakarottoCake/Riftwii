/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Resident runtime, C part. Rules that keep this position-independent and
 * safe to run inside the game's IOS_IoctlAsync (possibly from an interrupt
 * handler): no globals (all state lives in the context the trampoline hands
 * us), no string literals or other address-taken data (they would need
 * absolute relocations), no library calls, no allocation, no blocking
 * waits without a bound. The build links the blob at two different
 * addresses and checks the binaries are identical.
 */
#include "rt_hook.h"

#define RT_DI_READ 0x71u

int rt_is_di_read(uint32_t ioctl, const uint32_t* in, uint32_t in_len) {
    /* DVDLowRead: ioctl 0x71 with a 0x20-byte command block whose first
     * word repeats the command number in its top byte (wiibrew /dev/di). */
    return ioctl == RT_DI_READ && in_len == 0x20 && in != 0 && (in[0] >> 24) == RT_DI_READ;
}

#ifdef RT_TARGET_PPC
/* EXI register block (Wii: 0xCD006800), five words per channel:
 * CSR, MAR, LENGTH, CR, DATA. The sequence is the one libogc's EXI_Select /
 * EXI_Imm / EXI_Sync / EXI_Deselect perform for a 2-byte immediate
 * read-write transfer with device 0 at 32 MHz (see NOTICE.md). */
static volatile uint32_t* rt_exi(uint32_t channel, uint32_t reg) {
    return (volatile uint32_t*)(0xCD006800u + channel * 0x14u + reg * 4u);
}

static int rt_gecko_command(uint32_t channel, uint32_t command, uint32_t* reply) {
    volatile uint32_t* csr = rt_exi(channel, 0);
    volatile uint32_t* cr = rt_exi(channel, 3);
    volatile uint32_t* data = rt_exi(channel, 4);
    uint32_t spins = 0;
    *csr = (*csr & 0x405u) | 0x80u | (5u << 4);
    *data = command << 16;
    *cr = (1u << 4) | (2u << 2) | 1u;
    while (*cr & 1u) {
        if (++spins > 200000u) {
            *csr &= 0x405u;
            return 0;
        }
    }
    *reply = *data >> 16;
    *csr &= 0x405u;
    return 1;
}

/* USB Gecko protocol (wiibrew USB Gecko, libogc usbgecko.c): 0xC000 asks
 * whether a byte can be sent, 0xB0xx sends one; bit 0x0400 of the reply
 * means yes / accepted. */
static int rt_gecko_putc(struct rt_context* ctx, uint32_t ch) {
    uint32_t reply = 0;
    uint32_t tries;
    for (tries = 0; tries < 1000u; ++tries) {
        if (!rt_gecko_command(ctx->gecko_channel, 0xC000u, &reply)) break;
        if (reply & 0x0400u) {
            if (!rt_gecko_command(ctx->gecko_channel, 0xB000u | ((ch & 0xFFu) << 4), &reply)) break;
            if (reply & 0x0400u) return 1;
        }
    }
    ctx->gecko_failures++;
    return 0;
}
#else
static int rt_gecko_putc(struct rt_context* ctx, uint32_t ch) {
    (void)ctx;
    (void)ch;
    return 1;
}
#endif

static void rt_gecko_hex(struct rt_context* ctx, uint32_t value) {
    int shift;
    for (shift = 28; shift >= 0; shift -= 4) {
        const uint32_t digit = (value >> shift) & 0xFu;
        rt_gecko_putc(ctx, digit < 10u ? '0' + digit : 'a' + digit - 10u);
    }
}

int rt_on_ioctl_async(struct rt_context* ctx, const uintptr_t* args, uint32_t* result) {
    const uint32_t fd = (uint32_t)args[0];
    const uint32_t ioctl = (uint32_t)args[1];
    const uint32_t* in = (const uint32_t*)args[2];
    const uint32_t in_len = (uint32_t)args[3];
    (void)result;
    ctx->ioctl_async_calls++;
    if (rt_is_di_read(ioctl, in, in_len)) {
        const uint32_t length = in[1];
        const uint32_t word_offset = in[2];
        const uint32_t lo = ctx->di_read_bytes_lo + length;
        if (lo < ctx->di_read_bytes_lo) ctx->di_read_bytes_hi++;
        ctx->di_read_bytes_lo = lo;
        ctx->di_reads++;
        ctx->di_fd = fd;
        ctx->last_di_word_offset = word_offset;
        ctx->last_di_length = length;
        if (ctx->flags & RT_FLAG_GECKO) {
            /* "R<word offset>:<length>\n", hex, no literals. */
            rt_gecko_putc(ctx, 'R');
            rt_gecko_hex(ctx, word_offset);
            rt_gecko_putc(ctx, ':');
            rt_gecko_hex(ctx, length);
            rt_gecko_putc(ctx, '\n');
        }
    }
    return 0;
}
