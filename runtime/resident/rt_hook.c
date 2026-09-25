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
#define RT_DI_SEEK 0xABu

/* /dev/sdio/slot0 (wiibrew, libogc wiisd.c) */
#define RT_SDIO_SENDCMD 7u
#define RT_SD_CMD_READMULTIBLOCK 0x12u
#define RT_SD_CMD_WRITEMULTIBLOCK 0x19u
#define RT_SD_CMDTYPE_AC 3u
#define RT_SD_RESPONSE_R1 1u
/* CMD13 SEND_STATUS: the R1 card status in the response's first word,
 * the card's state in its bits 9-12 (wiibrew /dev/sdio/slot0, the SD
 * physical layer spec). */
#define RT_SD_CMD_SENDSTATUS 0x0Du
#define RT_SD_STATE_RCV 6u
#define RT_SD_STATE_PRG 7u
#define RT_SD_SETTLE_POLLS 8192u  /* about half a second of CMD13s; the spec allows 250 ms per write */

int rt_is_di_read(uint32_t ioctl, const uint32_t* in, uint32_t in_len) {
    /* DVDLowRead: ioctl 0x71 with a 0x20-byte command block whose first
     * word repeats the command number in its top byte (wiibrew /dev/di). */
    return ioctl == RT_DI_READ && in_len == 0x20 && in != 0 && (in[0] >> 24) == RT_DI_READ;
}

