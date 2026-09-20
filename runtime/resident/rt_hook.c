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

/* /dev/sdio/slot0 (wiibrew, libogc wiisd.c) */
#define RT_SDIO_SENDCMD 7u
#define RT_SD_CMD_READMULTIBLOCK 0x12u
#define RT_SD_CMD_WRITEMULTIBLOCK 0x19u
#define RT_SD_CMDTYPE_AC 3u
#define RT_SD_RESPONSE_R1 1u

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

static void rt_zero_bytes(uint8_t* dst, uint32_t bytes) {
    while (bytes--) *dst++ = 0;
}

int rt_build_fs_ipc(uint32_t entry_index, const uintptr_t* args, struct rtfs_ipc* out) {
    const uint32_t command = entry_index % RT_IPC_COMMANDS + 1u;
    const int async = entry_index < RT_IPC_COMMANDS;
    if (out == 0) return 0;
    rt_zero_bytes((uint8_t*)out, (uint32_t)sizeof(*out));
    if (args == 0 || entry_index >= RT_IPC_ENTRIES) return 0;
    out->command = command;
    if (command == RTFS_CMD_OPEN) {
        out->args.open.path = (uint32_t)args[0];
        out->args.open.mode = (uint32_t)args[1];
        if (async) { out->callback = (uint32_t)args[2]; out->user_data = (uint32_t)args[3]; }
        return 1;
    }
    out->fd = (int32_t)(uint32_t)args[0];
    if (command == RTFS_CMD_CLOSE) {
        if (async) { out->callback = (uint32_t)args[1]; out->user_data = (uint32_t)args[2]; }
        return 1;
    }
    if (command == RTFS_CMD_READ || command == RTFS_CMD_WRITE) {
        out->args.readwrite.data = (uint32_t)args[1];
        out->args.readwrite.length = (uint32_t)args[2];
        if (async) { out->callback = (uint32_t)args[3]; out->user_data = (uint32_t)args[4]; }
        return 1;
    }
    if (command == RTFS_CMD_SEEK) {
        out->args.seek.where = (int32_t)(uint32_t)args[1];
        out->args.seek.whence = (uint32_t)args[2];
        if (async) { out->callback = (uint32_t)args[3]; out->user_data = (uint32_t)args[4]; }
        return 1;
    }
    if (command == RTFS_CMD_IOCTL) {
        out->args.ioctl.request = (uint32_t)args[1];
        out->args.ioctl.in = (uint32_t)args[2];
        out->args.ioctl.in_len = (uint32_t)args[3];
        out->args.ioctl.out = (uint32_t)args[4];
        out->args.ioctl.out_len = (uint32_t)args[5];
        if (async) { out->callback = (uint32_t)args[6]; out->user_data = (uint32_t)args[7]; }
        return 1;
    }
    out->args.ioctlv.request = (uint32_t)args[1];
    out->args.ioctlv.in_count = (uint32_t)args[2];
    out->args.ioctlv.out_count = (uint32_t)args[3];
    out->args.ioctlv.vectors = (uint32_t)args[4];
    if (async) { out->callback = (uint32_t)args[5]; out->user_data = (uint32_t)args[6]; }
    return 1;
}

/* --- console-only pieces: EXI/USB Gecko, caches, interrupts, IPC -------- */
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

/* Writes the CPU's copy of [address, address + length) back to RAM and
 * drops it from the cache, so DMA readers (GX, IOS) see what the game
 * will see, and DMA writers (IOS) are not shadowed by stale lines. */
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

/* The game's IOS_IoctlvAsync (SDK: fd, ioctl, in count, out count,
 * vectors, callback, user data), reached through the context, so the
 * call is a register-indirect one and the blob stays relocatable. */
typedef int32_t (*rt_ioctlv_async_fn)(uint32_t fd, uint32_t ioctl, uint32_t in_count, uint32_t out_count,
                                       struct rt_ioctlv* vec, uint32_t callback, struct rt_pending* record);
static int32_t rt_ioctlv_async(struct rt_context* ctx, struct rt_pending* record) {
    rt_ioctlv_async_fn fn = (rt_ioctlv_async_fn)(uintptr_t)ctx->ioctlv_async;
    if (fn == 0) return -1;
    return fn(ctx->sdio_fd, RT_SDIO_SENDCMD, 2, 1, record->vec, ctx->complete_entry, record);
}

/* The unhooked IOS_IoctlAsync (the replay slot runs the displaced
 * instructions and continues in the original), for DVDLowReads of our
 * own on the game's /dev/di fd. */
typedef int32_t (*rt_ioctl_async_fn)(uint32_t fd, uint32_t ioctl, uint32_t* in, uint32_t in_len, uint32_t out,
                                     uint32_t out_len, uint32_t callback, struct rt_pending* record);
static int32_t rt_di_read_async(struct rt_context* ctx, struct rt_pending* record, uint32_t length) {
    rt_ioctl_async_fn fn = (rt_ioctl_async_fn)(uintptr_t)ctx->di_read_entry;
    if (fn == 0) return -1;
    return fn(ctx->di_fd, RT_DI_READ, record->di_command, 0x20, record->bounce, length, ctx->complete_entry, record);
}

/* The savegame path's IOS calls. The transfer request is built in the
 * state's own blocks (rt_fs_build_sendcmd); synchronous ones go through
 * the game's synchronous IOS_Ioctlv, asynchronous ones through its
 * IOS_IoctlvAsync with the FS completion entry and the given record. */
typedef int32_t (*rt_ioctlv_sync_fn)(uint32_t fd, uint32_t ioctl, uint32_t in_count, uint32_t out_count,
                                     struct rt_ioctlv* vec);
typedef int32_t (*rt_open_sync_fn)(const char* path, uint32_t mode);
static int32_t rt_fs_sendcmd_call(struct rt_context* ctx, struct rt_fs_state* st, int async) {
    if (ctx->sdio_fd == 0xFFFFFFFFu) return RTFAT_EIO;
    if (async) {
        rt_ioctlv_async_fn fn = (rt_ioctlv_async_fn)(uintptr_t)ctx->ioctlv_async;
        if (fn == 0) return RTFAT_EIO;
        return fn(ctx->sdio_fd, RT_SDIO_SENDCMD, 2, 1, st->vec, st->complete_fs, (struct rt_pending*)&st->pend);
    } else {
        rt_ioctlv_sync_fn fn = (rt_ioctlv_sync_fn)(uintptr_t)st->ioctlv_sync;
        if (fn == 0) return RTFAT_EIO;
        return fn(ctx->sdio_fd, RT_SDIO_SENDCMD, 2, 1, st->vec);
    }
}
/* The null round trip: SD GETSTATUS through the unhooked IOS_IoctlAsync,
 * its 4-byte answer landing in the slot's own line. */
static int32_t rt_fs_getstatus_async(struct rt_context* ctx, struct rt_fs_state* st, struct rt_fs_pend* slot) {
    rt_ioctl_async_fn fn = (rt_ioctl_async_fn)(uintptr_t)ctx->di_read_entry;
    if (fn == 0 || ctx->sdio_fd == 0xFFFFFFFFu) return -1;
    rt_flush_range((uintptr_t)slot->status, sizeof(slot->status));
    return fn(ctx->sdio_fd, RT_SDIO_GETSTATUS, 0, 0, (uint32_t)(uintptr_t)slot->status, 4, st->complete_fs,
              (struct rt_pending*)slot);
}
static int32_t rt_fs_open_sync(struct rt_fs_state* st, uint32_t path, uint32_t mode) {
    rt_open_sync_fn fn = (rt_open_sync_fn)(uintptr_t)st->open_sync;
    return fn((const char*)(uintptr_t)path, mode);
}
typedef int32_t (*rt_close_sync_fn)(int32_t fd);
typedef int32_t (*rt_read_sync_fn)(int32_t fd, void* buffer, uint32_t length);
typedef int32_t (*rt_ioctl_sync_fn)(int32_t fd, uint32_t request, const void* in, uint32_t in_len, void* out,
                                    uint32_t out_len);
