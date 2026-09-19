/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Resident runtime, C part. Rules that keep this position-independent and
 * safe to run inside the game's IOS_IoctlAsync and IPC callbacks (possibly
 * from an interrupt handler): no globals (all state lives in the context
 * the trampoline hands us), no string literals or other address-taken
 * data (they would need absolute relocations), no library calls, no
 * allocation, no blocking waits without a bound. The build links the blob
 * at two different addresses and checks the binaries are identical.
 */
#include "rt_hook.h"
#include "rtable.h"

#define RT_DI_READ 0x71u
#define RT_DI_SUCCESS 1

int rt_is_di_read(uint32_t ioctl, const uint32_t* in, uint32_t in_len) {
    /* DVDLowRead: ioctl 0x71 with a 0x20-byte command block whose first
     * word repeats the command number in its top byte (wiibrew /dev/di). */
    return ioctl == RT_DI_READ && in_len == 0x20 && in != 0 && (in[0] >> 24) == RT_DI_READ;
}

uint32_t rt_checksum(const uint8_t* bytes, uint32_t length) {
    uint32_t h = 0;
    uint32_t i;
    for (i = 0; i < length; ++i) h = h * 31u + bytes[i];
    return h;
}

/* --- console-only pieces: EXI/USB Gecko and cache maintenance ----------- */
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
    if (!(ctx->flags & RT_FLAG_GECKO)) return 0;
    for (tries = 0; tries < 100u; ++tries) {
        if (!rt_gecko_command(ctx->gecko_channel, 0xC000u, &reply)) break;
        if (reply & 0x0400u) {
            if (!rt_gecko_command(ctx->gecko_channel, 0xB000u | ((ch & 0xFFu) << 4), &reply)) break;
            if (reply & 0x0400u) return 1;
        }
    }
    /* Nobody is taking the bytes (no adapter, or a stalled host): stop
     * spending interrupt-off time on it. */
    if (++ctx->gecko_failures >= RT_GECKO_MAX_FAILURES) ctx->flags &= ~RT_FLAG_GECKO;
    return 0;
}

/* Writes the CPU's copy of [address, address + length) back to RAM so
 * DMA readers (GX, IOS) see what the game will see through the cache. */
static void rt_flush_range(uintptr_t address, uint32_t length) {
    uintptr_t line = address & ~(uintptr_t)31u;
    const uintptr_t end = address + length;
    while (line < end) {
        __asm__ volatile("dcbf 0, %0" : : "r"(line) : "memory");
        line += 32u;
    }
    __asm__ volatile("sync" : : : "memory");
}

/* External interrupts off/on around the claim of a pending record: the
 * hook runs on game threads and from the IPC interrupt handler (the DVD
 * driver issues its next command from the completion callback), so the
 * claim must not be interleaved. MSR[EE] is 0x8000; mtmsr is what the
 * SDK's OSDisableInterrupts uses too. */
static uint32_t rt_interrupts_off(void) {
    uint32_t msr;
    __asm__ volatile("mfmsr %0" : "=r"(msr));
    __asm__ volatile("mtmsr %0" : : "r"(msr & ~0x8000u) : "memory");
    return msr;
}
static void rt_interrupts_restore(uint32_t msr) {
    __asm__ volatile("mtmsr %0" : : "r"(msr) : "memory");
}
#else
static int rt_gecko_putc(struct rt_context* ctx, uint32_t ch) {
    (void)ch;
    return (ctx->flags & RT_FLAG_GECKO) != 0;
}
static void rt_flush_range(uintptr_t address, uint32_t length) {
    (void)address;
    (void)length;
}
static uint32_t rt_interrupts_off(void) {
    return 0;
}
static void rt_interrupts_restore(uint32_t msr) {
    (void)msr;
}
#endif

static void rt_gecko_hex(struct rt_context* ctx, uint32_t value) {
    int shift;
    for (shift = 28; shift >= 0; shift -= 4) {
        const uint32_t digit = (value >> shift) & 0xFu;
        rt_gecko_putc(ctx, digit < 10u ? '0' + digit : 'a' + digit - 10u);
    }
}

/* Own copies: the blob links against nothing. */
static void rt_copy(uint8_t* dst, const uint8_t* src, uint32_t n) {
    while (n--) *dst++ = *src++;
}
static void rt_zero(uint8_t* dst, uint32_t n) {
    while (n--) *dst++ = 0;
}

/* Splits a read against the table into `runs`. Returns the number of runs,
 * or -1 when the read must pass through untouched (no table, too many
 * runs, error). */