static int rt_is_di_seek(uint32_t ioctl, const uint32_t* in, uint32_t in_len) {
    /* DVDLowSeek: the IOS request and the command block both identify the
     * command, and the requested disc word is in command[2]. */
    return ioctl == RT_DI_SEEK && in_len == 0x20 && in != 0 && (in[0] >> 24) == RT_DI_SEEK;
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
 * means yes / accepted. Lines are written from game threads and from the
 * IPC interrupt handler on the one EXI channel: every reporter prints its
 * whole line with interrupts off, so lines never interleave. */
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
static int32_t rt_ioctlv_async(struct rt_context* ctx, struct rt_pending* record, uint32_t fd, uint32_t ioctl) {
    rt_ioctlv_async_fn fn = (rt_ioctlv_async_fn)(uintptr_t)ctx->ioctlv_async;
    if (fn == 0) return -1;
    return fn(fd, ioctl, 2, 1, record->vec, ctx->complete_entry, record);
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
/* The game's own read again, into its buffer (a retry). */
static int32_t rt_game_read_async(struct rt_context* ctx, struct rt_pending* record) {
    rt_ioctl_async_fn fn = (rt_ioctl_async_fn)(uintptr_t)ctx->di_read_entry;
    if (fn == 0) return -1;
    return fn(ctx->di_fd, RT_DI_READ, record->di_command, 0x20, record->out, record->length, ctx->complete_entry,
              record);
}

/* The savegame path's IOS calls. The transfer request is built in the
 * state's own blocks (rt_fs_build_sendcmd); synchronous ones go through
 * the game's synchronous IOS_Ioctlv, asynchronous ones through its
 * IOS_IoctlvAsync with the FS completion entry and the given record. */
typedef int32_t (*rt_ioctlv_sync_fn)(uint32_t fd, uint32_t ioctl, uint32_t in_count, uint32_t out_count,
                                     struct rt_ioctlv* vec);
typedef int32_t (*rt_open_sync_fn)(const char* path, uint32_t mode);
static int32_t rt_fs_sendcmd_call(struct rt_context* ctx, struct rt_fs_state* st, int async) {
    /* slot0: SENDCMD (request in, data and response out); d2x: sector and
     * count in, then the data out for a read or in for a write. */
    const int d2x = ctx->sdio_sdhc == RT_SD_D2X;
    const int write = st->req.fat.io_write != 0;
    const uint32_t ioctl = d2x ? (write ? RT_SDHC_WRITE : RT_SDHC_READ) : RT_SDIO_SENDCMD;
    const uint32_t in_count = d2x && write ? 3u : 2u;
    const uint32_t out_count = d2x && write ? 0u : 1u;
    if (ctx->sdio_fd == 0xFFFFFFFFu) return RTFAT_EIO;
    if (async) {
        rt_ioctlv_async_fn fn = (rt_ioctlv_async_fn)(uintptr_t)ctx->ioctlv_async;
        if (fn == 0) return RTFAT_EIO;
        return fn(ctx->sdio_fd, ioctl, in_count, out_count, st->vec, st->complete_fs, (struct rt_pending*)&st->pend);
    } else {
        rt_ioctlv_sync_fn fn = (rt_ioctlv_sync_fn)(uintptr_t)st->ioctlv_sync;
        if (fn == 0) return RTFAT_EIO;
        return fn(ctx->sdio_fd, ioctl, in_count, out_count, st->vec);
    }
}
/* The null round trip: SD GETSTATUS through the unhooked IOS_IoctlAsync,
 * its 4-byte answer landing in the slot's own line. */
static int32_t rt_fs_getstatus_async(struct rt_context* ctx, struct rt_fs_state* st, struct rt_fs_pend* slot) {
    rt_ioctl_async_fn fn = (rt_ioctl_async_fn)(uintptr_t)ctx->di_read_entry;
    if (ctx->sdio_fd == 0xFFFFFFFFu) return -1;
    if (ctx->sdio_sdhc == RT_SD_D2X) {
        /* d2x's device answers ioctlvs only. */
        rt_ioctlv_async_fn vfn = (rt_ioctlv_async_fn)(uintptr_t)ctx->ioctlv_async;
        if (vfn == 0) return -1;
        return vfn(ctx->sdio_fd, RT_SDHC_ISINSERTED, 0, 0, st->vec, st->complete_fs, (struct rt_pending*)slot);
    }
    if (fn == 0) return -1;
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
/* The import job's NAND requests through the game's asynchronous
 * originals (SDK forms), the FS completion entry their callback. */
typedef int32_t (*rt_open_async_fn)(const char* path, uint32_t mode, uint32_t callback, void* tag);
typedef int32_t (*rt_close_async_fn)(int32_t fd, uint32_t callback, void* tag);
typedef int32_t (*rt_read_async_fn)(int32_t fd, void* buffer, uint32_t length, uint32_t callback, void* tag);
static int32_t rt_fs_open_async(struct rt_fs_state* st, const char* path, uint32_t mode, void* tag) {
    return ((rt_open_async_fn)(uintptr_t)st->open_async)(path, mode, st->complete_fs, tag);
}
static int32_t rt_fs_close_async(struct rt_fs_state* st, int32_t fd, void* tag) {
    return ((rt_close_async_fn)(uintptr_t)st->close_async)(fd, st->complete_fs, tag);
}
static int32_t rt_fs_read_async(struct rt_fs_state* st, int32_t fd, uint32_t buffer, uint32_t length, void* tag) {
    return ((rt_read_async_fn)(uintptr_t)st->read_async)(fd, (void*)(uintptr_t)buffer, length, st->complete_fs, tag);
}
static int32_t rt_fs_ioctl_async(struct rt_context* ctx, struct rt_fs_state* st, int32_t fd, uint32_t request,
                                 uint32_t in, uint32_t in_len, uint32_t out, uint32_t out_len, void* tag) {
    rt_ioctl_async_fn fn = (rt_ioctl_async_fn)(uintptr_t)ctx->di_read_entry;
    if (fn == 0) return -1;
    return fn((uint32_t)fd, request, (uint32_t*)(uintptr_t)in, in_len, out, out_len, st->complete_fs,
              (struct rt_pending*)tag);
}
/* CMD13 in the state's request, its answer in the state's response line
 * (a plain SENDCMD, no DMA: libogc's wiisd.c sends status requests the
 * same way). */
static int32_t rt_fs_status_call(struct rt_context* ctx, struct rt_fs_state* st, int async) {
    if (async) {
        rt_ioctl_async_fn fn = (rt_ioctl_async_fn)(uintptr_t)ctx->di_read_entry;
        if (fn == 0) return -1;
        return fn(ctx->sdio_fd, RT_SDIO_SENDCMD, (uint32_t*)(void*)&st->request, sizeof(st->request),
                  (uint32_t)(uintptr_t)st->response, 16, st->complete_fs, (struct rt_pending*)&st->pend);
    }
    if (st->ioctl_sync == 0) return -1;
    return rt_fs_ioctl_sync(st, (int32_t)ctx->sdio_fd, RT_SDIO_SENDCMD, (uint32_t)(uintptr_t)&st->request,
                            sizeof(st->request), (uint32_t)(uintptr_t)st->response, 16);
}
/* A disc read's null round trip while the card is busy (GETSTATUS asks
 * the host controller, not the card). */
static int32_t rt_sd_wait_trip(struct rt_context* ctx, struct rt_pending* record) {
    rt_ioctl_async_fn fn = (rt_ioctl_async_fn)(uintptr_t)ctx->di_read_entry;
    if (fn == 0) return -1;
    rt_flush_range((uintptr_t)record->response, sizeof(record->response));
    return fn(ctx->sdio_fd, RT_SDIO_GETSTATUS, 0, 0, (uint32_t)(uintptr_t)record->response, 4, ctx->complete_entry,
              record);
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
/* One turn of a sync arrival's wait: a synchronous SD GETSTATUS through
 * the game's IOS_Ioctl. The SDK sleeps this thread until IOS answers,
 * and the SD device answers in order, so the reply comes after the
 * transfer of the request ahead: the thread holding the engine (another
 * game thread inside its own sync call, or the IPC interrupt) gets the
 * CPU meanwhile. Without the original the wait spins (interrupts on:
 * the IPC interrupt still drives async requests ahead). */
static void rt_fs_wait_tick(struct rt_context* ctx, struct rt_fs_state* st) {
    if (ctx->sdio_sdhc == RT_SD_D2X) {
        if (st->ioctlv_sync != 0 && ctx->sdio_fd != 0xFFFFFFFFu) {
            ((rt_ioctlv_sync_fn)(uintptr_t)st->ioctlv_sync)(ctx->sdio_fd, RT_SDHC_ISINSERTED, 0, 0, st->vec);
        }
        return;
    }
    if (st->ioctl_sync == 0 || ctx->sdio_fd == 0xFFFFFFFFu) return;
    rt_flush_range((uintptr_t)st->wait_status, sizeof(st->wait_status));
    rt_fs_ioctl_sync(st, (int32_t)ctx->sdio_fd, RT_SDIO_GETSTATUS, 0, 0, (uint32_t)(uintptr_t)st->wait_status, 4);
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
int32_t (*rt_host_fs_open_async)(const char* path, uint32_t mode, uint32_t callback, void* tag) = 0;
int32_t (*rt_host_fs_close_async)(int32_t fd, uint32_t callback, void* tag) = 0;
int32_t (*rt_host_fs_read_async)(int32_t fd, uint32_t buffer, uint32_t length, uint32_t callback, void* tag) = 0;
int32_t (*rt_host_fs_ioctl_async)(int32_t fd, uint32_t request, uint32_t in, uint32_t in_len, uint32_t out,
                                   uint32_t out_len, uint32_t callback, void* tag) = 0;
int rt_host_fs_in_thread = 1;
void (*rt_host_game_callback)(uint32_t cb, int32_t result, uint32_t user_data) = 0;
static int32_t rt_fs_open_async(struct rt_fs_state* st, const char* path, uint32_t mode, void* tag) {
    return rt_host_fs_open_async == 0 ? -1 : rt_host_fs_open_async(path, mode, st->complete_fs, tag);
}
static int32_t rt_fs_close_async(struct rt_fs_state* st, int32_t fd, void* tag) {
    return rt_host_fs_close_async == 0 ? -1 : rt_host_fs_close_async(fd, st->complete_fs, tag);
}
static int32_t rt_fs_read_async(struct rt_fs_state* st, int32_t fd, uint32_t buffer, uint32_t length, void* tag) {
    return rt_host_fs_read_async == 0 ? -1 : rt_host_fs_read_async(fd, buffer, length, st->complete_fs, tag);
}
static int32_t rt_fs_ioctl_async(struct rt_context* ctx, struct rt_fs_state* st, int32_t fd, uint32_t request,
                                 uint32_t in, uint32_t in_len, uint32_t out, uint32_t out_len, void* tag) {
    (void)ctx;
    return rt_host_fs_ioctl_async == 0 ? -1
                                       : rt_host_fs_ioctl_async(fd, request, in, in_len, out, out_len, st->complete_fs, tag);
}
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
static int32_t rt_fs_status_call(struct rt_context* ctx, struct rt_fs_state* st, int async) {
    (void)ctx;
    (void)st;
    (void)async;
    return -1;
}
static int32_t rt_sd_wait_trip(struct rt_context* ctx, struct rt_pending* record) {
    if (rt_host_ioctl_async == 0) return -1;
    return rt_host_ioctl_async(ctx->sdio_fd, RT_SDIO_GETSTATUS, 0, 0, (uint32_t)(uintptr_t)record->response, 4,
                               ctx->complete_entry, record);
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
static void rt_fs_wait_tick(struct rt_context* ctx, struct rt_fs_state* st) {
    (void)st;
    if (rt_host_fs_wait != 0) rt_host_fs_wait(ctx);
}
static int32_t rt_di_read_async(struct rt_context* ctx, struct rt_pending* record, uint32_t length) {
    if (ctx->di_read_entry == 0 || rt_host_ioctl_async == 0) return -1;
    return rt_host_ioctl_async(ctx->di_fd, RT_DI_READ, record->di_command, 0x20, record->bounce, length,
                               ctx->complete_entry, record);
}
static int32_t rt_game_read_async(struct rt_context* ctx, struct rt_pending* record) {
    if (ctx->di_read_entry == 0 || rt_host_ioctl_async == 0) return -1;
    return rt_host_ioctl_async(ctx->di_fd, RT_DI_READ, record->di_command, 0x20, record->out, record->length,
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
static int32_t rt_ioctlv_async(struct rt_context* ctx, struct rt_pending* record, uint32_t fd, uint32_t ioctl) {
    if (ctx->ioctlv_async == 0 || rt_host_ioctlv_async == 0) return -1;
    return rt_host_ioctlv_async(fd, ioctl, 2, 1, record->vec, ctx->complete_entry, record);
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

/* Splits `length` bytes at partition byte `offset` against the table into
 * `runs`. Returns the number of runs and sets *covered to the bytes they
 * reach: all of `length`, or less when the read needs more than
 * RT_MAX_RUNS pieces (the completion serves the rest window by window).
 * Returns -1 when the read must pass through untouched (no table, error). */
static int rt_split(const struct rt_context* ctx, uint64_t offset, uint32_t length, rt_run* runs,
                    uint32_t* covered, int* touched) {
    uint32_t count = 0;
    uint32_t i;
    int rc;
    *touched = 0;
    *covered = 0;
    if (ctx->table == 0) return -1;
    rc = rt_lookup((const rt_header*)(uintptr_t)ctx->table, offset, length, runs, RT_MAX_RUNS, &count);
    if (rc == RT_ERR_RUNS && count == RT_MAX_RUNS) {
        /* The runs written are valid; a gap and a table piece alternate at
         * worst, so a full window always holds one of ours. */
        const rt_run* last = &runs[count - 1];
        *covered = (uint32_t)(last->vstart + last->length - offset);
    } else if (rc != RT_OK) {
        return -1;
    } else {
        *covered = length;
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

#ifdef RT_RVZ
static int rt_rvz_on_read(struct rt_context* ctx, struct rt_rvz_state* st, uintptr_t* args, uint32_t* result,
                          uint32_t word_offset, uint32_t length);
static struct rt_rvz_state* rt_rvz_of(const struct rt_context* ctx);
#endif

int rt_on_ioctl_async(struct rt_context* ctx, uintptr_t* args, uint32_t* result) {
    const uint32_t fd = (uint32_t)args[0];
    const uint32_t ioctl = (uint32_t)args[1];
    const uint32_t* in = (const uint32_t*)args[2];
    const uint32_t in_len = (uint32_t)args[3];
    (void)result;
    ctx->ioctl_async_calls++;
    if (rt_is_di_seek(ioctl, in, in_len) && ctx->virtual_start_words != 0 && in[2] >= ctx->virtual_start_words) {
        /* A seek is normally followed by the read we redirect below.  A
         * virtual file has no drive address, so keep the original async IOS
         * call and callback but make its seek land on a known readable word. */
        uint32_t* command = (uint32_t*)args[2];
        command[2] = 0;
        rt_flush_range((uintptr_t)command, 0x20);
    }
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
            const uint32_t msr = rt_interrupts_off();
            rt_gecko_putc(ctx, 'R');
            rt_gecko_hex(ctx, word_offset);
            rt_gecko_putc(ctx, ':');
            rt_gecko_hex(ctx, length);
            rt_gecko_putc(ctx, '\n');
            rt_interrupts_restore(msr);
        }
#ifdef RT_RVZ
        {
            struct rt_rvz_state* st = rt_rvz_of(ctx);
            if (st != 0) return rt_rvz_on_read(ctx, st, args, result, word_offset, length);
        }
#endif
        if (ctx->table != 0) {
            const int in_window = ctx->virtual_start_words != 0 && word_offset >= ctx->virtual_start_words;
            struct rt_pending* rec = rt_claim_pending(ctx);
            if (rec == 0) {
                ctx->pending_overflow++;
            } else {
                int touched = 0;
                uint32_t covered = 0;
                const int count = rt_split(ctx, (uint64_t)word_offset << 2, length, rec->runs, &covered, &touched);
                if (count < 0) {
                    rec->in_use = 0; /* passes through; a virtual read then fails at the drive, honestly */
                } else if (!touched && !in_window) {
                    rec->in_use = 0; /* nothing of ours in this read */
                } else {
                    if (covered < length) ctx->run_overflow++; /* served in windows */
                    rec->run_count = (uint32_t)count;
                    rec->covered = covered;
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
                    ctx->retry[rec - ctx->pending] = 0;
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

/* "F<entry>:<fd or path>[/<ioctl>]:<result>\n" over the Gecko for an
 * answered call ('P' as the result of one whose transfers are in flight)
 * and "C:<result>\n" when such a one completes. */
static void rt_fs_report(struct rt_context* ctx, uint32_t entry_index, const struct rtfs_ipc* ipc, int32_t result,
                         int pending) {
    uint32_t msr;
    if (!(ctx->flags & RT_FLAG_GECKO)) return;
    msr = rt_interrupts_off();
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
    rt_interrupts_restore(msr);
}

static void rt_fs_report_done(struct rt_context* ctx, int32_t result) {
    uint32_t msr;
    if (!(ctx->flags & RT_FLAG_GECKO)) return;
    msr = rt_interrupts_off();
    rt_gecko_putc(ctx, 'C');
    rt_gecko_putc(ctx, ':');
    rt_gecko_hex(ctx, (uint32_t)result);
    rt_gecko_putc(ctx, '\n');
    rt_interrupts_restore(msr);
}

/* The SENDCMD for the engine's pending transfer (CMD18 read, CMD25 write:
 * wiibrew /dev/sdio, libogc wiisd.c), in the state's own lines, flushed
 * for IOS along with the buffer the card DMAs from or into. */
static void rt_fs_build_sendcmd(struct rt_context* ctx, struct rt_fs_state* st) {
    const struct rtfat_op* op = &st->req.fat;
    struct rt_sdio_request* rq = &st->request;
    const uint32_t bytes = op->io_count * RT_SECTOR_BYTES;
    if (ctx->sdio_sdhc == RT_SD_D2X) {
        /* The request's first two words hold the sector and the count. */
        rq->cmd = op->io_lba;
        rq->cmd_type = op->io_count;
        st->vec[0].data = (uint32_t)(uintptr_t)&rq->cmd;
        st->vec[0].len = 4;
        st->vec[1].data = (uint32_t)(uintptr_t)&rq->cmd_type;
        st->vec[1].len = 4;
        st->vec[2].data = op->io_buffer;
        st->vec[2].len = bytes;
        rt_flush_range((uintptr_t)rq, sizeof(*rq));
        rt_flush_range((uintptr_t)st->vec, sizeof(st->vec));
        rt_flush_range((uintptr_t)op->io_buffer, bytes);
        return;
    }
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

/* After a write on /dev/sdio/slot0 the card goes on programming its
 * flash; a command sent meanwhile is refused, and a disc read of the
 * mods landing there (Newer streams its music from the card) failed and
 * took the savegame down with it. libogc's driver deselects the card
 * after every transfer (an R1b command, which waits out the busy card);
 * d2x's /dev/sdio/sdhc waits too. Here the engine asks the card's status
 * (CMD13) after each write until it has left the receive and programming
 * states, and `card_busy` holds the disc engine's card reads back until
 * then. Not for d2x's device, nor without the card's address. */
static int rt_sd_settles(const struct rt_context* ctx, const struct rt_fs_state* st) {
    return ctx->sdio_sdhc != RT_SD_D2X && st->card_rca != 0 && ctx->sdio_fd != 0xFFFFFFFFu;
}

static void rt_fs_build_status(struct rt_fs_state* st) {
    struct rt_sdio_request* rq = &st->request;
    uint32_t i;
    rq->cmd = RT_SD_CMD_SENDSTATUS;
    rq->cmd_type = RT_SD_CMDTYPE_AC;
    rq->rsp_type = RT_SD_RESPONSE_R1;
    rq->arg = st->card_rca << 16;
    rq->blk_cnt = 0;
    rq->blk_size = 0;
    rq->dma_addr = 0;
    rq->isdma = 0;
    rq->pad0 = 0;
    for (i = 0; i < 4; ++i) st->response[i] = 0;
    rt_flush_range((uintptr_t)rq, sizeof(*rq));
    rt_flush_range((uintptr_t)st->response, sizeof(st->response));
}

/* The answer of the CMD13 just completed: still receiving or programming? */
static int rt_fs_card_writing(struct rt_fs_state* st) {
    uint32_t state;
    rt_flush_range((uintptr_t)st->response, sizeof(st->response)); /* drops the line: IOS wrote it */
    state = (st->response[0] >> 9) & 15u;
    return state == RT_SD_STATE_RCV || state == RT_SD_STATE_PRG;
}

/* Marks the card busy right before a write goes out. */
static void rt_fs_mark_write(struct rt_context* ctx, struct rt_fs_state* st) {
    if (st->req.fat.io_write && rt_sd_settles(ctx, st)) st->card_busy = 1;
}

/* The synchronous wait, on the caller's thread. */
static void rt_fs_settle_sync(struct rt_context* ctx, struct rt_fs_state* st) {
    uint32_t i;
    if (rt_sd_settles(ctx, st)) {
        for (i = 0; i < RT_SD_SETTLE_POLLS; ++i) {
            rt_fs_build_status(st);
            st->settle_polls++;
            if (rt_fs_status_call(ctx, st, 0) < 0 || !rt_fs_card_writing(st)) break;
        }
    }
    st->card_busy = 0;
}

/* The asynchronous wait, as the FILE record's next request (pend.reserved
 * counts its CMD13s). 1 when one is in flight, 0 when the card is taken
 * as idle. */
static int rt_fs_settle_next(struct rt_context* ctx, struct rt_fs_state* st, int32_t last) {
    if (st->pend.reserved != 0 && (last < 0 || !rt_fs_card_writing(st))) {
        st->pend.reserved = 0;
        st->card_busy = 0;
        return 0;
    }
    if (!rt_sd_settles(ctx, st) || st->pend.reserved >= RT_SD_SETTLE_POLLS) {
        st->pend.reserved = 0;
        st->card_busy = 0;
        return 0;
    }
    rt_fs_build_status(st);
    st->settle_polls++;
    st->pend.reserved++;
    if (rt_fs_status_call(ctx, st, 1) < 0) {
        st->pend.reserved = 0;
        st->card_busy = 0;
        return 0;
    }
    return 1;
}

/* Performs the engine's pending transfer on the caller's thread and
 * records its status. */
static void rt_fs_transfer_sync(struct rt_context* ctx, struct rt_fs_state* st) {
    int32_t r;
    uint32_t msr;
    rt_fs_build_sendcmd(ctx, st);
    msr = rt_interrupts_off();
    rt_fs_mark_write(ctx, st);
    rt_interrupts_restore(msr);
    r = rt_fs_sendcmd_call(ctx, st, 0);
    st->transfers++;
    if (r < 0) st->failures++;
    st->req.fat.io_status = r < 0 ? r : 0;
    if (st->req.fat.io_write) rt_fs_settle_sync(ctx, st);
}

/* Issues the engine's pending transfer with the FILE record as its tag:
 * 1 when in flight (the completion continues), 0 when refused (the
 * status then fails the request on its next step). */
static int rt_fs_issue_async(struct rt_context* ctx, struct rt_fs_state* st) {
    int32_t r;
    uint32_t msr;
    rt_fs_build_sendcmd(ctx, st);
    msr = rt_interrupts_off();
    rt_fs_mark_write(ctx, st);
    r = rt_fs_sendcmd_call(ctx, st, 1);
    if (r < 0 && st->req.fat.io_write) st->card_busy = 0;
    rt_interrupts_restore(msr);
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
 * without I/O through a null IOS round trip.  The hook must never invoke
 * a game callback before its IOS API returned.  A transient immediate
 * IOS refusal is retried while the private slot remains claimed.  If all
 * bounded attempts fail, no callback is made and the save engine is
 * failed closed: an accepted request cannot safely be completed inline. */
static int rt_fs_deliver(struct rt_context* ctx, struct rt_fs_state* st, uint32_t cb, uint32_t ud, int32_t result) {
    struct rt_fs_pend* slot = 0;
    uint32_t msr;
    uint32_t i;
    if (cb == 0) return 1;
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
        /* IOS can reject a request before taking ownership.  Retrying
         * here does not duplicate a callback: only an accepted GETSTATUS
         * owns the slot and can reach rt_on_fs_complete. */
        for (i = 0; i < 3u; ++i) {
            if (rt_fs_getstatus_async(ctx, st, slot) >= 0) {
                st->deferred++;
                return 1;
            }
        }
        {
            const uint32_t clear_msr = rt_interrupts_off();
            slot->in_use = 0;
            rt_interrupts_restore(clear_msr);
        }
    }
    st->delivery_failures++;
    st->dead = 1;
    return 0;
}

/* Takes the engine for `ipc` when it is free with nothing queued ahead
 * (BEGUN: the request is begun, its classification in st->req), else
 * queues the arrival when allowed (QUEUED) or reports FULL. Interrupts
 * off: hooks run in both contexts. */
static int rt_fs_admit(struct rt_fs_state* st, uint32_t entry_index, const struct rtfs_ipc* ipc, int may_queue,
                       int job) {
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
        q->job = (uint32_t)job;
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

#define RT_FS_JOB_WAIT 0
#define RT_FS_JOB_ENDED 1
static int rt_fs_job_run(struct rt_context* ctx, struct rt_fs_state* st);
static void rt_fs_job_end(struct rt_context* ctx, struct rt_fs_state* st);

/* Starts the requests queued behind the engine once it is free: each
 * runs until its first transfer is in flight (the completion continues
 * it) or completes at once (its result deferred to the game's callback,
 * or, for the import job's request, resuming the job). */
static void rt_fs_start_queued(struct rt_context* ctx, struct rt_fs_state* st) {
    while (st->queue_count != 0 && !st->fs.busy) {
        struct rt_fs_queued q;
        int32_t r;
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
            r = RTFAT_EIO;
        } else if (st->req.classification == RTFS_PASS_THROUGH) {
            /* Ours when it arrived, not any more (the fd was forgotten
             * meanwhile): nothing can replay it now. */
            r = RTFAT_EINVAL;
        } else {
            if (st->req.classification == RTFS_NEEDS_IO) {
                st->pend.in_use = 1;
                st->pend.kind = RT_FS_OP_FILE;
                st->pend.job = q.job;
                if (rt_fs_advance(ctx, st, 0) == RT_FS_ADVANCE_ISSUED) {
                    if (!q.job) rt_fs_report(ctx, q.entry_index, &q.ipc, 0, 1);
                    return;
                }
                st->pend.in_use = 0;
                st->pend.job = 0;
            }
            r = st->req.result;
        }
        if (q.job) {
            st->job.last = r;
            if (rt_fs_job_run(ctx, st) == RT_FS_JOB_ENDED) {
                rt_fs_job_end(ctx, st);
                rt_fs_deliver(ctx, st, st->job.callback, st->job.user_data, st->job.result);
            }
            continue;
        }
        rt_fs_report(ctx, q.entry_index, &q.ipc, r, 0);
        rt_fs_deliver(ctx, st, q.ipc.callback, q.ipc.user_data, r);
    }
}

/* Waits for the engine on the game's thread, each turn sleeping in a
 * null SD round trip (rt_fs_wait_tick) so the holder can run, bounded.
 * 1 when it came free. */
static int rt_fs_wait(struct rt_context* ctx, struct rt_fs_state* st) {
    const uint32_t start = rt_fs_ticks();
    const volatile uint32_t* busy = &st->fs.busy;         /* changed by the IPC interrupt: */
    const volatile uint32_t* queued = &st->queue_count;   /* re-read every turn */
    /* A synchronous IOS request issued from the IPC interrupt can wait
     * for the very completion that needs this interrupt.  Refuse before
     * trying the GETSTATUS sleep in that context. */
    if (!rt_fs_in_thread()) {
        st->wait_timeouts++;
        return 0;
    }
    st->waits++;
    while (*busy || *queued != 0) {
        rt_fs_wait_tick(ctx, st);
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
    while (rt_fs_admit(st, 0, ipc, 0, 0) != RT_FS_ADMIT_BEGUN) {
        if (!rt_fs_wait(ctx, st)) return RTFAT_EIO;
    }
    if (st->req.classification == RTFS_PASS_THROUGH) return RTFAT_EINVAL;
    if (st->req.classification == RTFS_NEEDS_IO) rt_fs_advance(ctx, st, 1);
    {
        /* Async arrivals queued meanwhile (from the game's IPC callbacks)
         * start now: only a completion drains the queue otherwise. */
        const int32_t r = st->req.result;
        rt_fs_start_queued(ctx, st);
        return r;
    }
}

/* Whether the game's synchronous functions an import or a clone needs
 * are known. */
static int rt_fs_has_sync_originals(const struct rt_fs_state* st) {
    return st->open_sync != 0 && st->close_sync != 0 && st->read_sync != 0 && st->ioctl_sync != 0;
}

/* The importer owns two hidden, flat-directory names.  The staged file
 * receives every NAND byte before the visible destination is touched;
 * a pre-existing destination is moved to the hidden backup only for the
 * short commit window.  The names are short enough for the strict ISFS
 * filename adapter and are cleaned/recovered before each import. */
static int rt_fs_internal_path(const struct rt_fs_state* st, char* out, const char* name) {
    uint32_t i, at = st->fs.prefix_len;
    if (at + 1u >= RTFS_PATH_BYTES) return 0;
    for (i = 0; i < at; ++i) out[i] = st->fs.data_prefix[i];
    out[at++] = '/';
    for (i = 0; name[i] != 0; ++i) {
        if (at + 1u >= RTFS_PATH_BYTES) return 0;
        out[at++] = name[i];
    }
    out[at] = 0;
    return 1;
}

static int32_t rt_fs_card_delete(struct rt_context* ctx, struct rt_fs_state* st, int32_t fs_fd, const char* path,
                                 uint32_t hidden) {
    struct rtfs_ipc ipc;
    rt_zero_bytes((uint8_t*)&ipc, sizeof(ipc));
    ipc.command = RTFS_CMD_IOCTL;
    ipc.fd = fs_fd;
    ipc.hidden = hidden;
    ipc.args.ioctl.request = RTFS_IOCTL_DELETE;
    ipc.args.ioctl.in = (uint32_t)(uintptr_t)path;
    ipc.args.ioctl.in_len = RTFS_PATH_BYTES;
    return rt_fs_run_internal(ctx, st, &ipc);
}

static int32_t rt_fs_card_create(struct rt_context* ctx, struct rt_fs_state* st, int32_t fs_fd, const char* path,
                                 uint32_t hidden) {
    struct rtfs_ipc ipc;
    uint32_t i;
    rt_zero_bytes((uint8_t*)&st->attr, sizeof(st->attr));
    for (i = 0; i < RTFS_PATH_BYTES - 1u && path[i] != 0; ++i) st->attr.filepath[i] = path[i];
    st->attr.ownerperm = 3;
    st->attr.groupperm = 3;
    st->attr.otherperm = 3;
    rt_zero_bytes((uint8_t*)&ipc, sizeof(ipc));
    ipc.command = RTFS_CMD_IOCTL;
    ipc.fd = fs_fd;
    ipc.hidden = hidden;
    ipc.args.ioctl.request = RTFS_IOCTL_CREATEFILE;
    ipc.args.ioctl.in = (uint32_t)(uintptr_t)&st->attr;
    ipc.args.ioctl.in_len = sizeof(st->attr);
    return rt_fs_run_internal(ctx, st, &ipc);
}

static int32_t rt_fs_card_open(struct rt_context* ctx, struct rt_fs_state* st, const char* path, uint32_t mode,
                               uint32_t hidden) {
    struct rtfs_ipc ipc;
    rt_zero_bytes((uint8_t*)&ipc, sizeof(ipc));
    ipc.command = RTFS_CMD_OPEN;
    ipc.hidden = hidden;
    ipc.args.open.path = (uint32_t)(uintptr_t)path;
    ipc.args.open.mode = mode;
    return rt_fs_run_internal(ctx, st, &ipc);
}

/* `paths` is two adjacent RTFS_PATH_BYTES strings, source then destination. */
static int32_t rt_fs_card_rename(struct rt_context* ctx, struct rt_fs_state* st, int32_t fs_fd, char* paths,
                                 uint32_t hidden) {
    struct rtfs_ipc ipc;
    rt_zero_bytes((uint8_t*)&ipc, sizeof(ipc));
    ipc.command = RTFS_CMD_IOCTL;
    ipc.fd = fs_fd;
    ipc.hidden = hidden;
    ipc.args.ioctl.request = RTFS_IOCTL_RENAME;
    ipc.args.ioctl.in = (uint32_t)(uintptr_t)paths;
    ipc.args.ioctl.in_len = 2u * RTFS_PATH_BYTES;
    return rt_fs_run_internal(ctx, st, &ipc);
}

/* The import's hidden names, spelled out: string literals would land in
 * .rodata and break the blob's position independence. */
static void rt_stage_name(char* out) {
    out[0] = '.'; out[1] = 'r'; out[2] = 'w'; out[3] = 's'; out[4] = 't';
    out[5] = 'a'; out[6] = 'g'; out[7] = 'e'; out[8] = '.'; out[9] = 't';
    out[10] = 'm'; out[11] = 'p'; out[12] = 0;
}
static void rt_backup_name(char* out) {
    out[0] = '.'; out[1] = 'r'; out[2] = 'w'; out[3] = 'b'; out[4] = 'a';
    out[5] = 'c'; out[6] = 'k'; out[7] = '.'; out[8] = 't'; out[9] = 'm';
    out[10] = 'p'; out[11] = 0;
}

/* Copies a NAND file through a hidden stage.  Its old card destination
 * is not moved until every byte and both close calls completed.  During
 * commit it is held under a hidden backup name, so a failed stage rename
 * can restore it.  An interrupted backup is recovered before the next
 * import: when the visible destination exists it wins; otherwise the
 * backup is restored. */
static int32_t rt_fs_copy_in(struct rt_context* ctx, struct rt_fs_state* st, int32_t fs_fd, const char* src,
                             const char* dst, int move) {
    struct rtfs_ipc ipc;
    /* State-resident, not stack: the engine addresses path buffers through
     * 32-bit fields, and the host stack is not 32-bit addressable. */
    char* stage = st->copy_stage;
    char* backup = st->copy_backup;
    char* paths = st->copy_paths;
    char stage_name[13], backup_name[12];
    int32_t src_fd, fake_fd = -1, r, close_r, old_fd;
    uint32_t size = 0, done, i, have_backup = 0;
    rt_stage_name(stage_name);
    rt_backup_name(backup_name);
    if (!rt_fs_internal_path(st, stage, stage_name) || !rt_fs_internal_path(st, backup, backup_name)) {
        return RTFAT_EINVAL;
    }
    src_fd = rt_fs_open_sync(st, (uint32_t)(uintptr_t)src, 1);
    if (src_fd < 0) return src_fd;
    rt_flush_range((uintptr_t)st->stats, sizeof(st->stats));
    r = rt_fs_ioctl_sync(st, src_fd, RTFS_IOCTL_GETFILESTATS, 0, 0, (uint32_t)(uintptr_t)st->stats, 8);
    if (r >= 0) {
        size = st->stats[0];
        /* A stale stage was never committed. It is always disposable. */
        r = rt_fs_card_delete(ctx, st, fs_fd, stage, 1);
        if (r == RTFAT_ENOENT) r = RTFAT_OK;
    }
    if (r >= 0) {
        /* Recover an interrupted commit's backup deterministically. */
        old_fd = rt_fs_card_open(ctx, st, dst, 1, 0);
        if (old_fd >= 0) {
            rt_zero_bytes((uint8_t*)&ipc, sizeof(ipc)); ipc.command = RTFS_CMD_CLOSE; ipc.fd = old_fd;
            r = rt_fs_run_internal(ctx, st, &ipc);
            if (r >= 0) {
                r = rt_fs_card_delete(ctx, st, fs_fd, backup, 1);
                if (r == RTFAT_ENOENT) r = RTFAT_OK;
            }
        } else if (old_fd == RTFAT_ENOENT) {
            for (i = 0; i < RTFS_PATH_BYTES; ++i) { paths[i] = backup[i]; paths[RTFS_PATH_BYTES + i] = dst[i]; }
            r = rt_fs_card_rename(ctx, st, fs_fd, paths, 2);
            if (r == RTFAT_ENOENT) r = RTFAT_OK;
        } else r = old_fd;
    }
    if (r >= 0) r = rt_fs_card_create(ctx, st, fs_fd, stage, 1);
    if (r >= 0) r = fake_fd = rt_fs_card_open(ctx, st, stage, 2, 1);
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
        close_r = rt_fs_run_internal(ctx, st, &ipc);
        if (r >= 0 && close_r < 0) r = close_r;
    }
    close_r = rt_fs_close_sync(st, src_fd);
    if (r >= 0 && close_r < 0) r = close_r;
    if (r < 0) {
        /* The old destination has not moved. The stage is hidden and is
         * removed when its close succeeded, otherwise recovered next run. */
        if (fake_fd >= 0 && close_r >= 0) rt_fs_card_delete(ctx, st, fs_fd, stage, 1);
        return r;
    }
    /* Commit: keep a recoverable old destination until the staged one is
     * visible. `hidden=1` hides the backup; `hidden=2` clears the hidden
     * bit when the stage becomes the game file. */
    old_fd = rt_fs_card_open(ctx, st, dst, 1, 0);
    if (old_fd >= 0) {
        rt_zero_bytes((uint8_t*)&ipc, sizeof(ipc)); ipc.command = RTFS_CMD_CLOSE; ipc.fd = old_fd;
        r = rt_fs_run_internal(ctx, st, &ipc);
        if (r < 0) return r;
        for (i = 0; i < RTFS_PATH_BYTES; ++i) { paths[i] = dst[i]; paths[RTFS_PATH_BYTES + i] = backup[i]; }
        r = rt_fs_card_rename(ctx, st, fs_fd, paths, 1);
        if (r < 0) return r;
        have_backup = 1;
    } else if (old_fd != RTFAT_ENOENT) return old_fd;
    for (i = 0; i < RTFS_PATH_BYTES; ++i) { paths[i] = stage[i]; paths[RTFS_PATH_BYTES + i] = dst[i]; }
    r = rt_fs_card_rename(ctx, st, fs_fd, paths, 2);
    if (r < 0) {
        if (have_backup) {
            for (i = 0; i < RTFS_PATH_BYTES; ++i) { paths[i] = backup[i]; paths[RTFS_PATH_BYTES + i] = dst[i]; }
            rt_fs_card_rename(ctx, st, fs_fd, paths, 2);
        }
        return r;
    }
    if (have_backup) {
        r = rt_fs_card_delete(ctx, st, fs_fd, backup, 1);
        if (r < 0) return r; /* source remains until old backup cleanup is known */
    }
    if (!move) return RTFAT_OK;
    /* It is a rename, not a best-effort copy: a failed NAND delete is
     * reported while both the source and the new card destination remain. */
    return rt_fs_ioctl_sync(st, fs_fd, RTFS_IOCTL_DELETE, (uint32_t)(uintptr_t)src, RTFS_PATH_BYTES, 0, 0);
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

/* ---- the asynchronous import job (rt_hook.h) ---- */

static int rt_fs_has_async_originals(const struct rt_context* ctx, const struct rt_fs_state* st) {
    return st->open_async != 0 && st->close_async != 0 && st->read_async != 0 && ctx->di_read_entry != 0;
}

/* Submits one of the job's engine requests: ENDED with job.last set when
 * it completed at once, WAIT when its first transfer is in flight or it
 * queued behind the game's requests (a completion resumes the job). */
static int rt_fs_job_engine(struct rt_context* ctx, struct rt_fs_state* st, const struct rtfs_ipc* ipc) {
    struct rt_fs_job* j = &st->job;
    int admit;
    if (st->dead) {
        j->last = RTFAT_EIO;
        return RT_FS_JOB_ENDED;
    }
    admit = rt_fs_admit(st, 0, ipc, 1, 1);
    if (admit == RT_FS_ADMIT_QUEUED) {
        st->queued++;
        return RT_FS_JOB_WAIT;
    }
    if (admit == RT_FS_ADMIT_FULL) {
        j->last = RTFAT_EIO;
        return RT_FS_JOB_ENDED;
    }
    if (st->req.classification == RTFS_PASS_THROUGH) {
        j->last = RTFAT_EINVAL;
        return RT_FS_JOB_ENDED;
    }
    if (st->req.classification == RTFS_NEEDS_IO) {
        st->pend.in_use = 1;
        st->pend.kind = RT_FS_OP_FILE;
        st->pend.job = 1;
        if (rt_fs_advance(ctx, st, 0) == RT_FS_ADVANCE_ISSUED) return RT_FS_JOB_WAIT;
        st->pend.in_use = 0;
        st->pend.job = 0;
    }
    j->last = st->req.result;
    return RT_FS_JOB_ENDED;
}

/* The job's NAND request just issued: WAIT when in flight, else its
 * refusal is the answer the job goes on with. */
static int rt_fs_job_issued(struct rt_fs_job* j, int32_t r) {
    if (r >= 0) return RT_FS_JOB_WAIT;
    j->tag.in_use = 0;
    j->last = r;
    return RT_FS_JOB_ENDED;
}

static void rt_fs_job_fail(struct rt_fs_job* j, int32_t r) {
    j->result = r;
    j->phase = RT_FS_JOB_CLEANUP;
}

/* Runs the job from its stage with job.last, until a request is in
 * flight (WAIT) or the job is over (ENDED: job.result). Each stage sets
 * the next before issuing, so a completion resumes at the right place;
 * a request that completes at once loops on here. */
static int rt_fs_job_run(struct rt_context* ctx, struct rt_fs_state* st) {
    struct rt_fs_job* j = &st->job;
    struct rtfs_ipc ipc;
    for (;;) {
        int32_t r, fd;
        uint32_t i;
        rt_zero_bytes((uint8_t*)&ipc, sizeof(ipc));
        switch (j->phase) {
            case RT_FS_JOB_OPENED:
                if (j->last < 0) { rt_fs_job_fail(j, j->last); break; }
                j->src_fd = j->last;
                rt_flush_range((uintptr_t)st->stats, sizeof(st->stats));
                j->phase = RT_FS_JOB_STATTED;
                j->tag.in_use = 1;
                r = rt_fs_ioctl_async(ctx, st, j->src_fd, RTFS_IOCTL_GETFILESTATS, 0, 0, (uint32_t)(uintptr_t)st->stats, 8,
                                      &j->tag);
                if (rt_fs_job_issued(j, r) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                break;
            case RT_FS_JOB_STATTED:
                if (j->last < 0) { rt_fs_job_fail(j, j->last); break; }
                j->size = st->stats[0];
                j->done = 0;
                /* A previous interrupted stage is disposable.  A stale
                 * backup is recovered in the following states. */
                ipc.command = RTFS_CMD_IOCTL;
                ipc.fd = j->fs_fd;
                ipc.hidden = 1;
                ipc.args.ioctl.request = RTFS_IOCTL_DELETE;
                ipc.args.ioctl.in = (uint32_t)(uintptr_t)j->stage;
                ipc.args.ioctl.in_len = RTFS_PATH_BYTES;
                j->phase = RT_FS_JOB_STAGE_CLEANED;
                if (rt_fs_job_engine(ctx, st, &ipc) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                break;
            case RT_FS_JOB_STAGE_CLEANED:
                if (j->last < 0 && j->last != RTFAT_ENOENT) { rt_fs_job_fail(j, j->last); break; }
                ipc.command = RTFS_CMD_OPEN;
                ipc.args.open.path = (uint32_t)(uintptr_t)j->dst;
                ipc.args.open.mode = 1;
                j->phase = RT_FS_JOB_RECOVER_DST_OPENED;
                if (rt_fs_job_engine(ctx, st, &ipc) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                break;
            case RT_FS_JOB_RECOVER_DST_OPENED:
                if (j->last >= 0) {
                    j->fake_fd = j->last;
                    ipc.command = RTFS_CMD_CLOSE;
                    ipc.fd = j->fake_fd;
                    j->phase = RT_FS_JOB_RECOVER_DST_CLOSED;
                    if (rt_fs_job_engine(ctx, st, &ipc) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                    break;
                }
                if (j->last != RTFAT_ENOENT) { rt_fs_job_fail(j, j->last); break; }
                for (i = 0; i < RTFS_PATH_BYTES; ++i) {
                    j->paths[i] = j->backup[i]; j->paths[RTFS_PATH_BYTES + i] = j->dst[i];
                }
                ipc.command = RTFS_CMD_IOCTL;
                ipc.fd = j->fs_fd;
                ipc.hidden = 2;
                ipc.args.ioctl.request = RTFS_IOCTL_RENAME;
                ipc.args.ioctl.in = (uint32_t)(uintptr_t)j->paths;
                ipc.args.ioctl.in_len = 2u * RTFS_PATH_BYTES;
                j->phase = RT_FS_JOB_RECOVER_BACKUP_DONE;
                if (rt_fs_job_engine(ctx, st, &ipc) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                break;
            case RT_FS_JOB_RECOVER_DST_CLOSED:
                if (j->last < 0) { rt_fs_job_fail(j, j->last); break; }
                j->fake_fd = -1;
                ipc.command = RTFS_CMD_IOCTL;
                ipc.fd = j->fs_fd;
                ipc.hidden = 1;
                ipc.args.ioctl.request = RTFS_IOCTL_DELETE;
                ipc.args.ioctl.in = (uint32_t)(uintptr_t)j->backup;
                ipc.args.ioctl.in_len = RTFS_PATH_BYTES;
                j->phase = RT_FS_JOB_RECOVER_BACKUP_DONE;
                if (rt_fs_job_engine(ctx, st, &ipc) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                break;
            case RT_FS_JOB_RECOVER_BACKUP_DONE:
                if (j->last < 0 && j->last != RTFAT_ENOENT) { rt_fs_job_fail(j, j->last); break; }
                rt_zero_bytes((uint8_t*)&st->attr, sizeof(st->attr));
                for (i = 0; i < RTFS_PATH_BYTES - 1 && j->stage[i] != 0; ++i) st->attr.filepath[i] = j->stage[i];
                st->attr.ownerperm = 3;
                st->attr.groupperm = 3;
                st->attr.otherperm = 3;
                ipc.command = RTFS_CMD_IOCTL;
                ipc.fd = j->fs_fd;
                ipc.hidden = 1;
                ipc.args.ioctl.request = RTFS_IOCTL_CREATEFILE;
                ipc.args.ioctl.in = (uint32_t)(uintptr_t)&st->attr;
                ipc.args.ioctl.in_len = sizeof(st->attr);
                j->phase = RT_FS_JOB_STAGE_CREATED;
                if (rt_fs_job_engine(ctx, st, &ipc) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                break;
            case RT_FS_JOB_STAGE_CREATED:
                if (j->last < 0) { rt_fs_job_fail(j, j->last); break; }
                j->stage_created = 1;
                ipc.command = RTFS_CMD_OPEN;
                ipc.hidden = 1;
                ipc.args.open.path = (uint32_t)(uintptr_t)j->stage;
                ipc.args.open.mode = 2;
                j->phase = RT_FS_JOB_STAGE_OPENED;
                if (rt_fs_job_engine(ctx, st, &ipc) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                break;
            case RT_FS_JOB_STAGE_OPENED:
                if (j->last < 0) { rt_fs_job_fail(j, j->last); break; }
                j->fake_fd = j->last;
                j->phase = RT_FS_JOB_CHUNK;
                break;
            case RT_FS_JOB_CHUNK:
                if (j->done < j->size) {
                    /* A piece off NAND into the import buffer (flushed first,
                     * so no stale line shadows the DMA). */
                    uint32_t n = j->size - j->done;
                    if (n > RT_FS_IMPORT_BYTES) n = RT_FS_IMPORT_BYTES;
                    j->chunk = n;
                    rt_flush_range((uintptr_t)st->import, n);
                    j->phase = RT_FS_JOB_READ_DONE;
                    j->tag.in_use = 1;
                    r = rt_fs_read_async(st, j->src_fd, (uint32_t)(uintptr_t)st->import, n, &j->tag);
                    if (rt_fs_job_issued(j, r) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                    break;
                }
                fd = j->fake_fd;
                j->fake_fd = -1;
                ipc.command = RTFS_CMD_CLOSE;
                ipc.fd = fd;
                j->phase = RT_FS_JOB_STAGE_CLOSED;
                if (rt_fs_job_engine(ctx, st, &ipc) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                break;
            case RT_FS_JOB_READ_DONE:
                if (j->last < 0) { rt_fs_job_fail(j, j->last); break; }
                if (j->last != (int32_t)j->chunk) { rt_fs_job_fail(j, RTFAT_EIO); break; }
                ipc.command = RTFS_CMD_WRITE;
                ipc.fd = j->fake_fd;
                ipc.args.readwrite.data = (uint32_t)(uintptr_t)st->import;
                ipc.args.readwrite.length = j->chunk;
                j->phase = RT_FS_JOB_WRITTEN;
                if (rt_fs_job_engine(ctx, st, &ipc) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                break;
            case RT_FS_JOB_WRITTEN:
                if (j->last < 0) { rt_fs_job_fail(j, j->last); break; }
                if (j->last != (int32_t)j->chunk) { rt_fs_job_fail(j, RTFAT_ENOSPC); break; }
                j->done += j->chunk;
                j->phase = RT_FS_JOB_CHUNK;
                break;
            case RT_FS_JOB_STAGE_CLOSED:
                if (j->last < 0) { rt_fs_job_fail(j, j->last); break; }
                j->fake_fd = -1;
                fd = j->src_fd;
                j->phase = RT_FS_JOB_SRC_CLOSED;
                j->tag.in_use = 1;
                r = rt_fs_close_async(st, fd, &j->tag);
                if (rt_fs_job_issued(j, r) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                break;
            case RT_FS_JOB_SRC_CLOSED:
                if (j->last < 0) { rt_fs_job_fail(j, j->last); break; }
                j->src_fd = -1;
                ipc.command = RTFS_CMD_OPEN;
                ipc.args.open.path = (uint32_t)(uintptr_t)j->dst;
                ipc.args.open.mode = 1;
                j->phase = RT_FS_JOB_COMMIT_DST_OPENED;
                if (rt_fs_job_engine(ctx, st, &ipc) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                break;
            case RT_FS_JOB_COMMIT_DST_OPENED:
                if (j->last >= 0) {
                    j->fake_fd = j->last;
                    ipc.command = RTFS_CMD_CLOSE;
                    ipc.fd = j->fake_fd;
                    j->phase = RT_FS_JOB_COMMIT_DST_CLOSED;
                    if (rt_fs_job_engine(ctx, st, &ipc) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                    break;
                }
                if (j->last != RTFAT_ENOENT) { rt_fs_job_fail(j, j->last); break; }
                for (i = 0; i < RTFS_PATH_BYTES; ++i) {
                    j->paths[i] = j->stage[i]; j->paths[RTFS_PATH_BYTES + i] = j->dst[i];
                }
                ipc.command = RTFS_CMD_IOCTL; ipc.fd = j->fs_fd; ipc.hidden = 2;
                ipc.args.ioctl.request = RTFS_IOCTL_RENAME;
                ipc.args.ioctl.in = (uint32_t)(uintptr_t)j->paths; ipc.args.ioctl.in_len = 2u * RTFS_PATH_BYTES;
                j->phase = RT_FS_JOB_STAGE_MOVED;
                if (rt_fs_job_engine(ctx, st, &ipc) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                break;
            case RT_FS_JOB_COMMIT_DST_CLOSED:
                if (j->last < 0) { rt_fs_job_fail(j, j->last); break; }
                j->fake_fd = -1;
                for (i = 0; i < RTFS_PATH_BYTES; ++i) {
                    j->paths[i] = j->dst[i]; j->paths[RTFS_PATH_BYTES + i] = j->backup[i];
                }
                ipc.command = RTFS_CMD_IOCTL; ipc.fd = j->fs_fd; ipc.hidden = 1;
                ipc.args.ioctl.request = RTFS_IOCTL_RENAME;
                ipc.args.ioctl.in = (uint32_t)(uintptr_t)j->paths; ipc.args.ioctl.in_len = 2u * RTFS_PATH_BYTES;
                j->phase = RT_FS_JOB_BACKUP_MOVED;
                if (rt_fs_job_engine(ctx, st, &ipc) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                break;
            case RT_FS_JOB_BACKUP_MOVED:
                if (j->last < 0) { rt_fs_job_fail(j, j->last); break; }
                j->backup_moved = 1;
                for (i = 0; i < RTFS_PATH_BYTES; ++i) {
                    j->paths[i] = j->stage[i]; j->paths[RTFS_PATH_BYTES + i] = j->dst[i];
                }
                ipc.command = RTFS_CMD_IOCTL; ipc.fd = j->fs_fd; ipc.hidden = 2;
                ipc.args.ioctl.request = RTFS_IOCTL_RENAME;
                ipc.args.ioctl.in = (uint32_t)(uintptr_t)j->paths; ipc.args.ioctl.in_len = 2u * RTFS_PATH_BYTES;
                j->phase = RT_FS_JOB_STAGE_MOVED;
                if (rt_fs_job_engine(ctx, st, &ipc) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                break;
            case RT_FS_JOB_STAGE_MOVED:
                if (j->last < 0) { rt_fs_job_fail(j, j->last); break; }
                j->stage_created = 0;
                if (j->backup_moved) {
                    ipc.command = RTFS_CMD_IOCTL; ipc.fd = j->fs_fd; ipc.hidden = 1;
                    ipc.args.ioctl.request = RTFS_IOCTL_DELETE;
                    ipc.args.ioctl.in = (uint32_t)(uintptr_t)j->backup; ipc.args.ioctl.in_len = RTFS_PATH_BYTES;
                    j->phase = RT_FS_JOB_BACKUP_REMOVED;
                    if (rt_fs_job_engine(ctx, st, &ipc) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                    break;
                }
                rt_flush_range((uintptr_t)j->src, sizeof(j->src));
                j->phase = RT_FS_JOB_SRC_DELETED; j->tag.in_use = 1;
                r = rt_fs_ioctl_async(ctx, st, j->fs_fd, RTFS_IOCTL_DELETE, (uint32_t)(uintptr_t)j->src, RTFS_PATH_BYTES,
                                      0, 0, &j->tag);
                if (rt_fs_job_issued(j, r) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                break;
            case RT_FS_JOB_BACKUP_REMOVED:
                if (j->last < 0) { j->result = j->last; j->phase = RT_FS_JOB_DONE; break; }
                j->backup_moved = 0;
                rt_flush_range((uintptr_t)j->src, sizeof(j->src));
                j->phase = RT_FS_JOB_SRC_DELETED; j->tag.in_use = 1;
                r = rt_fs_ioctl_async(ctx, st, j->fs_fd, RTFS_IOCTL_DELETE, (uint32_t)(uintptr_t)j->src, RTFS_PATH_BYTES,
                                      0, 0, &j->tag);
                if (rt_fs_job_issued(j, r) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                break;
            case RT_FS_JOB_SRC_DELETED:
                j->result = j->last < 0 ? j->last : RTFAT_OK;
                j->phase = RT_FS_JOB_DONE;
                break;
            case RT_FS_JOB_CLEANUP:
                /* The visible destination is untouched until commit. */
                if (j->fake_fd >= 0) {
                    fd = j->fake_fd;
                    j->fake_fd = -1;
                    ipc.command = RTFS_CMD_CLOSE;
                    ipc.fd = fd;
                    if (rt_fs_job_engine(ctx, st, &ipc) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                    break;
                }
                if (j->stage_created) {
                    j->stage_created = 0;
                    ipc.command = RTFS_CMD_IOCTL;
                    ipc.fd = j->fs_fd;
                    ipc.hidden = 1;
                    ipc.args.ioctl.request = RTFS_IOCTL_DELETE;
                    ipc.args.ioctl.in = (uint32_t)(uintptr_t)j->stage;
                    ipc.args.ioctl.in_len = RTFS_PATH_BYTES;
                    if (rt_fs_job_engine(ctx, st, &ipc) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                    break;
                }
                if (j->src_fd >= 0) {
                    fd = j->src_fd;
                    j->src_fd = -1;
                    j->tag.in_use = 1;
                    r = rt_fs_close_async(st, fd, &j->tag);
                    if (rt_fs_job_issued(j, r) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                    break;
                }
                if (j->backup_moved) {
                    j->backup_moved = 0;
                    for (i = 0; i < RTFS_PATH_BYTES; ++i) {
                        j->paths[i] = j->backup[i]; j->paths[RTFS_PATH_BYTES + i] = j->dst[i];
                    }
                    ipc.command = RTFS_CMD_IOCTL; ipc.fd = j->fs_fd; ipc.hidden = 2;
                    ipc.args.ioctl.request = RTFS_IOCTL_RENAME;
                    ipc.args.ioctl.in = (uint32_t)(uintptr_t)j->paths; ipc.args.ioctl.in_len = 2u * RTFS_PATH_BYTES;
                    if (rt_fs_job_engine(ctx, st, &ipc) == RT_FS_JOB_WAIT) return RT_FS_JOB_WAIT;
                    break;
                }
                j->phase = RT_FS_JOB_DONE;
                break;
            default:
                return RT_FS_JOB_ENDED;
        }
    }
}

/* The job is over: counted, its C line printed. The caller hands the
 * result to the game's callback (a tail call from a completion, or a
 * null round trip). */
static void rt_fs_job_end(struct rt_context* ctx, struct rt_fs_state* st) {
    struct rt_fs_job* j = &st->job;
    j->active = 0;
    j->phase = RT_FS_JOB_IDLE;
    if (j->result < 0) st->import_failures++;
    rt_fs_report_done(ctx, j->result);
}

/* Resumes the job with a completion's result. When it ends, the game's
 * callback pair and the rename's result go out through the completion's
 * tail call. */
static void rt_fs_job_resume(struct rt_context* ctx, struct rt_fs_state* st, int32_t last, int32_t* result,
                             uintptr_t* callback, uintptr_t* user_data) {
    struct rt_fs_job* j = &st->job;
    j->last = last;
    if (rt_fs_job_run(ctx, st) != RT_FS_JOB_ENDED) return;
    rt_fs_job_end(ctx, st);
    *result = j->result;
    *callback = (uintptr_t)j->callback;
    *user_data = (uintptr_t)j->user_data;
}

/* Starts the asynchronous import of the rename `ipc` (src -> dst) on the
 * game's /dev/fs fd it came on. 0 when it cannot (a job is running, or
 * the async originals are unknown: the caller falls back), 1 when it is
 * in flight (the game's callback follows from a completion), 2 when it
 * ended at once (job.result, ended: the caller delivers). */
static int rt_fs_job_start(struct rt_context* ctx, struct rt_fs_state* st, uint32_t entry_index,
                           const struct rtfs_ipc* ipc, const char* src, const char* dst) {
    struct rt_fs_job* j = &st->job;
    uint32_t i;
    int32_t r;
    if (j->active || !rt_fs_has_async_originals(ctx, st)) return 0;
    rt_zero_bytes((uint8_t*)j, sizeof(*j));
    j->active = 1;
    j->tag.kind = RT_FS_OP_JOB;
    j->fs_fd = ipc->fd;
    j->src_fd = -1;
    j->fake_fd = -1;
    j->callback = ipc->callback;
    j->user_data = ipc->user_data;
    j->entry_index = entry_index;
    j->ipc = *ipc;
    for (i = 0; i < RTFS_PATH_BYTES - 1 && src[i] != 0; ++i) j->src[i] = src[i];
    for (i = 0; i < RTFS_PATH_BYTES - 1 && dst[i] != 0; ++i) j->dst[i] = dst[i];
    {
        char stage_name[13], backup_name[12];
        rt_stage_name(stage_name);
        rt_backup_name(backup_name);
        if (!rt_fs_internal_path(st, j->stage, stage_name) ||
            !rt_fs_internal_path(st, j->backup, backup_name)) {
            j->active = 0;
            return 0;
        }
    }
    st->imports++;
    st->import_jobs++;
    rt_flush_range((uintptr_t)j->src, sizeof(j->src));
    j->phase = RT_FS_JOB_OPENED;
    j->tag.in_use = 1;
    r = rt_fs_open_async(st, j->src, 1, &j->tag);
    if (rt_fs_job_issued(j, r) == RT_FS_JOB_WAIT) return 1;
    if (rt_fs_job_run(ctx, st) == RT_FS_JOB_WAIT) return 1;
    rt_fs_job_end(ctx, st);
    return 2;
}

/* "K:<path>:<result>\n" over the Gecko for each file a clone copies. */
static void rt_fs_report_clone(struct rt_context* ctx, const char* path, int32_t result) {
    uint32_t i, msr;
    if (!(ctx->flags & RT_FLAG_GECKO)) return;
    msr = rt_interrupts_off();
    rt_gecko_putc(ctx, 'K');
    rt_gecko_putc(ctx, ':');
    for (i = 0; i < RTFS_PATH_BYTES && path[i] != 0; ++i) rt_gecko_putc(ctx, (uint32_t)(uint8_t)path[i]);
    rt_gecko_putc(ctx, ':');
    rt_gecko_hex(ctx, (uint32_t)result);
    rt_gecko_putc(ctx, '\n');
    rt_interrupts_restore(msr);
}

/* The clone ran to its end: the loader's marker (rt_hook.h) goes so the
 * next launch does not repeat it. An internal delete of a hidden entry;
 * `fs_fd` is the fd the engine classifies as /dev/fs. */
static void rt_fs_clone_unmark(struct rt_context* ctx, struct rt_fs_state* st, int32_t fs_fd) {
    struct rtfs_ipc ipc;
    char* p = st->path;
    uint32_t at = st->fs.prefix_len, k;
    int32_t r;
    if (at + 13 > RTFS_PATH_BYTES) return;
    rt_zero_bytes((uint8_t*)p, RTFS_PATH_BYTES);
    for (k = 0; k < at; ++k) p[k] = st->fs.data_prefix[k];
    p[at + 0] = '/'; p[at + 1] = 'r'; p[at + 2] = 'i'; p[at + 3] = 'f'; p[at + 4] = 't'; p[at + 5] = 'w';
    p[at + 6] = 'i'; p[at + 7] = 'i'; p[at + 8] = '.'; p[at + 9] = 'c'; p[at + 10] = 'l'; p[at + 11] = 'n';
    rt_zero_bytes((uint8_t*)&ipc, sizeof(ipc));
    ipc.command = RTFS_CMD_IOCTL;
    ipc.fd = fs_fd;
    ipc.hidden = 1;
    ipc.args.ioctl.request = RTFS_IOCTL_DELETE;
    ipc.args.ioctl.in = (uint32_t)(uintptr_t)p;
    ipc.args.ioctl.in_len = RTFS_PATH_BYTES;
    r = rt_fs_run_internal(ctx, st, &ipc);
    if (r >= 0) st->clone_marker++;
    rt_fs_report_clone(ctx, p, r);
}

/* <savegame clone> (rt_hook.h): the NAND data directory copied into the
 * folder, once, on the game's thread. Its own /dev/fs fd, opened and
 * closed here; the listing through the game's synchronous IOS_Ioctlv
 * (ReadDir: the path and a count in, the names one after another, each
 * NUL-terminated, in a buffer of 13 bytes per name, and the count out);
 * each name copied by rt_fs_copy_in with the same path on both sides
 * (NAND through the original IOS_Open, the card through the engine,
 * which classifies by the game's /dev/fs fd once that is known). */
static void rt_fs_clone(struct rt_context* ctx, struct rt_fs_state* st) {
    int32_t fd, engine_fd, r;
    uint32_t count, i, n;
    int listed = 0;
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
    engine_fd = st->fs.fs_fd >= 0 ? st->fs.fs_fd : fd;
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
    if (r < 0 && r != RTFAT_ENOENT) st->clone_failures++;
    else listed = 1;
    rt_fs_report_clone(ctx, p, r < 0 ? r : (int32_t)count);
    if (count > RT_FS_CLONE_MAX) {
        /* More names than the listing holds: the ones past the cap are
         * not copied, and each is a failure the counters show. */
        st->clone_failures += count - RT_FS_CLONE_MAX;
        count = RT_FS_CLONE_MAX;
    }
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
            listed = 0;
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
        r = rt_fs_copy_in(ctx, st, engine_fd, p, p, 0);
        if (r < 0) st->clone_failures++;
        else st->clone_files++;
        rt_fs_report_clone(ctx, p, r);
    }
    if (listed) rt_fs_clone_unmark(ctx, st, engine_fd);
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
        /* Call the saved original ourselves so a rejected close cannot
         * discard the only real /dev/fs descriptor we know. */
        int32_t r;
        if (st->close_sync == 0) return 0;
        r = rt_fs_close_sync(st, ipc.fd);
        if (r >= 0 && st->fs.fs_fd == ipc.fd) st->fs.fs_fd = -1;
        *result = (uint32_t)r;
        return 1;
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
    while (rt_fs_admit(st, entry_index, &ipc, 0, 0) != RT_FS_ADMIT_BEGUN) {
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
    rt_fs_start_queued(ctx, st); /* async arrivals queued behind this one */
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
            slot->fd = -1;
        }
        rt_interrupts_restore(msr);
        if (slot == 0) return 0; /* every slot taken: replayed unobserved */
        /* Call the saved original explicitly: a direct tail replay gives
         * us no way to release this slot when IOS rejects immediately. */
        if (st->open_async == 0) {
            const uint32_t clear_msr = rt_interrupts_off();
            slot->in_use = 0;
            rt_interrupts_restore(clear_msr);
            return 0;
        }
        {
            const int32_t r = rt_fs_open_async(st, (const char*)(uintptr_t)ipc.args.open.path, ipc.args.open.mode, slot);
            if (r >= 0) {
                *result = 0;
                return 1;
            }
            {
                const uint32_t clear_msr = rt_interrupts_off();
                slot->in_use = 0;
                rt_interrupts_restore(clear_msr);
            }
            *result = (uint32_t)r;
            return 1;
        }
    }
    if (ipc.command == RTFS_CMD_CLOSE && ipc.fd >= 0 && ipc.fd == st->fs.fs_fd) {
        struct rt_fs_pend* slot = 0;
        const uint32_t msr = rt_interrupts_off();
        for (i = 0; i < RT_FS_SNOOPS && slot == 0; ++i) {
            if (!st->snoop[i].in_use) slot = &st->snoop[i];
        }
        if (slot != 0) {
            slot->in_use = 1;
            slot->kind = RT_FS_OP_CLOSE_SNOOP;
            slot->callback = ipc.callback;
            slot->user_data = ipc.user_data;
            slot->fd = ipc.fd;
        }
        rt_interrupts_restore(msr);
        if (slot == 0 || st->close_async == 0) {
            if (slot != 0) {
                const uint32_t clear_msr = rt_interrupts_off();
                slot->in_use = 0;
                rt_interrupts_restore(clear_msr);
            }
            return 0;
        }
        {
            const int32_t r = rt_fs_close_async(st, ipc.fd, slot);
            if (r >= 0) {
                *result = 0;
                return 1;
            }
            {
                const uint32_t clear_msr = rt_interrupts_off();
                slot->in_use = 0;
                rt_interrupts_restore(clear_msr);
            }
            *result = (uint32_t)r;
            return 1;
        }
    }
    {
        const char* src;
        const char* dst;
        const int kind = rt_fs_rename_kind(st, &ipc, &src, &dst);
        if (kind != 0) {
            /* An import: the asynchronous job when the game's async
             * originals are known; else the synchronous import on a
             * thread, and a refusal from an IPC callback. */
            int32_t r = RTFAT_EACCESS;
            int started = 0;
            rtfs_learn_fs_fd(&st->fs, ipc.fd);
            ctx->fs_hijacked++;
            if (kind > 0) {
                started = rt_fs_job_start(ctx, st, entry_index, &ipc, src, dst);
                if (started == 2) r = st->job.result;
                else if (started == 0 && rt_fs_in_thread()) r = rt_fs_import(ctx, st, ipc.fd, src, dst);
                else if (started == 0) st->import_refused++;
            } else {
                st->import_refused++;
            }
            if (started == 1) {
                rt_fs_report(ctx, entry_index, &ipc, 0, 1);
                *result = 0;
                return 1;
            }
            /* Accepted like any other async answer: the result, refusal
             * included, goes to the callback (or EIO when no delivery can
             * be scheduled), never back through the call. */
            rt_fs_report(ctx, entry_index, &ipc, r, 0);
            *result = rt_fs_deliver(ctx, st, ipc.callback, ipc.user_data, r) ? 0u : (uint32_t)RTFAT_EIO;
            return 1;
        }
    }
    rt_fs_probe(st, &ipc, &classification);
    if (classification == RTFS_PASS_THROUGH) return 0;
    ctx->fs_hijacked++;
    if (st->dead) {
        /* This request has not been accepted yet.  Do not claim success
         * if a delivery cannot be scheduled. */
        const int delivered = rt_fs_deliver(ctx, st, ipc.callback, ipc.user_data, RTFAT_EIO);
        rt_fs_report(ctx, entry_index, &ipc, RTFAT_EIO, 0);
        *result = delivered ? 0u : (uint32_t)RTFAT_EIO;
        return 1;
    }
    admit = rt_fs_admit(st, entry_index, &ipc, 1, 0);
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
    if (!rt_fs_deliver(ctx, st, st->req.callback, st->req.user_data, st->req.result)) {
        rt_fs_report(ctx, entry_index, &ipc, RTFAT_EIO, 0);
        *result = (uint32_t)RTFAT_EIO;
        return 1;
    }
    rt_fs_report(ctx, entry_index, &ipc, st->req.result, 0);
    *result = 0;
    rt_fs_start_queued(ctx, st); /* an immediate result also frees the engine */
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
        if (slot->kind == RT_FS_OP_SNOOP && *result >= 0) {
            rtfs_learn_fs_fd(&st->fs, *result);
            st->fs_fd_learned++;
        }
        if (slot->kind == RT_FS_OP_CLOSE_SNOOP && *result >= 0 && st->fs.fs_fd == slot->fd) st->fs.fs_fd = -1;
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
        if (st->pend.reserved != 0) {
            /* A CMD13 after a write: another, or the engine goes on. */
            if (rt_fs_settle_next(ctx, st, *result)) return;
        } else {
            if (*result < 0) st->failures++;
            req->fat.io_status = *result < 0 ? *result : 0;
            if (req->fat.io_write && rt_fs_settle_next(ctx, st, 0)) return;
        }
        if (rt_fs_advance(ctx, st, 0) == RT_FS_ADVANCE_ISSUED) return;
        st->pend.in_use = 0;
        if (st->pend.job) {
            /* The import job's card request: the job goes on. */
            st->pend.job = 0;
            rt_fs_job_resume(ctx, st, req->result, result, callback, user_data);
        } else {
            *result = req->result;
            *callback = (uintptr_t)req->callback;
            *user_data = (uintptr_t)req->user_data;
            rt_fs_report_done(ctx, req->result);
        }
        rt_fs_start_queued(ctx, st);
        return;
    }
    if (tag == (void*)&st->job.tag && st->job.tag.in_use) {
        /* One of the job's NAND requests. */
        st->job.tag.in_use = 0;
        rt_fs_job_resume(ctx, st, *result, result, callback, user_data);
        rt_fs_start_queued(ctx, st);
        return;
    }
    /* An unknown tag: nothing to continue, nothing to call. */
}

int rt_on_ipc(struct rt_context* ctx, uint32_t entry_index, uintptr_t* args, uint32_t* result) {
    if (entry_index == RT_IPC_ASYNC_IOCTL) {
        /* Disc reads and seeks keep their hook; every other async ioctl
         * joins the savegame path. ISFS request numbers never collide
         * with 0x71 or 0xAB, so the checks below read no game memory for
         * them. */
        const uint32_t ioctl = (uint32_t)args[1];
        const uint32_t* in = (const uint32_t*)args[2];
        const uint32_t in_len = (uint32_t)args[3];
        if (rt_is_di_read(ioctl, in, in_len) || rt_is_di_seek(ioctl, in, in_len)) {
            return rt_on_ioctl_async(ctx, args, result);
        }
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
    const uint64_t k_di_bytes = UINT64_C(1) << 34;  /* 32-bit DVD word offset */
    uint32_t remaining;
    uint64_t first;
    uint64_t start;
    uint64_t end;
    uint64_t aligned_end;
    uint32_t length;
    uint32_t i;
    if (run->length > UINT32_MAX || record->run_done > run->length ||
        run->source > UINT64_MAX - record->run_done) {
        ctx->disc_failures++;
        return -1;
    }
    remaining = (uint32_t)(run->length - record->run_done);
    if (remaining == 0) return 0;
    first = run->source + record->run_done;  /* partition byte wanted first */
    if (first >= k_di_bytes || (uint64_t)remaining > k_di_bytes - first) {
        /* /dev/di encodes a full 34-bit byte offset in its 32-bit word
         * field.  Do not let a malformed or out-of-range DISC entry wrap. */
        ctx->disc_failures++;
        return -1;
    }
    start = first & ~UINT64_C(31);
    record->chunk_skip = (uint32_t)(first - start);
    record->chunk_bytes = remaining;
    if (record->chunk_bytes > RT_BOUNCE_BYTES - record->chunk_skip) {
        record->chunk_bytes = RT_BOUNCE_BYTES - record->chunk_skip;
    }
    end = first + record->chunk_bytes;
    aligned_end = (end + 31u) & ~UINT64_C(31);
    if (aligned_end > k_di_bytes) {
        ctx->disc_failures++;
        return -1;
    }
    length = (uint32_t)(aligned_end - start);
    for (i = 0; i < 8; ++i) record->di_command[i] = 0;
    record->di_command[0] = RT_DI_READ << 24;
    record->di_command[1] = length;
    record->di_command[2] = (uint32_t)(start >> 2);
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

/* Reads `sectors` sectors from `sector` of the card (or, for `usb`, of
 * the USB drive) into `target` (32-byte aligned) for `record`, which
 * moves to `phase`. 1 when the request is in flight, -1 on an IPC
 * refusal. */
static int rt_issue_sd_read(struct rt_context* ctx, struct rt_pending* record, int usb, uint32_t sector,
                            uint32_t sectors, uint32_t target, uint32_t phase) {
    struct rt_sdio_request* rq = &record->request;
    const uint32_t fd = usb ? ctx->usb_fd : ctx->sdio_fd;
    const uint32_t ioctl = usb ? RT_UMS_READ_SECTORS : ctx->sdio_sdhc == RT_SD_D2X ? RT_SDHC_READ : RT_SDIO_SENDCMD;
    if (usb || ctx->sdio_sdhc == RT_SD_D2X) {
        /* d2x's /dev/sdio/sdhc and /dev/usb2: sector and count in, the data out. */
        rq->cmd = sector;
        rq->cmd_type = sectors;
        record->vec[0].data = (uint32_t)(uintptr_t)&rq->cmd;
        record->vec[0].len = 4;
        record->vec[1].data = (uint32_t)(uintptr_t)&rq->cmd_type;
        record->vec[1].len = 4;
        record->vec[2].data = target;
        record->vec[2].len = sectors * RT_SECTOR_BYTES;
    } else {
        rq->cmd = RT_SD_CMD_READMULTIBLOCK;
        rq->cmd_type = RT_SD_CMDTYPE_AC;
        rq->rsp_type = RT_SD_RESPONSE_R1;
        rq->arg = ctx->sdio_sdhc ? sector : sector * RT_SECTOR_BYTES;
        rq->blk_cnt = sectors;
        rq->blk_size = RT_SECTOR_BYTES;
        rq->dma_addr = target;
        rq->isdma = 1;
        rq->pad0 = 0;
        record->vec[0].data = (uint32_t)(uintptr_t)rq;
        record->vec[0].len = sizeof(*rq);
        record->vec[1].data = target;
        record->vec[1].len = sectors * RT_SECTOR_BYTES;
        record->vec[2].data = (uint32_t)(uintptr_t)record->response;
        record->vec[2].len = sizeof(record->response);
    }
    rt_flush_range((uintptr_t)rq, sizeof(*rq));
    rt_flush_range((uintptr_t)record->vec, sizeof(record->vec));
    rt_flush_range((uintptr_t)target, sectors * RT_SECTOR_BYTES); /* no stale lines over the DMA target */
    record->phase = phase;
    ctx->sd_requests++;
    if (fd == 0xFFFFFFFFu || rt_ioctlv_async(ctx, record, fd, ioctl) < 0) {
        ctx->sd_failures++;
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
    if (run->kind == RT_KIND_DISC) return rt_issue_disc_chunk(ctx, record);
    if ((run->kind != RT_KIND_SD && run->kind != RT_KIND_USB) || remaining == 0) return 0;
    if (want > RT_BOUNCE_BYTES) want = RT_BOUNCE_BYTES;
    sectors = (want + RT_SECTOR_BYTES - 1) / RT_SECTOR_BYTES;
    record->chunk_skip = skip;
    record->chunk_bytes = sectors * RT_SECTOR_BYTES - skip;
    if (record->chunk_bytes > remaining) record->chunk_bytes = remaining;
    if (run->kind == RT_KIND_SD) {
        /* Not while a savegame write is still being programmed (rt_sd_settles):
         * checked and issued with interrupts off, so a write cannot slip in
         * between. pad_request[0] counts the waits (the request line is
         * only ever flushed, never dropped). */
        struct rt_fs_state* fs = rt_fs_of(ctx);
        if (fs != 0 && rt_sd_settles(ctx, fs)) {
            const uint32_t msr = rt_interrupts_off();
            int r;
            if (fs->card_busy && record->pad_request[0] < RT_SD_SETTLE_POLLS) {
                record->pad_request[0]++;
                record->phase = RT_PHASE_SD_WAIT;
                r = rt_sd_wait_trip(ctx, record) < 0 ? -1 : 1;
            } else {
                record->pad_request[0] = 0;
                r = rt_issue_sd_read(ctx, record, 0, sector, sectors, record->bounce, RT_PHASE_SD);
            }
            rt_interrupts_restore(msr);
            return r;
        }
    }
    return rt_issue_sd_read(ctx, record, run->kind == RT_KIND_USB, sector, sectors, record->bounce, RT_PHASE_SD);
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

/* Everything of the current window that comes from memory; its SD and
 * DISC runs are fetched afterwards. */
static void rt_fill_memory_runs(struct rt_pending* record) {
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
            continue; /* PASSTHROUGH: the disc already filled it; SD and DISC: fetched later */
        }
        rt_flush_range((uintptr_t)dst, n);
    }
    record->run_index = 0;
    record->run_done = 0;
}

/* Moves a read that needs more than RT_MAX_RUNS pieces on to its next
 * window. Returns 1 when one is ready (memory runs already filled), 0
 * when the read is fully covered, -1 when the table lookup fails. */
static int rt_next_window(struct rt_context* ctx, struct rt_pending* record) {
    const uint32_t done = record->covered;
    uint32_t covered = 0;
    int touched = 0;
    int count;
    if (done >= record->length) return 0;
    count = rt_split(ctx, ((uint64_t)record->word_offset << 2) + done, record->length - done, record->runs,
                     &covered, &touched);
    if (count <= 0 || covered == 0) return -1;
    record->run_count = (uint32_t)count;
    record->covered = done + covered;
    rt_fill_memory_runs(record);
    return 1;
}

#ifdef RT_RVZ
/* --- RVZ games (rt_hook.h: rt_rvz_state) ---------------------------------- */
#define RT_RVZ_ERR_PAST_END 1u
#define RT_RVZ_ERR_ENTRY 2u
#define RT_RVZ_ERR_SD 3u
#define RT_RVZ_ERR_TOO_BIG 4u
#define RT_RVZ_ERR_DECODER 5u
#define RT_RVZ_ERR_DECODE 6u
#define RT_RVZ_ERR_LISTS 7u
#define RT_RVZ_ERR_DATA 8u
#define RT_RVZ_ERR_EXTENT 9u
#define RT_RVZ_ERR_START 10u

static struct rt_rvz_state* rt_rvz_of(const struct rt_context* ctx) {
    struct rt_rvz_state* st = (struct rt_rvz_state*)(uintptr_t)ctx->rvz_state;
    return st != 0 && st->magic == RT_RVZ_MAGIC ? st : 0;
}

static uint32_t rt_rvz_ticks(void) {
#ifdef RT_TARGET_PPC
    uint32_t tb;
    __asm__ volatile("mftb %0" : "=r"(tb) : : "memory");
    return tb;
#else
    return 0;
#endif
}

/* Word `i` of the table sector held (big endian on the card). */
static uint32_t rt_rvz_entry_word(const struct rt_rvz_state* st, uint32_t i) {
    const uint8_t* p = (const uint8_t*)st->entries + 4u * i;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* The card sector holding sector `file_sector` of a file, and how many of
 * the file's sectors follow it there. 0 when the file has no such sector. */
static int rt_rvz_locate(const struct rt_rvz_extent* extents, uint32_t count, uint32_t file_sector,
                         uint32_t* device, uint32_t* run) {
    uint32_t lo = 0;
    uint32_t hi = count;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2u;
        if (extents[mid].file_sector + extents[mid].count <= file_sector) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (lo >= count || file_sector < extents[lo].file_sector) return 0;
    *device = extents[lo].device_sector + (file_sector - extents[lo].file_sector);
    *run = extents[lo].count - (file_sector - extents[lo].file_sector);
    return 1;
}

/* The group holding the partition data's KiB `kib`: its index, first
 * KiB and size in bytes (the last group of each segment may be short). */
static void rt_rvz_group_of(const struct rt_rvz_state* st, uint32_t kib, uint32_t* group, uint32_t* start_kib,
                            uint32_t* payload) {
    uint32_t end;
    if (kib < st->split_kib) {
        *group = kib / st->group_kib;
        *start_kib = *group * st->group_kib;
        end = st->split_kib;
    } else {
        const uint32_t j = (kib - st->split_kib) / st->group_kib;
        *group = st->first_groups + j;
        *start_kib = st->split_kib + j * st->group_kib;
        end = st->data_kib;
    }
    *payload = (end - *start_kib < st->group_kib ? end - *start_kib : st->group_kib) << 10;
}

static int rt_rvz_fail(struct rt_rvz_state* st, uint32_t code) {
    st->last_error = code;
    return -1;
}

/* The null round trip that starts a read: the completion entry then runs
 * from the IPC interrupt, where every read ends, as with a disc reply. */
static int32_t rt_rvz_null_trip(struct rt_context* ctx, struct rt_rvz_state* st, struct rt_pending* record) {
    if (ctx->sdio_fd == 0xFFFFFFFFu) return -1;
#ifdef RT_TARGET_PPC
    if (ctx->sdio_sdhc == RT_SD_D2X) {
        rt_ioctlv_async_fn vfn = (rt_ioctlv_async_fn)(uintptr_t)ctx->ioctlv_async;
        if (vfn == 0) return -1;
        return vfn(ctx->sdio_fd, RT_SDHC_ISINSERTED, 0, 0, record->vec, ctx->complete_entry, record);
    } else {
        rt_ioctl_async_fn fn = (rt_ioctl_async_fn)(uintptr_t)ctx->di_read_entry;
        if (fn == 0) return -1;
        rt_flush_range((uintptr_t)st->status, sizeof(st->status));
        return fn(ctx->sdio_fd, RT_SDIO_GETSTATUS, 0, 0, (uint32_t)(uintptr_t)st->status, 4, ctx->complete_entry,
                  record);
    }
#else
    if (ctx->sdio_sdhc == RT_SD_D2X) {
        if (rt_host_ioctlv_async == 0) return -1;
        return rt_host_ioctlv_async(ctx->sdio_fd, RT_SDHC_ISINSERTED, 0, 0, record->vec, ctx->complete_entry, record);
    }
    if (rt_host_ioctl_async == 0) return -1;
    return rt_host_ioctl_async(ctx->sdio_fd, RT_SDIO_GETSTATUS, 0, 0, (uint32_t)(uintptr_t)st->status, 4,
                               ctx->complete_entry, record);
#endif
}

/* A partition read of an RVZ game, taken whole: nothing reaches the
 * drive. Returns 1 (the hook answers the call). */
static int rt_rvz_on_read(struct rt_context* ctx, struct rt_rvz_state* st, uintptr_t* args, uint32_t* result,
                          uint32_t word_offset, uint32_t length) {
    const int in_window = ctx->virtual_start_words != 0 && word_offset >= ctx->virtual_start_words;
    struct rt_pending* rec = 0;
    uint32_t msr;
    uint32_t busy;
    int count = -1;
    uint32_t covered = 0;
    msr = rt_interrupts_off();
    busy = st->busy;
    if (!busy) st->busy = 1;
    rt_interrupts_restore(msr);
    if (!busy) rec = rt_claim_pending(ctx);
    if (rec == 0) {
        /* The DVD driver issues one read at a time; a second one cannot be
         * served while the cache is being refilled. IOS's own refusal. */
        if (!busy) st->busy = 0;
        st->busy_refusals++;
        st->failures++;
        *result = (uint32_t)-1;
        return 1;
    }
    if (ctx->table != 0) {
        int touched = 0;
        count = rt_split(ctx, (uint64_t)word_offset << 2, length, rec->runs, &covered, &touched);
    }
    if (count < 0) {
        /* No table (or none that answers): all of it is the RVZ's. */
        rec->runs[0].vstart = (uint64_t)word_offset << 2;
        rec->runs[0].length = length;
        rec->runs[0].source = 0;
        rec->runs[0].skip = 0;
        rec->runs[0].kind = RT_KIND_PASSTHROUGH;
        count = 1;
        covered = length;
    }
    if (covered < length) ctx->run_overflow++;
    rec->run_count = (uint32_t)count;
    rec->covered = covered;
    rec->callback = (uint32_t)args[6];
    rec->user_data = (uint32_t)args[7];
    rec->out = (uint32_t)args[4];
    rec->length = length;
    rec->word_offset = word_offset;
    rec->is_virtual = (uint32_t)in_window;
    rec->phase = RT_PHASE_RVZ_START;
    rec->run_index = 0;
    rec->run_done = 0;
    rec->chunk_bytes = 0;
    rec->chunk_skip = 0;
    rec->di_result = RT_DI_SUCCESS;
    if (in_window) ctx->virtual_reads++;
    if (rt_rvz_null_trip(ctx, st, rec) < 0) {
        rec->in_use = 0;
        st->busy = 0;
        st->failures++;
        st->last_error = RT_RVZ_ERR_START;
        *result = (uint32_t)-1;
        return 1;
    }
    ctx->redirected_reads++;
    *result = 0; /* queued, as IOS answers an accepted request */
    return 1;
}

/* Issues the next piece of the group being fetched. 1: in flight. */
static int rt_rvz_fetch_piece(struct rt_context* ctx, struct rt_rvz_state* st, struct rt_pending* record) {
    uint32_t device = 0;
    uint32_t run = 0;
    uint32_t n = st->fetch_sectors - st->fetch_done;
    if (!rt_rvz_locate(st->extents, st->extent_count, st->fetch_file_sector + st->fetch_done, &device, &run)) {
        return rt_rvz_fail(st, RT_RVZ_ERR_EXTENT);
    }
    if (n > run) n = run;
    if (n > RT_RVZ_REQUEST_SECTORS) n = RT_RVZ_REQUEST_SECTORS;
    st->fetch_last = n;
    st->sd_requests++;
    if (rt_issue_sd_read(ctx, record, st->on_usb != 0, device, n,
                         st->fetch_target + st->fetch_done * RT_SECTOR_BYTES, RT_PHASE_RVZ_GROUP) < 0) {
        return rt_rvz_fail(st, RT_RVZ_ERR_SD);
    }
    return 1;
}

/* Makes `group` (entry in the held table sector) the cached one: at once
 * for a group of zeros, else by fetching its stored bytes (1: in flight). */
static int rt_rvz_begin_group(struct rt_context* ctx, struct rt_rvz_state* st, struct rt_pending* record,
                              uint32_t group, uint32_t start_kib, uint32_t payload) {
    const uint32_t slot = group % RT_RVZ_ENTRIES_PER_SECTOR;
    const uint32_t word0 = rt_rvz_entry_word(st, 2u * slot);
    const uint32_t word1 = rt_rvz_entry_word(st, 2u * slot + 1u);
    const uint32_t size = word1 & RT_RVZ_SIZE_MASK;
    uint64_t byte;
    uint32_t skip;
    st->cached_group = RT_RVZ_NO_GROUP; /* the buffers are about to change */
    st->cached_start_kib = start_kib;
    st->cached_payload = payload;
    if (size == 0) {
        /* Every byte zero, no exceptions (docs/RVZ.md: rvz_group_t). */
        st->cached_data = 0;
        st->cached_bytes = 0;
        st->cached_skip = 0;
        st->cached_packed = 0;
        st->cached_group = group;
        return 0;
    }
    byte = (uint64_t)word0 * 4u;
    skip = (uint32_t)(byte % RT_SECTOR_BYTES);
    st->fetch_group = group;
    st->fetch_word0 = word0;
    st->fetch_word1 = word1;
    st->fetch_file_sector = (uint32_t)(byte / RT_SECTOR_BYTES);
    st->fetch_sectors = (skip + size + RT_SECTOR_BYTES - 1u) / RT_SECTOR_BYTES;
    st->fetch_done = 0;
    {
        const uint32_t room = (word1 & RT_RVZ_COMPRESSED) ? st->stored_bytes : st->group_bytes;
        if (size > room || st->fetch_sectors * RT_SECTOR_BYTES > room) return rt_rvz_fail(st, RT_RVZ_ERR_TOO_BIG);
    }
    st->fetch_target = (word1 & RT_RVZ_COMPRESSED) ? st->stored_buffer : st->group_buffer;
    return rt_rvz_fetch_piece(ctx, st, record);
}

/* The fetched group's stored bytes have all landed: decode them. */
static int rt_rvz_decode(struct rt_rvz_state* st) {
    const uint32_t size = st->fetch_word1 & RT_RVZ_SIZE_MASK;
    const uint32_t skip = (st->fetch_word0 * 4u) % RT_SECTOR_BYTES;
    const int compressed = (st->fetch_word1 & RT_RVZ_COMPRESSED) != 0;
    const uint8_t* data;
    uint32_t bytes;
    uint32_t lists = 0;
    if (compressed) {
        uint32_t t0;
        int32_t got;
        if (st->dctx == 0) st->dctx = rt_zstd_init(st->dctx_buffer, st->dctx_bytes);
        if (st->dctx == 0) return rt_rvz_fail(st, RT_RVZ_ERR_DECODER);
        t0 = rt_rvz_ticks();
        got = rt_zstd_decode(st->dctx, (uint8_t*)(uintptr_t)st->group_buffer, st->group_bytes,
                             (const uint8_t*)(uintptr_t)st->stored_buffer + skip, size);
        st->decode_ticks += rt_rvz_ticks() - t0;
        if (got < 0) return rt_rvz_fail(st, RT_RVZ_ERR_DECODE);
        data = (const uint8_t*)(uintptr_t)st->group_buffer;
        bytes = (uint32_t)got;
    } else {
        data = (const uint8_t*)(uintptr_t)st->group_buffer + skip;
        bytes = size;
    }
    /* Groups stored as they are pad their exception lists to 4 bytes. */
    if (rtrvz_exception_bytes(data, bytes, st->lists, !compressed, &lists) != RTRVZ_OK) {
        return rt_rvz_fail(st, RT_RVZ_ERR_LISTS);
    }
    if (!(st->fetch_word1 & RT_RVZ_PACKED) && bytes - lists < st->cached_payload) {
        return rt_rvz_fail(st, RT_RVZ_ERR_DATA);
    }
    st->cached_data = (uint32_t)(uintptr_t)data;
    st->cached_bytes = bytes;
    st->cached_skip = lists;
    st->cached_packed = (st->fetch_word1 & RT_RVZ_PACKED) != 0;
    st->cached_group = st->fetch_group;
    st->groups_loaded++;
    return 0;
}

/* Copies bytes [within, within + n) of the cached group's data to dst. */
static int rt_rvz_copy(struct rt_rvz_state* st, uint32_t within, uint8_t* dst, uint32_t n) {
    const uint8_t* data = (const uint8_t*)(uintptr_t)st->cached_data + st->cached_skip;
    const uint32_t available = st->cached_bytes - st->cached_skip;
    if (st->cached_data == 0) {
        rt_zero(dst, n);
        return 0;
    }
    if (st->cached_packed) {
        if (rtrvz_unpack(data, available, (uint64_t)st->cached_start_kib << 10, within, dst, n, &st->junk) !=
            RTRVZ_OK) {
            return rt_rvz_fail(st, RT_RVZ_ERR_DATA);
        }
        return 0;
    }
    if (within > available || n > available - within) return rt_rvz_fail(st, RT_RVZ_ERR_DATA);
    rt_copy(dst, data + within, n);
    return 0;
}

/* Serves the current run from the RVZ as far as the cache reaches.
 * 1: a request is in flight; 0: the run is done; -1: it failed. */
static int rt_rvz_serve_run(struct rt_context* ctx, struct rt_rvz_state* st, struct rt_pending* record) {
    const rt_run* run = &record->runs[record->run_index];
    const uint64_t base = run->kind == RT_KIND_DISC ? run->source : run->vstart;
    while (record->run_done < run->length) {
        const uint64_t at = base + record->run_done;
        uint8_t* dst = rt_run_destination(record, run) + record->run_done;
        uint32_t within;
        uint32_t group = 0;
        uint32_t start_kib = 0;
        uint32_t payload = 0;
        uint32_t n;
        if ((at >> 10) >= st->data_kib) {
            /* Past the partition's data: the drive's own refusal. */
            st->past_end++;
            return rt_rvz_fail(st, RT_RVZ_ERR_PAST_END);
        }
        rt_rvz_group_of(st, (uint32_t)(at >> 10), &group, &start_kib, &payload);
        within = (uint32_t)(at - ((uint64_t)start_kib << 10));
        if (group >= st->group_count || payload == 0) return rt_rvz_fail(st, RT_RVZ_ERR_ENTRY);
        if (st->cached_group != group) {
            const uint32_t sector = group / RT_RVZ_ENTRIES_PER_SECTOR;
            int rc;
            if (st->entry_sector != sector) {
                uint32_t device = 0;
                uint32_t contiguous = 0;
                if (!rt_rvz_locate(st->table_extents, st->table_extent_count, st->table_sector + sector, &device,
                                   &contiguous)) {
                    return rt_rvz_fail(st, RT_RVZ_ERR_EXTENT);
                }
                st->entry_sector = RT_RVZ_NO_GROUP;
                st->want_entry_sector = sector;
                st->entry_loads++;
                st->sd_requests++;
                if (rt_issue_sd_read(ctx, record, 0, device, 1, (uint32_t)(uintptr_t)st->entries,
                                     RT_PHASE_RVZ_ENTRY) < 0) {
                    return rt_rvz_fail(st, RT_RVZ_ERR_SD);
                }
                return 1;
            }
            rc = rt_rvz_begin_group(ctx, st, record, group, start_kib, payload);
            if (rc != 0) return rc;
        }
        n = (uint32_t)(run->length - record->run_done);
        if (n > payload - within) n = payload - within;
        if (rt_rvz_copy(st, within, dst, n) < 0) return -1;
        rt_flush_range((uintptr_t)dst, n);
        record->run_done += n;
    }
    return 0;
}

/* The completion entry for an RVZ game's reads. */
static void rt_rvz_complete(struct rt_context* ctx, struct rt_rvz_state* st, int32_t* result,
                            struct rt_pending* record, uintptr_t* callback, uintptr_t* user_data) {
    if (record->phase == RT_PHASE_RVZ_START) {
        rt_fill_memory_runs(record); /* MEM and ZERO runs, zeros in virtual gaps */
    } else if (*result < 0) {
        ctx->sd_failures++;
        st->last_error = RT_RVZ_ERR_SD;
        record->di_result = RT_DI_ERROR;
    } else if (record->phase == RT_PHASE_RVZ_ENTRY) {
        st->entry_sector = st->want_entry_sector;
    } else if (record->phase == RT_PHASE_RVZ_GROUP) {
        st->fetch_done += st->fetch_last;
        if (st->fetch_done < st->fetch_sectors) {
            if (rt_rvz_fetch_piece(ctx, st, record) > 0) {
                *callback = 0;
                return;
            }
            record->di_result = RT_DI_ERROR;
        } else if (rt_rvz_decode(st) < 0) {
            record->di_result = RT_DI_ERROR;
        }
    } else if (record->phase == RT_PHASE_SD) {
        /* A chunk of one of the table's SD or USB runs (a mod's file). */
        const rt_run* run = &record->runs[record->run_index];
        uint8_t* dst = rt_run_destination(record, run) + record->run_done;
        rt_copy(dst, (const uint8_t*)(uintptr_t)record->bounce + record->chunk_skip, record->chunk_bytes);
        rt_flush_range((uintptr_t)dst, record->chunk_bytes);
        record->run_done += record->chunk_bytes;
    }

    while (record->di_result == RT_DI_SUCCESS) {
        const rt_run* run;
        int issued = 0;
        if (record->run_index >= record->run_count) {
            const int next = rt_next_window(ctx, record);
            if (next == 0) break;
            if (next < 0) {
                record->di_result = RT_DI_ERROR;
                break;
            }
            continue;
        }
        run = &record->runs[record->run_index];
        if (run->kind == RT_KIND_SD || run->kind == RT_KIND_USB) {
            issued = rt_issue_sd_chunk(ctx, record);
        } else if (run->kind == RT_KIND_DISC || (run->kind == RT_KIND_PASSTHROUGH && !record->is_virtual)) {
            issued = rt_rvz_serve_run(ctx, st, record);
        }
        if (issued > 0) {
            *callback = 0;
            return;
        }
        if (issued < 0) {
            record->di_result = RT_DI_ERROR;
            break;
        }
        record->run_index++;
        record->run_done = 0;
    }

    *result = (int32_t)record->di_result;
    *callback = (uintptr_t)record->callback;
    *user_data = (uintptr_t)record->user_data;
    if (record->di_result != RT_DI_SUCCESS) st->failures++;
    st->reads++;
    if (ctx->flags & RT_FLAG_GECKO) {
        /* "Z<word offset>:<length>:<result>:<groups loaded>\n" */
        const uint32_t msr = rt_interrupts_off();
        rt_gecko_putc(ctx, 'Z');
        rt_gecko_hex(ctx, record->word_offset);
        rt_gecko_putc(ctx, ':');
        rt_gecko_hex(ctx, record->length);
        rt_gecko_putc(ctx, ':');
        rt_gecko_hex(ctx, record->di_result == RT_DI_SUCCESS ? 0u : st->last_error);
        rt_gecko_putc(ctx, ':');
        rt_gecko_hex(ctx, st->groups_loaded);
        rt_gecko_putc(ctx, '\n');
        rt_interrupts_restore(msr);
    }
    record->in_use = 0;
    st->busy = 0;
}
#endif

/* The game's read issued again as it asked, into its own buffer. */
static int rt_reissue_game_read(struct rt_context* ctx, struct rt_pending* record) {
    uint32_t i;
    for (i = 0; i < 8; ++i) record->di_command[i] = 0;
    record->di_command[0] = RT_DI_READ << 24;
    record->di_command[1] = record->length;
    record->di_command[2] = record->word_offset;
    rt_flush_range((uintptr_t)record->di_command, sizeof(record->di_command));
    return rt_game_read_async(ctx, record) < 0 ? -1 : 1;
}

void rt_on_di_complete(struct rt_context* ctx, int32_t* result, struct rt_pending* record, uintptr_t* callback,
                       uintptr_t* user_data) {
    uint32_t slot;
    ctx->completions++;
#ifdef RT_RVZ
    {
        struct rt_rvz_state* st = rt_rvz_of(ctx);
        if (st != 0) {
            rt_rvz_complete(ctx, st, result, record, callback, user_data);
            return;
        }
    }
#endif
    slot = (uint32_t)(record - ctx->pending);
    if (slot >= RT_MAX_PENDING) slot = 0;
    if (record->phase == RT_PHASE_SD_WAIT) {
        /* The card was finishing a savegame write: the chunk now, or
         * another wait. */
        const int issued = rt_issue_sd_chunk(ctx, record);
        if (issued > 0) {
            *callback = 0;
            return;
        }
        ctx->sd_failures++;
        record->di_result = RT_DI_ERROR;
        record->run_index = record->run_count;
    } else if (record->phase == RT_PHASE_DISC) {
        /* A virtual read's stand-in at the partition start: every byte of
         * the answer comes from the table, so the drive's verdict on it
         * does not matter. */
        if (*result != RT_DI_SUCCESS && record->is_virtual) *result = RT_DI_SUCCESS;
        if (*result != RT_DI_SUCCESS && ctx->retry[slot] < RT_READ_RETRIES) {
            ctx->retry[slot]++;
            ctx->read_retries++;
            if (rt_reissue_game_read(ctx, record) > 0) {
                *callback = 0;
                return;
            }
        }
        record->di_result = (uint32_t)*result;
        if (*result == RT_DI_SUCCESS) {
            ctx->retry[slot] = 0;
            rt_fill_memory_runs(record);
        } else {
            record->run_index = record->run_count; /* nothing to fetch for a failed read */
        }
    } else {
        /* An SD or DISC chunk landed (or failed). IPC results are 0 for a
         * successful SD request and RT_DI_SUCCESS for a DVDLowRead. */
        const rt_run* run = &record->runs[record->run_index];
        const int failed = record->phase == RT_PHASE_SD ? *result < 0 : *result != RT_DI_SUCCESS;
        if (failed) {
            if (record->phase == RT_PHASE_SD) ctx->sd_failures++;
            else ctx->disc_failures++;
            /* The same chunk again (run_done has not moved), a few times:
             * a card or drive that misses once usually answers next time. */
            if (ctx->retry[slot] < RT_READ_RETRIES) {
                ctx->retry[slot]++;
                ctx->read_retries++;
                if (rt_issue_sd_chunk(ctx, record) > 0) {
                    *callback = 0;
                    return;
                }
            }
            if (record->phase == RT_PHASE_SD) {
                record->di_result = RT_DI_ERROR;
            } else {
                record->di_result = *result > 0 ? (uint32_t)*result : RT_DI_ERROR; /* the drive's own verdict */
            }
            record->run_index = record->run_count;
        } else {
            ctx->retry[slot] = 0;
            uint8_t* dst = rt_run_destination(record, run) + record->run_done;
            rt_copy(dst, (const uint8_t*)(uintptr_t)record->bounce + record->chunk_skip, record->chunk_bytes);
            rt_flush_range((uintptr_t)dst, record->chunk_bytes);
            record->run_done += record->chunk_bytes;
        }
    }

    /* Next SD or DISC work, if any, window by window. Every failure above
     * leaves a di_result other than success, which ends the loop. */
    while (record->di_result == RT_DI_SUCCESS) {
        int issued;
        if (record->run_index >= record->run_count) {
            const int next = rt_next_window(ctx, record);
            if (next == 0) break;
            if (next < 0) {
                record->di_result = RT_DI_ERROR;
                break;
            }
            continue;
        }
        issued = rt_issue_sd_chunk(ctx, record);
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
        const uint32_t msr = rt_interrupts_off();
        ctx->last_checksum = rt_checksum_runs(record);
        rt_gecko_putc(ctx, 'M');
        rt_gecko_hex(ctx, record->word_offset);
        rt_gecko_putc(ctx, ':');
        rt_gecko_hex(ctx, record->length);
        rt_gecko_putc(ctx, ':');
        rt_gecko_hex(ctx, ctx->last_checksum);
        rt_gecko_putc(ctx, '\n');
        rt_interrupts_restore(msr);
    }
    record->in_use = 0;
}