static int32_t rt_fs_close_sync(struct rt_fs_state* st, int32_t fd) {
    return ((rt_close_sync_fn)(uintptr_t)st->close_sync)(fd);
}
static int32_t rt_fs_read_sync(struct rt_fs_state* st, int32_t fd, uint32_t buffer, uint32_t length) {
    return ((rt_read_sync_fn)(uintptr_t)st->read_sync)(fd, (void*)(uintptr_t)buffer, length);
}
static int32_t rt_fs_ioctl_sync(struct rt_fs_state* st, int32_t fd, uint32_t request, uint32_t in, uint32_t in_len,
                                uint32_t out, uint32_t out_len) {
    return ((rt_ioctl_sync_fn)(uintptr_t)st->ioctl_sync)(fd, request, (const void*)(uintptr_t)in, in_len,
                                                           (void*)(uintptr_t)out, out_len);
}
static int32_t rt_fs_ioctlv_sync(struct rt_fs_state* st, int32_t fd, uint32_t request, uint32_t in_count,
                                 uint32_t out_count, struct rt_ioctlv* vec) {
    return ((rt_ioctlv_sync_fn)(uintptr_t)st->ioctlv_sync)((uint32_t)fd, request, in_count, out_count, vec);
}
/* On a thread (external interrupts on) rather than inside an interrupt
 * handler: synchronous IOS calls may sleep here. MSR[EE] is 0x8000. */
static int rt_fs_in_thread(void) {
    uint32_t msr;
    __asm__ volatile("mfmsr %0" : "=r"(msr));
    return (msr & 0x8000u) != 0;
}
static uint32_t rt_fs_ticks(void) {
    uint32_t tb;
    __asm__ volatile("mftb %0" : "=r"(tb) : : "memory");
    return tb;
}
static void rt_fs_wait_tick(struct rt_context* ctx) {
    (void)ctx;
}
static void rt_invoke_game(uint32_t cb, int32_t result, uint32_t user_data) {
    if (cb != 0) ((rt_game_callback_fn)(uintptr_t)cb)(result, user_data);
}
#else
int32_t (*rt_host_ioctlv_async)(uint32_t fd, uint32_t ioctl, uint32_t in_count, uint32_t out_count,
                                struct rt_ioctlv* vec, uint32_t callback, struct rt_pending* record) = 0;
int32_t (*rt_host_ioctl_async)(uint32_t fd, uint32_t ioctl, uint32_t* in, uint32_t in_len, uint32_t out,
                                uint32_t out_len, uint32_t callback, struct rt_pending* record) = 0;
int32_t (*rt_host_fs_transfer)(uint32_t lba, uint32_t count, uint32_t buffer, uint32_t is_write) = 0;
int32_t (*rt_host_fs_issue)(uint32_t lba, uint32_t count, uint32_t buffer, uint32_t is_write, void* tag) = 0;
int32_t (*rt_host_fs_defer)(void* tag) = 0;
void (*rt_host_fs_wait)(struct rt_context* ctx) = 0;
int32_t (*rt_host_fs_open_sync)(const char* path, uint32_t mode) = 0;
int32_t (*rt_host_fs_close_sync)(int32_t fd) = 0;
int32_t (*rt_host_fs_read_sync)(int32_t fd, uint32_t buffer, uint32_t length) = 0;
int32_t (*rt_host_fs_ioctl_sync)(int32_t fd, uint32_t request, uint32_t in, uint32_t in_len, uint32_t out,
                                  uint32_t out_len) = 0;
int32_t (*rt_host_fs_ioctlv_sync)(int32_t fd, uint32_t request, uint32_t in_count, uint32_t out_count,
                                   struct rt_ioctlv* vec) = 0;