static int rt_split(const struct rt_context* ctx, uint32_t word_offset, uint32_t length, rt_run* runs,
                    int* touched) {
    uint32_t count = 0;
    uint32_t i;
    *touched = 0;
    if (ctx->table == 0) return -1;
    if (rt_lookup((const rt_header*)(uintptr_t)ctx->table, (uint64_t)word_offset << 2, length, runs, RT_MAX_RUNS,
                  &count) != RT_OK) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        if (runs[i].kind != RT_KIND_PASSTHROUGH) *touched = 1;
    }
    return (int)count;
}

/* Takes a free pending record, or returns 0. */
static struct rt_pending* rt_claim_pending(struct rt_context* ctx) {
    struct rt_pending* rec = 0;
    const uint32_t msr = rt_interrupts_off();
    uint32_t i;
    for (i = 0; i < RT_MAX_PENDING; ++i) {
        if (!ctx->pending[i].in_use) {
            rec = &ctx->pending[i];
            rec->in_use = 1;
            break;
        }
    }
    rt_interrupts_restore(msr);
    return rec;
}

int rt_on_ioctl_async(struct rt_context* ctx, uintptr_t* args, uint32_t* result) {
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
        if (ctx->table != 0) {
            const int in_window = ctx->virtual_start_words != 0 && word_offset >= ctx->virtual_start_words;
            struct rt_pending* rec = rt_claim_pending(ctx);
            if (rec == 0) {
                ctx->pending_overflow++;
            } else {
                int touched = 0;
                const int count = rt_split(ctx, word_offset, length, rec->runs, &touched);
                if (count < 0) {
                    ctx->run_overflow++; /* passes through; a virtual read then fails at the drive, honestly */
                    rec->in_use = 0;
                } else if (!touched && !in_window) {
                    rec->in_use = 0; /* nothing of ours in this read */
                } else {
                    rec->run_count = (uint32_t)count;
                    rec->callback = (uint32_t)args[6];
                    rec->user_data = (uint32_t)args[7];
                    rec->out = (uint32_t)args[4];
                    rec->length = length;
                    rec->word_offset = word_offset;
                    rec->is_virtual = (uint32_t)in_window;
                    if (in_window) {
                        /* The drive must never see the virtual offset: fetch the
                         * same length from the partition start instead (always
                         * readable) and replace every byte on completion. The
                         * command block is the game's; flush our change so the
                         * DMA to IOS carries it whatever the SDK flushed before. */
                        uint32_t* command = (uint32_t*)args[2];
                        command[2] = 0;
                        rt_flush_range((uintptr_t)command, 0x20);
                        ctx->virtual_reads++;
                    }
                    args[6] = (uintptr_t)ctx->complete_entry;
                    args[7] = (uintptr_t)rec;
                    ctx->redirected_reads++;
                }
            }
        }
    }
    return 0;
}

void rt_on_di_complete(struct rt_context* ctx, int32_t result, struct rt_pending* record, uintptr_t* callback,
                       uintptr_t* user_data) {
    ctx->completions++;
    *callback = (uintptr_t)record->callback;
    *user_data = (uintptr_t)record->user_data;
    if (result == RT_DI_SUCCESS) {
        uint8_t* out = (uint8_t*)(uintptr_t)record->out;
        const uint64_t base = (uint64_t)record->word_offset << 2;
        uint32_t checksum = 0;
        uint32_t i;
        for (i = 0; i < record->run_count; ++i) {
            const rt_run* run = &record->runs[i];
            uint8_t* dst = out + (uint32_t)(run->vstart - base);
            const uint32_t n = (uint32_t)run->length;
            uint32_t k;
            if (run->kind == RT_KIND_MEM) {
                rt_copy(dst, (const uint8_t*)(uintptr_t)run->source, n);
            } else if (run->kind == RT_KIND_ZERO || (run->kind == RT_KIND_PASSTHROUGH && record->is_virtual)) {
                rt_zero(dst, n); /* nothing on the disc belongs in a virtual gap */
            } else {
                continue; /* PASSTHROUGH: the disc already filled it; SD/DISC: E4 */
            }
            for (k = 0; k < n; ++k) checksum = checksum * 31u + dst[k]; /* while the lines are still ours */
            rt_flush_range((uintptr_t)dst, n);
        }
        ctx->last_checksum = checksum;
        if (ctx->flags & RT_FLAG_GECKO) {
            /* "M<word offset>:<length>:<checksum of the redirected bytes>\n" */
            rt_gecko_putc(ctx, 'M');
            rt_gecko_hex(ctx, record->word_offset);
            rt_gecko_putc(ctx, ':');
            rt_gecko_hex(ctx, record->length);
            rt_gecko_putc(ctx, ':');
            rt_gecko_hex(ctx, checksum);
            rt_gecko_putc(ctx, '\n');
        }
    }
    record->in_use = 0;
}