int rt_host_fs_in_thread = 1;
void (*rt_host_game_callback)(uint32_t cb, int32_t result, uint32_t user_data) = 0;
static int32_t rt_fs_ioctlv_sync(struct rt_fs_state* st, int32_t fd, uint32_t request, uint32_t in_count,
                                 uint32_t out_count, struct rt_ioctlv* vec) {
    (void)st;
    return rt_host_fs_ioctlv_sync == 0 ? -1 : rt_host_fs_ioctlv_sync(fd, request, in_count, out_count, vec);
}
static int32_t rt_fs_close_sync(struct rt_fs_state* st, int32_t fd) {
    (void)st;
    return rt_host_fs_close_sync == 0 ? -1 : rt_host_fs_close_sync(fd);
}
static int32_t rt_fs_read_sync(struct rt_fs_state* st, int32_t fd, uint32_t buffer, uint32_t length) {
    (void)st;
    return rt_host_fs_read_sync == 0 ? -1 : rt_host_fs_read_sync(fd, buffer, length);
}
static int32_t rt_fs_ioctl_sync(struct rt_fs_state* st, int32_t fd, uint32_t request, uint32_t in, uint32_t in_len,
                                uint32_t out, uint32_t out_len) {
    (void)st;
    return rt_host_fs_ioctl_sync == 0 ? -1 : rt_host_fs_ioctl_sync(fd, request, in, in_len, out, out_len);
}
static int rt_fs_in_thread(void) {
    return rt_host_fs_in_thread;
}
static int32_t rt_fs_sendcmd_call(struct rt_context* ctx, struct rt_fs_state* st, int async) {
    const struct rtfat_op* op = &st->req.fat;
    (void)ctx;
    if (async) {
        if (rt_host_fs_issue == 0) return -1;
        return rt_host_fs_issue(op->io_lba, op->io_count, op->io_buffer, op->io_write, &st->pend);
    }
    if (rt_host_fs_transfer == 0) return -1;
    return rt_host_fs_transfer(op->io_lba, op->io_count, op->io_buffer, op->io_write);
}
static int32_t rt_fs_getstatus_async(struct rt_context* ctx, struct rt_fs_state* st, struct rt_fs_pend* slot) {
    (void)ctx;
    (void)st;
    return rt_host_fs_defer == 0 ? -1 : rt_host_fs_defer(slot);
}
static int32_t rt_fs_open_sync(struct rt_fs_state* st, uint32_t path, uint32_t mode) {
    (void)st;
    return rt_host_fs_open_sync == 0 ? -1 : rt_host_fs_open_sync((const char*)(uintptr_t)path, mode);
}
/* Eight waits, then the timeout. */
static uint32_t rt_fs_ticks(void) {
    static uint32_t ticks = 0;
    ticks += RT_FS_WAIT_TICKS / 8u + 1u;
    return ticks;
}
static void rt_fs_wait_tick(struct rt_context* ctx) {
    if (rt_host_fs_wait != 0) rt_host_fs_wait(ctx);
}
static void rt_invoke_game(uint32_t cb, int32_t result, uint32_t user_data) {
    if (cb != 0 && rt_host_game_callback != 0) rt_host_game_callback(cb, result, user_data);
}
static int32_t rt_di_read_async(struct rt_context* ctx, struct rt_pending* record, uint32_t length) {
    if (ctx->di_read_entry == 0 || rt_host_ioctl_async == 0) return -1;
    return rt_host_ioctl_async(ctx->di_fd, RT_DI_READ, record->di_command, 0x20, record->bounce, length,
                               ctx->complete_entry, record);
}
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
static int32_t rt_ioctlv_async(struct rt_context* ctx, struct rt_pending* record) {
    if (ctx->ioctlv_async == 0 || rt_host_ioctlv_async == 0) return -1;
    return rt_host_ioctlv_async(ctx->sdio_fd, RT_SDIO_SENDCMD, 2, 1, record->vec, ctx->complete_entry, record);
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
#ifdef RT_TARGET_PPC
/* The compiler lowers some struct copies in the FAT engine to memcpy even
 * with -fno-builtin, so the blob provides it. PPC-only: host builds use
 * libc, where a second definition would collide at the link. */
void* memcpy(void* dst, const void* src, __SIZE_TYPE__ n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dst;
}
#endif
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
                    rec->phase = RT_PHASE_DISC;
                    rec->run_index = 0;
                    rec->run_done = 0;
                    rec->chunk_bytes = 0;
                    rec->chunk_skip = 0;
                    rec->di_result = 0;
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

/* --- savegame requests --------------------------------------------------- */
/* The records, their tags and the serialization rules: rt_fs_state in
 * rt_hook.h. Everything here runs on game threads and inside the IPC
 * interrupt alike. */

#define RT_FS_ADVANCE_DONE 0
#define RT_FS_ADVANCE_ISSUED 1
#define RT_FS_ADMIT_BEGUN 1
#define RT_FS_ADMIT_QUEUED 2
#define RT_FS_ADMIT_FULL 3

static struct rt_fs_state* rt_fs_of(const struct rt_context* ctx) {
    if ((ctx->flags & RT_FLAG_FS) == 0 || ctx->fs_state == 0) return 0;
    return (struct rt_fs_state*)(uintptr_t)ctx->fs_state;
}

/* Installs the FS completion entry and a record tag as an async call's
 * callback pair, in the register images the SDK form keeps them in
 * (rt_build_fs_ipc); every other register is the game's. */
static void rt_fs_swap_callback(uintptr_t* args, uint32_t command, uintptr_t cb, uintptr_t tag) {
    if (command == RTFS_CMD_OPEN) {
        args[2] = cb;
        args[3] = tag;
    } else if (command == RTFS_CMD_CLOSE) {
        args[1] = cb;
        args[2] = tag;
    } else if (command == RTFS_CMD_READ || command == RTFS_CMD_WRITE || command == RTFS_CMD_SEEK) {
        args[3] = cb;
        args[4] = tag;
    } else if (command == RTFS_CMD_IOCTL) {
        args[6] = cb;
        args[7] = tag;
    } else {
        args[5] = cb;
        args[6] = tag;
    }
}

/* "F<entry>:<fd or path>[/<ioctl>]:<result>\n" over the Gecko for an
 * answered call ('P' as the result of one whose transfers are in flight)
 * and "C:<result>\n" when such a one completes. */
static void rt_fs_report(struct rt_context* ctx, uint32_t entry_index, const struct rtfs_ipc* ipc, int32_t result,
                         int pending) {
    if (!(ctx->flags & RT_FLAG_GECKO)) return;
    rt_gecko_putc(ctx, 'F');
    rt_gecko_hex(ctx, entry_index);
    rt_gecko_putc(ctx, ':');
    if (ipc->command == RTFS_CMD_OPEN) {
        const char* p = (const char*)(uintptr_t)ipc->args.open.path;
        uint32_t i;
        for (i = 0; p != 0 && i < RTFS_PATH_BYTES && p[i] != 0; ++i) rt_gecko_putc(ctx, (uint32_t)(uint8_t)p[i]);
    } else {
        rt_gecko_hex(ctx, (uint32_t)ipc->fd);
        if (ipc->command == RTFS_CMD_IOCTL) {
            /* "/<request>,<in len>,<out len>[,<text at the start of the in
             * buffer>[,<text at its 64th byte>]]": the ISFS paths. */
            const char* p = (const char*)(uintptr_t)ipc->args.ioctl.in;
            const uint32_t in_len = ipc->args.ioctl.in_len;
            uint32_t at;
            rt_gecko_putc(ctx, '/');
            rt_gecko_hex(ctx, ipc->args.ioctl.request);
            rt_gecko_putc(ctx, ',');
            rt_gecko_hex(ctx, in_len);
            rt_gecko_putc(ctx, ',');
            rt_gecko_hex(ctx, ipc->args.ioctl.out_len);
            for (at = 0; p != 0 && at + RTFS_PATH_BYTES <= in_len && at < 2 * RTFS_PATH_BYTES; at += RTFS_PATH_BYTES) {
                uint32_t i;
                rt_gecko_putc(ctx, ',');
                for (i = 0; i < RTFS_PATH_BYTES && p[at + i] >= 0x20 && p[at + i] < 0x7F; ++i) {
                    rt_gecko_putc(ctx, (uint32_t)(uint8_t)p[at + i]);
                }
            }
        } else if (ipc->command == RTFS_CMD_IOCTLV) {
            /* "/<request>,<in count>,<out count>,<len of each vector>[,<first vector as text>]" */
            const struct rtfs_iovec* v = (const struct rtfs_iovec*)(uintptr_t)ipc->args.ioctlv.vectors;
            const uint32_t n = ipc->args.ioctlv.in_count + ipc->args.ioctlv.out_count;
            uint32_t i;
            rt_gecko_putc(ctx, '/');
            rt_gecko_hex(ctx, ipc->args.ioctlv.request);
            rt_gecko_putc(ctx, ',');
            rt_gecko_hex(ctx, ipc->args.ioctlv.in_count);
            rt_gecko_putc(ctx, ',');
            rt_gecko_hex(ctx, ipc->args.ioctlv.out_count);
            for (i = 0; v != 0 && i < n && i < 4; ++i) {
                rt_gecko_putc(ctx, ',');
                rt_gecko_hex(ctx, v[i].len);
            }
            if (v != 0 && n != 0 && v[0].data != 0) {
                const char* p = (const char*)(uintptr_t)v[0].data;
                rt_gecko_putc(ctx, ',');
                for (i = 0; i < RTFS_PATH_BYTES && i < v[0].len && p[i] >= 0x20 && p[i] < 0x7F; ++i) {
                    rt_gecko_putc(ctx, (uint32_t)(uint8_t)p[i]);
                }
            }
        }
    }
    rt_gecko_putc(ctx, ':');
    if (pending) rt_gecko_putc(ctx, 'P');
    else rt_gecko_hex(ctx, (uint32_t)result);
    rt_gecko_putc(ctx, '\n');
}

static void rt_fs_report_done(struct rt_context* ctx, int32_t result) {
    if (!(ctx->flags & RT_FLAG_GECKO)) return;
    rt_gecko_putc(ctx, 'C');
    rt_gecko_putc(ctx, ':');
    rt_gecko_hex(ctx, (uint32_t)result);
    rt_gecko_putc(ctx, '\n');
}

/* The SENDCMD for the engine's pending transfer (CMD18 read, CMD25 write:
 * wiibrew /dev/sdio, libogc wiisd.c), in the state's own lines, flushed
 * for IOS along with the buffer the card DMAs from or into. */
static void rt_fs_build_sendcmd(struct rt_context* ctx, struct rt_fs_state* st) {
    const struct rtfat_op* op = &st->req.fat;
    struct rt_sdio_request* rq = &st->request;
    const uint32_t bytes = op->io_count * RT_SECTOR_BYTES;
    rq->cmd = op->io_write ? RT_SD_CMD_WRITEMULTIBLOCK : RT_SD_CMD_READMULTIBLOCK;
    rq->cmd_type = RT_SD_CMDTYPE_AC;
    rq->rsp_type = RT_SD_RESPONSE_R1;
    rq->arg = ctx->sdio_sdhc ? op->io_lba : op->io_lba * RT_SECTOR_BYTES;
    rq->blk_cnt = op->io_count;
    rq->blk_size = RT_SECTOR_BYTES;
    rq->dma_addr = op->io_buffer;
    rq->isdma = 1;
    rq->pad0 = 0;
    st->vec[0].data = (uint32_t)(uintptr_t)rq;
    st->vec[0].len = sizeof(*rq);
    st->vec[1].data = op->io_buffer;
    st->vec[1].len = bytes;
    st->vec[2].data = (uint32_t)(uintptr_t)st->response;
    st->vec[2].len = 16;
    rt_flush_range((uintptr_t)rq, sizeof(*rq));
    rt_flush_range((uintptr_t)st->vec, sizeof(st->vec));
    rt_flush_range((uintptr_t)op->io_buffer, bytes);
}

/* Performs the engine's pending transfer on the caller's thread and
 * records its status. */
static void rt_fs_transfer_sync(struct rt_context* ctx, struct rt_fs_state* st) {
    int32_t r;
    rt_fs_build_sendcmd(ctx, st);
    r = rt_fs_sendcmd_call(ctx, st, 0);
    st->transfers++;
    if (r < 0) st->failures++;
    st->req.fat.io_status = r < 0 ? r : 0;
}

/* Issues the engine's pending transfer with the FILE record as its tag:
 * 1 when in flight (the completion continues), 0 when refused (the
 * status then fails the request on its next step). */
static int rt_fs_issue_async(struct rt_context* ctx, struct rt_fs_state* st) {
    int32_t r;
    rt_fs_build_sendcmd(ctx, st);
    r = rt_fs_sendcmd_call(ctx, st, 1);
    st->transfers++;
    if (r < 0) {
        st->failures++;
        st->req.fat.io_status = r;
        return 0;
    }
    return 1;
}

/* Runs the request in flight until it is complete (DONE: result in
 * req.result, engine released) or a transfer is in flight (ISSUED).
 * An engine step that neither completes nor asks for I/O, or one that
 * never ends, marks the state dead: the card image may be half-updated,
 * so every later request answers -114 rather than touching it or NAND. */
static int rt_fs_advance(struct rt_context* ctx, struct rt_fs_state* st, int sync) {
    struct rtfs_request* req = &st->req;
    uint32_t guard;
    for (guard = 0; guard < ((uint32_t)1 << 20); ++guard) {
        const int step = rtfs_step(&st->fs, req);
        if (step == RTFAT_DONE) {
            /* Bytes read into the game's buffer went through the cache;
             * write them back in case the game invalidates the buffer as
             * it would after a DMA. */
            if (req->action == RTFS_ACTION_READ && req->result > 0) {
                rt_flush_range((uintptr_t)req->fat.buffer, (uint32_t)req->result);
            }
            return RT_FS_ADVANCE_DONE;
        }
        if (step != RTFAT_IO) break;
        if (sync) rt_fs_transfer_sync(ctx, st);
        else if (rt_fs_issue_async(ctx, st)) return RT_FS_ADVANCE_ISSUED;
    }
    st->dead = 1;
    req->result = RTFAT_EIO;
    req->classification = RTFS_COMPLETE;
    req->active = 0;
    st->fs.busy = 0;
    return RT_FS_ADVANCE_DONE;
}

/* Hands `result` to the game's callback of an async call that completed
 * without I/O: through a null IOS round trip, so it runs from the IPC
 * interrupt after the call has returned, as every IOS completion does;
 * directly, as the last resort, when no round trip can be issued. */
static void rt_fs_deliver(struct rt_context* ctx, struct rt_fs_state* st, uint32_t cb, uint32_t ud, int32_t result) {
    struct rt_fs_pend* slot = 0;
    uint32_t msr;
    uint32_t i;
    if (cb == 0) return;
    msr = rt_interrupts_off();
    for (i = 0; i < RT_FS_DELIVERS && slot == 0; ++i) {
        if (!st->deliver[i].in_use) slot = &st->deliver[i];
    }
    if (slot != 0) {
        slot->in_use = 1;
        slot->kind = RT_FS_OP_DELIVER;
        slot->callback = cb;
        slot->user_data = ud;
        slot->result = result;
    }
    rt_interrupts_restore(msr);
    if (slot != 0) {
        if (rt_fs_getstatus_async(ctx, st, slot) >= 0) {
            st->deferred++;
            return;
        }
        slot->in_use = 0;
    }
    st->inline_deliveries++;
    rt_invoke_game(cb, result, ud);
}

/* Takes the engine for `ipc` when it is free with nothing queued ahead
 * (BEGUN: the request is begun, its classification in st->req), else
 * queues the arrival when allowed (QUEUED) or reports FULL. Interrupts
 * off: hooks run in both contexts. */
static int rt_fs_admit(struct rt_fs_state* st, uint32_t entry_index, const struct rtfs_ipc* ipc, int may_queue) {
    const uint32_t msr = rt_interrupts_off();
    int r = RT_FS_ADMIT_FULL;
    if (!st->fs.busy && st->queue_count == 0) {
        st->req.fat.bounce = (uint32_t)(uintptr_t)st->bounce;
        st->req.fat.bounce_bytes = RT_FS_BOUNCE_BYTES;
        rtfs_begin(&st->fs, &st->req, ipc);
        r = RT_FS_ADMIT_BEGUN;
    } else if (may_queue && st->queue_count < RT_FS_QUEUE) {
        struct rt_fs_queued* q = &st->queue[(st->queue_head + st->queue_count) % RT_FS_QUEUE];
        q->in_use = 1;
        q->entry_index = entry_index;
        q->ipc = *ipc;
        st->queue_count++;
        r = RT_FS_ADMIT_QUEUED;
    }
    rt_interrupts_restore(msr);
    return r;
}

/* rtfs_probe under interrupts off (the scratch record is shared by both
 * contexts); the classification and result come back in the arguments. */
static void rt_fs_probe(struct rt_fs_state* st, const struct rtfs_ipc* ipc, uint32_t* classification) {
    const uint32_t msr = rt_interrupts_off();
    rtfs_probe(&st->fs, &st->probe, ipc);
    *classification = st->probe.classification;
    rt_interrupts_restore(msr);
}

/* Starts the requests queued behind the engine once it is free: each
 * runs until its first transfer is in flight (the completion continues
 * it) or completes at once (its result deferred). */
static void rt_fs_start_queued(struct rt_context* ctx, struct rt_fs_state* st) {
    while (st->queue_count != 0 && !st->fs.busy) {
        struct rt_fs_queued q;
        const uint32_t msr = rt_interrupts_off();
        q = st->queue[st->queue_head];
        st->queue[st->queue_head].in_use = 0;
        st->queue_head = (st->queue_head + 1u) % RT_FS_QUEUE;
        st->queue_count--;
        if (!st->dead) {
            st->req.fat.bounce = (uint32_t)(uintptr_t)st->bounce;
            st->req.fat.bounce_bytes = RT_FS_BOUNCE_BYTES;
            rtfs_begin(&st->fs, &st->req, &q.ipc);
        }
        rt_interrupts_restore(msr);
        if (st->dead) {
            rt_fs_deliver(ctx, st, q.ipc.callback, q.ipc.user_data, RTFAT_EIO);
            continue;
        }
        if (st->req.classification == RTFS_PASS_THROUGH) {
            /* Ours when it arrived, not any more (the fd was forgotten
             * meanwhile): nothing can replay it now. */
            rt_fs_deliver(ctx, st, q.ipc.callback, q.ipc.user_data, RTFAT_EINVAL);
            continue;
        }
        if (st->req.classification == RTFS_NEEDS_IO) {
            st->pend.in_use = 1;
            st->pend.kind = RT_FS_OP_FILE;
            if (rt_fs_advance(ctx, st, 0) == RT_FS_ADVANCE_ISSUED) {
                rt_fs_report(ctx, q.entry_index, &q.ipc, 0, 1);
                return;
            }
            st->pend.in_use = 0;
        }
        rt_fs_report(ctx, q.entry_index, &q.ipc, st->req.result, 0);
        rt_fs_deliver(ctx, st, st->req.callback, st->req.user_data, st->req.result);
    }
}

/* Waits for the engine on the game's thread (interrupts on: the IPC
 * interrupt drives the requests ahead), bounded. 1 when it came free. */
static int rt_fs_wait(struct rt_context* ctx, struct rt_fs_state* st) {
    const uint32_t start = rt_fs_ticks();
    const volatile uint32_t* busy = &st->fs.busy;         /* changed by the IPC interrupt: */
    const volatile uint32_t* queued = &st->queue_count;   /* re-read every turn */
    st->waits++;
    while (*busy || *queued != 0) {
        rt_fs_wait_tick(ctx);
        if (rt_fs_ticks() - start > RT_FS_WAIT_TICKS) {
            st->wait_timeouts++;
            return 0;
        }
    }
    return 1;
}

/* Runs one of the runtime's own requests on the engine, on the game's
 * thread: waits for the engine, performs the transfers inline. */
static int32_t rt_fs_run_internal(struct rt_context* ctx, struct rt_fs_state* st, const struct rtfs_ipc* ipc) {
    if (st->dead) return RTFAT_EIO;
    while (rt_fs_admit(st, 0, ipc, 0) != RT_FS_ADMIT_BEGUN) {
        if (!rt_fs_wait(ctx, st)) return RTFAT_EIO;
    }
    if (st->req.classification == RTFS_PASS_THROUGH) return RTFAT_EINVAL;
    if (st->req.classification == RTFS_NEEDS_IO) rt_fs_advance(ctx, st, 1);
    return st->req.result;
}

/* Whether the game's synchronous functions an import or a clone needs
 * are known. */
static int rt_fs_has_sync_originals(const struct rt_fs_state* st) {
    return st->open_sync != 0 && st->close_sync != 0 && st->read_sync != 0 && st->ioctl_sync != 0;
}

/* Copies the NAND file `src` into the card file `dst` (replacing one of
 * that name), then deletes the source when `move` (an import) or leaves
 * it (a clone). `fs_fd` is a /dev/fs fd for the delete. Runs on the
 * game's thread; returns the ISFS result. */
static int32_t rt_fs_copy_in(struct rt_context* ctx, struct rt_fs_state* st, int32_t fs_fd, const char* src,
                             const char* dst, int move) {
    struct rtfs_ipc ipc;
    int32_t src_fd, fake_fd = -1, r;
    uint32_t size, done, i;
    src_fd = rt_fs_open_sync(st, (uint32_t)(uintptr_t)src, 1);
    if (src_fd < 0) return src_fd;
    rt_flush_range((uintptr_t)st->stats, sizeof(st->stats));
    r = rt_fs_ioctl_sync(st, src_fd, RTFS_IOCTL_GETFILESTATS, 0, 0, (uint32_t)(uintptr_t)st->stats, 8);
    if (r >= 0) {
        size = st->stats[0];
        /* The destination, replaced when it exists, created empty. */
        rt_zero_bytes((uint8_t*)&ipc, sizeof(ipc));
        ipc.command = RTFS_CMD_IOCTL;
        ipc.fd = fs_fd;
        ipc.args.ioctl.request = RTFS_IOCTL_DELETE;
        ipc.args.ioctl.in = (uint32_t)(uintptr_t)dst;
        ipc.args.ioctl.in_len = RTFS_PATH_BYTES;
        r = rt_fs_run_internal(ctx, st, &ipc);
        if (r == RTFAT_ENOENT) r = RTFAT_OK;
    }
    if (r >= 0) {
        rt_zero_bytes((uint8_t*)&st->attr, sizeof(st->attr));
        for (i = 0; i < RTFS_PATH_BYTES - 1 && dst[i] != 0; ++i) st->attr.filepath[i] = dst[i];
        st->attr.ownerperm = 3;
        st->attr.groupperm = 3;
        st->attr.otherperm = 3;
        ipc.args.ioctl.request = RTFS_IOCTL_CREATEFILE;
        ipc.args.ioctl.in = (uint32_t)(uintptr_t)&st->attr;
        ipc.args.ioctl.in_len = sizeof(st->attr);
        r = rt_fs_run_internal(ctx, st, &ipc);
    }
    if (r >= 0) {
        rt_zero_bytes((uint8_t*)&ipc, sizeof(ipc));
        ipc.command = RTFS_CMD_OPEN;
        ipc.args.open.path = (uint32_t)(uintptr_t)dst;
        ipc.args.open.mode = 2;
        r = fake_fd = rt_fs_run_internal(ctx, st, &ipc);
    }
    /* The bytes, a piece at a time: NAND into the import buffer (flushed
     * first, so no stale line of the previous piece shadows the DMA),
     * the buffer into the card. */
    for (done = 0; r >= 0 && done < size;) {
        uint32_t n = size - done;
        if (n > RT_FS_IMPORT_BYTES) n = RT_FS_IMPORT_BYTES;
        rt_flush_range((uintptr_t)st->import, n);
        r = rt_fs_read_sync(st, src_fd, (uint32_t)(uintptr_t)st->import, n);
        if (r >= 0 && r != (int32_t)n) r = RTFAT_EIO;
        if (r < 0) break;
        rt_zero_bytes((uint8_t*)&ipc, sizeof(ipc));
        ipc.command = RTFS_CMD_WRITE;
        ipc.fd = fake_fd;
        ipc.args.readwrite.data = (uint32_t)(uintptr_t)st->import;
        ipc.args.readwrite.length = n;
        r = rt_fs_run_internal(ctx, st, &ipc);
        if (r >= 0 && r != (int32_t)n) r = RTFAT_ENOSPC;
        done += n;
    }
    if (fake_fd >= 0) {
        rt_zero_bytes((uint8_t*)&ipc, sizeof(ipc));
        ipc.command = RTFS_CMD_CLOSE;
        ipc.fd = fake_fd;
        rt_fs_run_internal(ctx, st, &ipc);
    }
    rt_fs_close_sync(st, src_fd);
    if (r < 0) {
        /* Nothing half-moved: the destination goes, the source stays. */
        if (fake_fd >= 0) {
            rt_zero_bytes((uint8_t*)&ipc, sizeof(ipc));
            ipc.command = RTFS_CMD_IOCTL;
            ipc.fd = fs_fd;
            ipc.args.ioctl.request = RTFS_IOCTL_DELETE;
            ipc.args.ioctl.in = (uint32_t)(uintptr_t)dst;
            ipc.args.ioctl.in_len = RTFS_PATH_BYTES;
            rt_fs_run_internal(ctx, st, &ipc);
        }
        return r;
    }
    /* The move's other half. A failure here leaves a stray NAND file,
     * nothing worse. */
    if (move) rt_fs_ioctl_sync(st, fs_fd, RTFS_IOCTL_DELETE, (uint32_t)(uintptr_t)src, RTFS_PATH_BYTES, 0, 0);
    return RTFAT_OK;
}

/* A rename from outside the redirected directory into it (rt_hook.h):
 * the NAND file `src` becomes the card file `dst`, then leaves NAND.
 * `fs_fd` is the game's /dev/fs fd the rename came on. */
static int32_t rt_fs_import(struct rt_context* ctx, struct rt_fs_state* st, int32_t fs_fd, const char* src,
                            const char* dst) {
    int32_t r;
    if (!rt_fs_has_sync_originals(st)) {
        st->import_refused++;
        return RTFAT_EACCESS;
    }
    st->imports++;
    r = rt_fs_copy_in(ctx, st, fs_fd, src, dst, 1);
    if (r < 0) st->import_failures++;
    return r;
}

/* "K:<path>:<result>\n" over the Gecko for each file a clone copies. */
static void rt_fs_report_clone(struct rt_context* ctx, const char* path, int32_t result) {
    uint32_t i;
    if (!(ctx->flags & RT_FLAG_GECKO)) return;
    rt_gecko_putc(ctx, 'K');
    rt_gecko_putc(ctx, ':');
    for (i = 0; i < RTFS_PATH_BYTES && path[i] != 0; ++i) rt_gecko_putc(ctx, (uint32_t)(uint8_t)path[i]);
    rt_gecko_putc(ctx, ':');
    rt_gecko_hex(ctx, (uint32_t)result);
    rt_gecko_putc(ctx, '\n');
}

/* <savegame clone> (rt_hook.h): the NAND data directory copied into the
 * folder, once, on the game's thread. Its own /dev/fs fd, opened and
 * closed here; the listing through the game's synchronous IOS_Ioctlv
 * (ReadDir: the path and a count in, the names one after another, each
 * NUL-terminated, in a buffer of 13 bytes per name, and the count out);
 * each name copied by rt_fs_copy_in with the same path on both sides
 * (NAND through the original IOS_Open, the card through the engine). */
static void rt_fs_clone(struct rt_context* ctx, struct rt_fs_state* st) {
    int32_t fd, r;
    uint32_t count, i, n;
    char* p = st->path;
    st->clone_pending = 0;
    if (!rt_fs_has_sync_originals(st) || st->ioctlv_sync == 0) {
        st->clone_failures++;
        return;
    }
    st->clones++;
    rt_zero_bytes((uint8_t*)p, RTFS_PATH_BYTES);
    p[0] = '/'; p[1] = 'd'; p[2] = 'e'; p[3] = 'v'; p[4] = '/'; p[5] = 'f'; p[6] = 's';
    fd = rt_fs_open_sync(st, (uint32_t)(uintptr_t)p, 0);
    if (fd < 0) {
        st->clone_failures++;
        rt_fs_report_clone(ctx, p, fd);
        return;
    }
    /* The listing. A missing directory is an empty save: nothing to copy. */
    rt_zero_bytes((uint8_t*)p, RTFS_PATH_BYTES);
    for (i = 0; i < st->fs.prefix_len && i < RTFS_PATH_BYTES - 1; ++i) p[i] = st->fs.data_prefix[i];
    st->count_in[0] = 0;
    st->count_out[0] = 0;
    rt_flush_range((uintptr_t)st->count_in, sizeof(st->count_in));
    rt_flush_range((uintptr_t)st->count_out, sizeof(st->count_out));
    st->dvec[0].data = (uint32_t)(uintptr_t)p;
    st->dvec[0].len = RTFS_PATH_BYTES;
    st->dvec[1].data = (uint32_t)(uintptr_t)st->count_out;
    st->dvec[1].len = 4;
    rt_flush_range((uintptr_t)st->dvec, sizeof(st->dvec));
    r = rt_fs_ioctlv_sync(st, fd, RTFS_IOCTL_READDIR, 1, 1, st->dvec);
    count = r < 0 ? 0 : st->count_out[0];
    if (count > RT_FS_CLONE_MAX) count = RT_FS_CLONE_MAX;
    if (r < 0 && r != RTFAT_ENOENT) st->clone_failures++;
    rt_fs_report_clone(ctx, p, r < 0 ? r : (int32_t)count);
    if (count != 0) {
        st->count_in[0] = count;
        rt_flush_range((uintptr_t)st->count_in, sizeof(st->count_in));
        rt_flush_range((uintptr_t)st->count_out, sizeof(st->count_out));
        rt_zero_bytes(st->names, sizeof(st->names));
        rt_flush_range((uintptr_t)st->names, sizeof(st->names));
        st->dvec[1].data = (uint32_t)(uintptr_t)st->count_in;
        st->dvec[1].len = 4;
        st->dvec[2].data = (uint32_t)(uintptr_t)st->names;
        st->dvec[2].len = count * RTFAT_SLOT_BYTES;
        st->dvec[3].data = (uint32_t)(uintptr_t)st->count_out;
        st->dvec[3].len = 4;
        rt_flush_range((uintptr_t)st->dvec, sizeof(st->dvec));
        r = rt_fs_ioctlv_sync(st, fd, RTFS_IOCTL_READDIR, 2, 2, st->dvec);
        if (r < 0) {
            st->clone_failures++;
            count = 0;
        } else if (st->count_out[0] < count) {
            count = st->count_out[0];
        }
    }
    for (n = 0, i = 0; n < count && i < count * RTFAT_SLOT_BYTES; ++n) {
        const char* name = (const char*)st->names + i;
        uint32_t at = st->fs.prefix_len;
        uint32_t len = 0;
        while (i + len < count * RTFAT_SLOT_BYTES && name[len] != 0) len++;
        i += len + 1;
        if (len == 0 || len > RTFAT_NAME_MAX || at + 1 + len >= RTFS_PATH_BYTES) continue;
        rt_zero_bytes((uint8_t*)p, RTFS_PATH_BYTES);
        {
            uint32_t k;
            for (k = 0; k < at; ++k) p[k] = st->fs.data_prefix[k];
            p[at++] = '/';
            for (k = 0; k < len; ++k) p[at + k] = name[k];
        }
        r = rt_fs_copy_in(ctx, st, fd, p, p, 0);
        if (r < 0) st->clone_failures++;
        else st->clone_files++;
        rt_fs_report_clone(ctx, p, r);
    }
    rt_fs_close_sync(st, fd);
}

/* Whether an ISFS rename crosses the directory's boundary: 1 inward
 * (`src` outside, `dst` a file name inside: an import), -1 outward,
 * 0 for anything else (the engine's business, or not ours). */
static int rt_fs_rename_kind(const struct rt_fs_state* st, const struct rtfs_ipc* ipc, const char** src,
                             const char** dst) {
    char name[RTFAT_NAME_MAX + 1];
    int p, q;
    if (ipc->command != RTFS_CMD_IOCTL || ipc->args.ioctl.request != RTFS_IOCTL_RENAME) return 0;
    if (ipc->args.ioctl.in == 0 || ipc->args.ioctl.in_len < 2u * RTFS_PATH_BYTES) return 0;
    if (ipc->fd < 0 || ipc->fd >= (int32_t)RTFS_FD_BASE || (st->fs.fs_fd >= 0 && ipc->fd != st->fs.fs_fd)) return 0;
    *src = (const char*)(uintptr_t)ipc->args.ioctl.in;
    *dst = *src + RTFS_PATH_BYTES;
    p = rtfs_path_type(&st->fs, *src, name);
    q = rtfs_path_type(&st->fs, *dst, name);
    if (p == RTFS_PATH_OUTSIDE && q == RTFS_PATH_FILE) return 1;
    if (p == RTFS_PATH_FILE && q == RTFS_PATH_OUTSIDE) return -1;
    return 0;
}

/* A synchronous SDK call: answered on the caller's thread, transfers
 * performed inline. Returns 0 to replay the original. */
static int rt_on_sync_fs(struct rt_context* ctx, uint32_t entry_index, uintptr_t* args, uint32_t* result) {
    struct rtfs_ipc ipc;
    struct rt_fs_state* st = rt_fs_of(ctx);
    uint32_t classification;
    if (st == 0) return 0;
    if (st->clone_pending) rt_fs_clone(ctx, st); /* the game's first call: on its thread */
    if (!rt_build_fs_ipc(entry_index, args, &ipc)) return 0;
    if (ipc.command == RTFS_CMD_OPEN && rtfs_is_fs_device((const char*)(uintptr_t)ipc.args.open.path)) {
        /* The game opening /dev/fs: done for it through the original, so
         * the fd is known from then on. */
        int32_t fd;
        if (st->open_sync == 0) return 0;
        fd = rt_fs_open_sync(st, ipc.args.open.path, ipc.args.open.mode);
        if (fd >= 0) {
            rtfs_learn_fs_fd(&st->fs, fd);
            st->fs_fd_learned++;
        }
        *result = (uint32_t)fd;
        return 1;
    }
    if (ipc.command == RTFS_CMD_CLOSE && ipc.fd >= 0 && ipc.fd == st->fs.fs_fd) {
        st->fs.fs_fd = -1; /* the close itself replays */
        return 0;
    }
    {
        const char* src;
        const char* dst;
        const int kind = rt_fs_rename_kind(st, &ipc, &src, &dst);
        if (kind != 0) {
            int32_t r = RTFAT_EACCESS;
            if (kind > 0) r = rt_fs_import(ctx, st, ipc.fd, src, dst);
            else st->import_refused++;
            rtfs_learn_fs_fd(&st->fs, ipc.fd);
            *result = (uint32_t)r;
            ctx->fs_hijacked++;
            rt_fs_report(ctx, entry_index, &ipc, r, 0);
            return 1;
        }
    }
    rt_fs_probe(st, &ipc, &classification);
    if (classification == RTFS_PASS_THROUGH) return 0;
    if (st->dead) {
        *result = (uint32_t)RTFAT_EIO;
        ctx->fs_hijacked++;
        rt_fs_report(ctx, entry_index, &ipc, RTFAT_EIO, 0);
        return 1;
    }
    while (rt_fs_admit(st, entry_index, &ipc, 0) != RT_FS_ADMIT_BEGUN) {
        if (!rt_fs_wait(ctx, st)) {
            /* The engine never came free: the request ahead is stuck
             * (nothing can be answered from the card meanwhile). */
            *result = (uint32_t)RTFAT_EIO;
            ctx->fs_hijacked++;
            rt_fs_report(ctx, entry_index, &ipc, RTFAT_EIO, 0);
            return 1;
        }
    }
    if (st->req.classification == RTFS_PASS_THROUGH) return 0;
    if (st->req.classification == RTFS_NEEDS_IO) rt_fs_advance(ctx, st, 1);
    *result = (uint32_t)st->req.result;
    ctx->fs_hijacked++;
    rt_fs_report(ctx, entry_index, &ipc, st->req.result, 0);
    return 1;
}

/* An asynchronous SDK call: accepted (0) and answered through the game's
 * callback later, from the IPC interrupt. Returns 0 to replay the
 * original. */
static int rt_on_async_fs(struct rt_context* ctx, uint32_t entry_index, uintptr_t* args, uint32_t* result) {
    struct rtfs_ipc ipc;
    struct rt_fs_state* st = rt_fs_of(ctx);
    uint32_t classification;
    uint32_t i;
    int admit;
    if (st == 0) return 0;
    if (st->clone_pending && rt_fs_in_thread()) rt_fs_clone(ctx, st);
    if (!rt_build_fs_ipc(entry_index, args, &ipc)) return 0;
    if (ipc.command == RTFS_CMD_OPEN && rtfs_is_fs_device((const char*)(uintptr_t)ipc.args.open.path)) {
        /* The game opening /dev/fs: replayed to IOS under observation,
         * our completion in place of the game's callback learns the fd.
         * Claimed with interrupts off like every other slot: hooks run
         * on the game's thread and in its IPC callbacks, and two
         * overlapping opens must never take the same slot (one game's
         * callback would be lost). */
        struct rt_fs_pend* slot = 0;
        const uint32_t msr = rt_interrupts_off();
        for (i = 0; i < RT_FS_SNOOPS && slot == 0; ++i) {
            if (!st->snoop[i].in_use) slot = &st->snoop[i];
        }
        if (slot != 0) {
            slot->in_use = 1;
            slot->kind = RT_FS_OP_SNOOP;
            slot->callback = ipc.callback;
            slot->user_data = ipc.user_data;
        }
        rt_interrupts_restore(msr);
        if (slot == 0) return 0; /* every slot taken: replayed unobserved */
        rt_fs_swap_callback(args, ipc.command, (uintptr_t)st->complete_fs, (uintptr_t)slot);
        return 0;
    }
    if (ipc.command == RTFS_CMD_CLOSE && ipc.fd >= 0 && ipc.fd == st->fs.fs_fd) {
        st->fs.fs_fd = -1;
        return 0;
    }
    {
        const char* src;
        const char* dst;
        const int kind = rt_fs_rename_kind(st, &ipc, &src, &dst);
        if (kind != 0) {
            /* An import needs the game's synchronous functions, so a
             * thread; from an IPC callback it is refused. */
            int32_t r = RTFAT_EACCESS;
            if (kind > 0 && rt_fs_in_thread()) r = rt_fs_import(ctx, st, ipc.fd, src, dst);
            else st->import_refused++;
            rtfs_learn_fs_fd(&st->fs, ipc.fd);
            ctx->fs_hijacked++;
            rt_fs_report(ctx, entry_index, &ipc, r, 0);
            rt_fs_deliver(ctx, st, ipc.callback, ipc.user_data, r);
            *result = 0;
            return 1;
        }
    }
    rt_fs_probe(st, &ipc, &classification);
    if (classification == RTFS_PASS_THROUGH) return 0;
    ctx->fs_hijacked++;
    if (st->dead) {
        rt_fs_deliver(ctx, st, ipc.callback, ipc.user_data, RTFAT_EIO);
        rt_fs_report(ctx, entry_index, &ipc, RTFAT_EIO, 0);
        *result = 0;
        return 1;
    }
    admit = rt_fs_admit(st, entry_index, &ipc, 1);
    if (admit == RT_FS_ADMIT_QUEUED) {
        st->queued++;
        *result = 0;
        return 1;
    }
    if (admit == RT_FS_ADMIT_FULL) {
        /* Refused at the call, as IOS refuses when its queue is full: no
         * callback follows. */
        *result = (uint32_t)RTFAT_EACCESS;
        rt_fs_report(ctx, entry_index, &ipc, RTFAT_EACCESS, 0);
        return 1;
    }
    if (st->req.classification == RTFS_PASS_THROUGH) {
        ctx->fs_hijacked--;
        return 0;
    }
    if (st->req.classification == RTFS_NEEDS_IO) {
        st->pend.in_use = 1;
        st->pend.kind = RT_FS_OP_FILE;
        if (rt_fs_advance(ctx, st, 0) == RT_FS_ADVANCE_ISSUED) {
            rt_fs_report(ctx, entry_index, &ipc, 0, 1);
            *result = 0;
            return 1;
        }
        st->pend.in_use = 0;
    }
    rt_fs_report(ctx, entry_index, &ipc, st->req.result, 0);
    rt_fs_deliver(ctx, st, st->req.callback, st->req.user_data, st->req.result);
    *result = 0;
    return 1;
}

void rt_on_fs_complete(struct rt_context* ctx, int32_t* result, void* tag, uintptr_t* callback,
                       uintptr_t* user_data) {
    struct rt_fs_state* st = rt_fs_of(ctx);
    uint32_t i;
    *callback = 0;
    if (st == 0) return;
    for (i = 0; i < RT_FS_SNOOPS; ++i) {
        struct rt_fs_pend* slot = &st->snoop[i];
        if (tag != (void*)slot || !slot->in_use) continue;
        if (*result >= 0) {
            rtfs_learn_fs_fd(&st->fs, *result);
            st->fs_fd_learned++;
        }
        slot->in_use = 0;
        *callback = (uintptr_t)slot->callback;
        *user_data = (uintptr_t)slot->user_data;
        return;
    }
    for (i = 0; i < RT_FS_DELIVERS; ++i) {
        struct rt_fs_pend* slot = &st->deliver[i];
        if (tag != (void*)slot || !slot->in_use) continue;
        *result = slot->result;
        *callback = (uintptr_t)slot->callback;
        *user_data = (uintptr_t)slot->user_data;
        slot->in_use = 0;
        return;
    }
    if (tag == (void*)&st->pend && st->pend.in_use) {
        struct rtfs_request* req = &st->req;
        if (*result < 0) st->failures++;
        req->fat.io_status = *result < 0 ? *result : 0;
        if (rt_fs_advance(ctx, st, 0) == RT_FS_ADVANCE_ISSUED) return;
        st->pend.in_use = 0;
        *result = req->result;
        *callback = (uintptr_t)req->callback;
        *user_data = (uintptr_t)req->user_data;
        rt_fs_report_done(ctx, req->result);
        rt_fs_start_queued(ctx, st);
        return;
    }
    /* An unknown tag: nothing to continue, nothing to call. */
}

int rt_on_ipc(struct rt_context* ctx, uint32_t entry_index, uintptr_t* args, uint32_t* result) {
    if (entry_index == RT_IPC_ASYNC_IOCTL) {
        /* Disc reads keep their hook; every other async ioctl joins the
         * savegame path. ISFS request numbers never collide with 0x71,
         * so the check below reads no game memory for them. */
        const uint32_t ioctl = (uint32_t)args[1];
        const uint32_t* in = (const uint32_t*)args[2];
        const uint32_t in_len = (uint32_t)args[3];
        if (rt_is_di_read(ioctl, in, in_len)) return rt_on_ioctl_async(ctx, args, result);
        return rt_on_async_fs(ctx, entry_index, args, result);
    }
    if (entry_index >= RT_IPC_COMMANDS) return rt_on_sync_fs(ctx, entry_index, args, result);
    return rt_on_async_fs(ctx, entry_index, args, result);
}

/* Where a run of `record` lands in the game's buffer. */
static uint8_t* rt_run_destination(const struct rt_pending* record, const rt_run* run) {
    return (uint8_t*)(uintptr_t)record->out + (uint32_t)(run->vstart - ((uint64_t)record->word_offset << 2));
}

/* Issues the next chunk of the current DISC run: a DVDLowRead of the
 * 32-byte aligned span around the wanted bytes into the bounce buffer. */
static int rt_issue_disc_chunk(struct rt_context* ctx, struct rt_pending* record) {
    const rt_run* run = &record->runs[record->run_index];
    const uint32_t remaining = (uint32_t)run->length - record->run_done;
    const uint32_t first = (uint32_t)run->source + record->run_done; /* partition byte wanted first */
    const uint32_t start = first & ~31u;
    uint32_t end = first + remaining;
    uint32_t length;
    uint32_t i;
    if (remaining == 0) return 0;
    end = (end + 31u) & ~31u;
    if (end - start > RT_BOUNCE_BYTES) end = start + RT_BOUNCE_BYTES;
    length = end - start;
    record->chunk_skip = first - start;
    record->chunk_bytes = length - record->chunk_skip;
    if (record->chunk_bytes > remaining) record->chunk_bytes = remaining;
    for (i = 0; i < 8; ++i) record->di_command[i] = 0;
    record->di_command[0] = RT_DI_READ << 24;
    record->di_command[1] = length;
    record->di_command[2] = start >> 2;
    rt_flush_range((uintptr_t)record->di_command, sizeof(record->di_command));
    rt_flush_range((uintptr_t)record->bounce, length);
    record->phase = RT_PHASE_DISC_RUN;
    ctx->disc_requests++;
    if (rt_di_read_async(ctx, record, length) < 0) {
        ctx->disc_failures++;
        return -1;
    }
    return 1;
}

/* Issues the next SD chunk of the current run. Returns 1 when a request
 * is in flight, 0 when the run is complete or not an SD run, -1 on an IPC
 * refusal. */
static int rt_issue_sd_chunk(struct rt_context* ctx, struct rt_pending* record) {
    const rt_run* run = &record->runs[record->run_index];
    const uint32_t remaining = (uint32_t)run->length - record->run_done;
    const uint32_t byte_in_sectors = run->skip + record->run_done; /* from the start of sector `source` */
    const uint32_t sector = (uint32_t)run->source + byte_in_sectors / RT_SECTOR_BYTES;
    const uint32_t skip = byte_in_sectors % RT_SECTOR_BYTES;
    uint32_t want = skip + remaining;
    uint32_t sectors;
    struct rt_sdio_request* rq = &record->request;
    if (run->kind == RT_KIND_DISC) return rt_issue_disc_chunk(ctx, record);
    if (run->kind != RT_KIND_SD || remaining == 0) return 0;
    if (want > RT_BOUNCE_BYTES) want = RT_BOUNCE_BYTES;
    sectors = (want + RT_SECTOR_BYTES - 1) / RT_SECTOR_BYTES;
    record->chunk_skip = skip;
    record->chunk_bytes = sectors * RT_SECTOR_BYTES - skip;
    if (record->chunk_bytes > remaining) record->chunk_bytes = remaining;
    rq->cmd = RT_SD_CMD_READMULTIBLOCK;
    rq->cmd_type = RT_SD_CMDTYPE_AC;
    rq->rsp_type = RT_SD_RESPONSE_R1;
    rq->arg = ctx->sdio_sdhc ? sector : sector * RT_SECTOR_BYTES;
    rq->blk_cnt = sectors;
    rq->blk_size = RT_SECTOR_BYTES;
    rq->dma_addr = record->bounce;
    rq->isdma = 1;
    rq->pad0 = 0;
    record->vec[0].data = (uint32_t)(uintptr_t)rq;
    record->vec[0].len = sizeof(*rq);
    record->vec[1].data = record->bounce;
    record->vec[1].len = sectors * RT_SECTOR_BYTES;
    record->vec[2].data = (uint32_t)(uintptr_t)record->response;
    record->vec[2].len = sizeof(record->response);
    rt_flush_range((uintptr_t)rq, sizeof(*rq));
    rt_flush_range((uintptr_t)record->vec, sizeof(record->vec));
    rt_flush_range((uintptr_t)record->bounce, sectors * RT_SECTOR_BYTES); /* no stale lines over the DMA target */
    record->phase = RT_PHASE_SD;
    ctx->sd_requests++;
    if (rt_ioctlv_async(ctx, record) < 0) {
        ctx->sd_failures++;
        return -1;
    }
    return 1;
}

/* The runtime's bytes of a completed read, in buffer order, for the
 * diagnostics (MEM, ZERO, SD runs and virtual gaps). */
static uint32_t rt_checksum_runs(const struct rt_pending* record) {
    uint32_t checksum = 0;
    uint32_t i;
    for (i = 0; i < record->run_count; ++i) {
        const rt_run* run = &record->runs[i];
        const uint8_t* bytes = rt_run_destination(record, run);
        uint32_t k;
        if (run->kind == RT_KIND_PASSTHROUGH && !record->is_virtual) continue;
        for (k = 0; k < (uint32_t)run->length; ++k) checksum = checksum * 31u + bytes[k];
    }
    return checksum;
}

void rt_on_di_complete(struct rt_context* ctx, int32_t* result, struct rt_pending* record, uintptr_t* callback,
                       uintptr_t* user_data) {
    ctx->completions++;
    if (record->phase == RT_PHASE_DISC) {
        record->di_result = (uint32_t)*result;
        if (*result == RT_DI_SUCCESS) {
            /* Everything that comes from memory, now; SD runs follow. */
            uint32_t i;
            for (i = 0; i < record->run_count; ++i) {
                const rt_run* run = &record->runs[i];
                uint8_t* dst = rt_run_destination(record, run);
                const uint32_t n = (uint32_t)run->length;
                if (run->kind == RT_KIND_MEM) {
                    rt_copy(dst, (const uint8_t*)(uintptr_t)run->source, n);
                } else if (run->kind == RT_KIND_ZERO || (run->kind == RT_KIND_PASSTHROUGH && record->is_virtual)) {
                    rt_zero(dst, n); /* nothing on the disc belongs in a virtual gap */
                } else {
                    continue; /* PASSTHROUGH: the disc already filled it; SD and DISC: below */
                }
                rt_flush_range((uintptr_t)dst, n);
            }
            record->run_index = 0;
            record->run_done = 0;
        } else {
            record->run_index = record->run_count; /* nothing to fetch for a failed read */
        }
    } else {
        /* An SD or DISC chunk landed (or failed). IPC results are 0 for a
         * successful SD request and RT_DI_SUCCESS for a DVDLowRead. */
        const rt_run* run = &record->runs[record->run_index];
        const int failed = record->phase == RT_PHASE_SD ? *result < 0 : *result != RT_DI_SUCCESS;
        if (failed) {
            if (record->phase == RT_PHASE_SD) {
                ctx->sd_failures++;
                record->di_result = RT_DI_ERROR;
            } else {
                ctx->disc_failures++;
                record->di_result = *result > 0 ? (uint32_t)*result : RT_DI_ERROR; /* the drive's own verdict */
            }
            record->run_index = record->run_count;
        } else {
            uint8_t* dst = rt_run_destination(record, run) + record->run_done;
            rt_copy(dst, (const uint8_t*)(uintptr_t)record->bounce + record->chunk_skip, record->chunk_bytes);
            rt_flush_range((uintptr_t)dst, record->chunk_bytes);
            record->run_done += record->chunk_bytes;
        }
    }

    /* Next SD work, if any. */
    while (record->run_index < record->run_count) {
        const int issued = rt_issue_sd_chunk(ctx, record);
        if (issued > 0) {
            *callback = 0; /* still busy: the IPC dispatcher just returns */
            return;
        }
        if (issued < 0) {
            record->di_result = RT_DI_ERROR;
            break;
        }
        record->run_index++;
        record->run_done = 0;
    }

    /* Done: hand the game its completion. */
    *result = (int32_t)record->di_result;
    *callback = (uintptr_t)record->callback;
    *user_data = (uintptr_t)record->user_data;
    if (record->di_result == RT_DI_SUCCESS && (ctx->flags & RT_FLAG_GECKO)) {
        /* Diagnostics only (they read the buffer back): "M<word
         * offset>:<length>:<checksum of the redirected bytes>\n" */
        ctx->last_checksum = rt_checksum_runs(record);
        rt_gecko_putc(ctx, 'M');
        rt_gecko_hex(ctx, record->word_offset);
        rt_gecko_putc(ctx, ':');
        rt_gecko_hex(ctx, record->length);
        rt_gecko_putc(ctx, ':');
        rt_gecko_hex(ctx, ctx->last_checksum);
        rt_gecko_putc(ctx, '\n');
    }
    record->in_use = 0;
}
