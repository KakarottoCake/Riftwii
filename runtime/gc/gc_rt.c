/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The GameCube disc blob's C part (gc_rt.h). Same rules as the other
 * blobs: no globals, no literals that need relocations, no library
 * calls; all state is in the context the trampolines hand over. */
#include "gc_rt.h"
#include "gc_bt.h"
#include "gc_card.h"
#include "gc_gcad.h"
#include "gc_ports.h"
#include "gc_tgc.h"
#include "gc_video.h"
#include "gc_load.h"
#include "gc_disc.h"
#include "gc_programs.h"
#ifdef RT_RVZ
#include "gc_rvz.h"
#endif

/* Hollywood's IPC registers, uncached (wiibrew "Hardware/IPC"): the
 * request's physical address, the control bits, IOS's reply. */
#define GC_IPC_PPCMSG 0xCD000000u
#define GC_IPC_PPCCTRL 0xCD000004u
#define GC_IPC_ARMMSG 0xCD000008u
#define GC_IPC_X1 0x01u  /* a request is waiting for IOS */
#define GC_IPC_Y2 0x02u  /* IOS took it (write 1 to clear) */
#define GC_IPC_Y1 0x04u  /* IOS replied (write 1 to clear) */
#define GC_IPC_X2 0x08u  /* the reply is read: IOS may send the next */

#define GC_IOS_IOCTLV 7u
#define GC_SDIO_SENDCMD 7u
#define GC_SD_READMULTIBLOCK 0x12u
#define GC_SD_CMDTYPE_AC 3u
#define GC_SD_RESPONSE_R1 1u
#define GC_SECTOR 512u
/* SENDCMD's reply: exactly 16 bytes (three response words and the
 * auto-CMD12 one). IOS on a Wii refuses any other length with -4;
 * Dolphin does not check. */
#define GC_SD_RESPONSE_BYTES 16u
#define GC_TRACE_SKIPPED (-3000)  /* no trace, or the IPC buffers were in use */

#define GC_IO_READ 1u
#define GC_IO_STREAM 2u
#define GC_IO_BEAT 3u
#define GC_IO_TRACE 4u
#define GC_IO_POLL_TICKS 6075u  /* 100 us; IOS runs while the alarm is out */
#define GC_IO_SLICE_TICKS 24300u  /* 400 us of CPU work between checkpoints */
#define GC_IO_TIMEOUT_TICKS 303750000u  /* five seconds, independent of polling frequency */

static void gc_io_resume(struct gc_rt_context* ctx);
static int gc_io_start(struct gc_rt_context* ctx, uint32_t job);

static void gc_io_arm(struct gc_rt_context* ctx) {
    if (ctx->io_queued) return;
    ctx->io_queued = 1;
    gc_set_alarm(ctx, ctx->io_alarm, GC_IO_POLL_TICKS);
}

/* Yield all the way out of the alarm (or ReadDiskID's initial hook turn).
 * Merely setting MSR.EE here would
 * allow nested game callbacks inside an SDK alarm's temporary context.
 * Instead, its normal epilogue restores the interrupted game, and the
 * next alarm picks this private-stack continuation up where it stopped. */
static void gc_io_pause(struct gc_rt_context* ctx) {
    if (!ctx->io_running) return;
    ctx->io_running = 0;
    gc_io_switch(&ctx->io_sp, ctx->io_caller_sp);
}

void gc_io_checkpoint(void* io) {
    struct gc_rt_context* ctx = (struct gc_rt_context*)io;
    uint32_t now;
    if (ctx == 0 || !ctx->io_running) return;
    __asm__ volatile("mftb %0" : "=r"(now));
    if (now - ctx->io_ticks >= GC_IO_SLICE_TICKS) {
        /* Decode/copy checkpoints leave shared IPC buffers untouched.
         * Controllers use private requests; other storage stays excluded. */
        ctx->io_buffers_idle = 1;
        gc_io_pause(ctx);
        ctx->io_buffers_idle = 0;
    }
}

/* Polled callers used during launch/exit retain their old timeout. In
 * the worker, each unsuccessful poll ends this alarm, and elapsed time
 * bounds a failed device rather than fifty million separate alarms. */
static int gc_ipc_wait(struct gc_rt_context* ctx, uint32_t started, uint32_t polls) {
    /* ES_LaunchTitle reloads IOS and resets the PPC. A cooperative
     * return at that point would resume the game against a dead IOS. */
    if (ctx != 0 && ctx->ipc_no_timeout) return 1;
    if (ctx != 0 && ctx->io_running) {
        uint32_t now;
        __asm__ volatile("mftb %0" : "=r"(now));
        if (now - started >= GC_IO_TIMEOUT_TICKS && !ctx->ipc_no_timeout) return 0;
        gc_io_pause(ctx);
        return 1;
    }
    return polls != GC_IPC_TIMEOUT || (ctx != 0 && ctx->ipc_no_timeout);
}

static uint32_t gc_read32(uint32_t address) { return *(volatile uint32_t*)(uintptr_t)address; }
static void gc_write32(uint32_t address, uint32_t value) { *(volatile uint32_t*)(uintptr_t)address = value; }

static uint32_t gc_physical(uint32_t address) { return address & 0x3FFFFFFFu; }

/* Match a real card's wait even when Dolphin finishes IOS requests at
 * once. The deadline starts before sending, not after IOS replies. */
static uint32_t gc_sd_test_start(struct gc_rt_context* ctx) {
    uint32_t now = 0;
    if (ctx->flags & GC_FLAG_SD_SLOW) __asm__ volatile("mftb %0" : "=r"(now));
    return now;
}

static void gc_sd_test_wait(struct gc_rt_context* ctx, uint32_t started, uint32_t bytes) {
    uint32_t now;
    uint32_t ticks;
    if (!(ctx->flags & GC_FLAG_SD_SLOW)) return;
    /* 60.75 ticks/us; transfers are whole sectors (60 us each). */
    ticks = 91125u + (bytes / GC_SECTOR) * 3645u;
    ctx->io_buffers_idle = 1;
    do {
        __asm__ volatile("mftb %0" : "=r"(now));
        if (now - started < ticks) gc_io_pause(ctx);
    } while (now - started < ticks);
    ctx->io_buffers_idle = 0;
}

void gc_flush(uint32_t address, uint32_t length) {
    uint32_t a = address & ~31u;
    const uint32_t end = address + length;
    for (; a < end; a += 32) __asm__ volatile("dcbf 0,%0" : : "r"(a) : "memory");
    __asm__ volatile("sync" : : : "memory");
}

void gc_invalidate(uint32_t address, uint32_t length) {
    uint32_t a = address & ~31u;
    const uint32_t end = address + length;
    for (; a < end; a += 32) __asm__ volatile("dcbi 0,%0" : : "r"(a) : "memory");
    __asm__ volatile("sync" : : : "memory");
}

static void gc_copy(uint8_t* dst, const uint8_t* src, uint32_t n) {
    while (n--) *dst++ = *src++;
}

static void gc_zero(uint8_t* dst, uint32_t n) {
    while (n--) *dst++ = 0;
}

/* The guard (gc_rt.h): the game's bytes at guard_addr put back wherever
 * IOS wrote over them, in memory and out of both caches. */
#define GC_GUARD_WORDS 64u
void gc_guard(struct gc_rt_context* ctx) {
    uint32_t i;
    if (ctx->guard_addr == 0) return;
    for (i = 0; i < GC_GUARD_WORDS; ++i) {
        const uint32_t cached = ctx->guard_addr + 4 * i;
        const uint32_t uncached = gc_physical(cached) | 0xC0000000u;
        const uint32_t now = gc_read32(uncached);
        if (now == ctx->guard_copy[i]) continue;
        if (ctx->guard_hits == 0) {
            ctx->guard_first_request = ctx->sd_requests;
            ctx->guard_first_offset = 4 * i;
            ctx->guard_first_value = now;
        }
        ctx->guard_hits++;
        gc_write32(uncached, ctx->guard_copy[i]);
        __asm__ volatile("dcbi 0,%0\n\tsync\n\ticbi 0,%0\n\tisync" : : "r"(cached) : "memory");
    }
}

/* The reply IOS has ready (the request's physical address), or 0. */
uint32_t gc_ipc_take(void) {
    uint32_t reply;
    if ((gc_read32(GC_IPC_PPCCTRL) & GC_IPC_Y1) == 0) return 0;
    reply = gc_read32(GC_IPC_ARMMSG);
    gc_write32(GC_IPC_PPCCTRL, GC_IPC_Y1);
    gc_write32(GC_IPC_PPCCTRL, GC_IPC_X2);
    return reply;
}

/* A request to IOS (its physical address): sent, and taken by IOS. IOS
 * takes no new request while a reply waits to be acknowledged, and the
 * Wii Remotes' reads (gc_bt.c) finish whenever a remote sends something:
 * a reply waiting meanwhile goes to them, or the send never completes
 * (Luigi's Mansion in Dolphin: every disc read timed out, the control
 * register at X1 | Y1). Every reply but a pending request's own is
 * theirs: disc reads and trace writes run one at a time. */
int32_t gc_ipc_send(uint32_t phys, struct gc_rt_context* ctx) {
    uint32_t polls;
    uint32_t started;
    if (ctx->ipc_sending) return -1000;
    ctx->ipc_sending = 1;
    /* A synchronous controller send may have been abandoned. Retire its
     * X1 and any late Y2 before publishing a different request pointer.
     * PPCCTRL's X1 is read/write; Y2 is write-one-to-clear (Hardware/IPC). */
    gc_write32(GC_IPC_PPCCTRL, GC_IPC_Y2);
    __asm__ volatile("mftb %0" : "=r"(started));
    gc_write32(GC_IPC_PPCMSG, phys);
    gc_write32(GC_IPC_PPCCTRL, GC_IPC_X1);
    for (polls = 0; (gc_read32(GC_IPC_PPCCTRL) & GC_IPC_Y2) == 0; ++polls) {
        const uint32_t reply = gc_ipc_take();
        if (reply != 0) gc_ipc_route(ctx, reply);
        if (!gc_ipc_wait(ctx, started, polls)) {
            /* Only the shared request has a late-DMA recovery path. A
             * controller's synchronous caller has no continuation to take
             * Y2 later: withdraw its doorbell before releasing ownership.
             * Interrupts remain off throughout this caller, so no other
             * sender can publish X1 between that withdrawal and release.
             * Storage retains both its handshake and buffer quarantine. */
            if (phys != ctx->io_pending) {
                gc_write32(GC_IPC_PPCCTRL, GC_IPC_Y2);
                ctx->ipc_sending = 0;
            }
            return -1000;
        }
    }
    gc_write32(GC_IPC_PPCCTRL, GC_IPC_Y2);
    ctx->ipc_sending = 0;
    return 0;
}

void gc_ipc_route(struct gc_rt_context* ctx, uint32_t reply) {
    if (ctx == 0) return;
    if (reply == ctx->io_pending) {
        ctx->io_reply = 1;
        /* A timed-out job has returned already. Only this late reply
         * makes its DMA buffers safe for the next job to reuse. */
        if (ctx->io_job == 0) ctx->io_pending = 0;
        return;
    }
    if (ctx->bt && gc_bt_on_reply((struct gc_bt*)(uintptr_t)ctx->bt, reply)) return;
    if (ctx->gcad) gc_gcad_on_reply((struct gc_gcad*)(uintptr_t)ctx->gcad, reply);
}

/* A reply that is not the one being waited for. */
static void gc_ipc_other(struct gc_rt_context* ctx, uint32_t reply) { gc_ipc_route(ctx, reply); }

/* One ioctlv to IOS and its reply, by polling (the Wii Remotes' reads may
 * be in flight meanwhile; their replies go to gc_bt.c). Returns IOS's
 * result, or -1000 when IOS does not answer. */
static int32_t gc_ioctlv(struct gc_rt_context* ctx, uint32_t fd, uint32_t ioctl, uint32_t in_count,
                         uint32_t out_count) {
    uint32_t* rq = ctx->ipc;
    const uint32_t phys = gc_physical((uint32_t)(uintptr_t)rq);
    uint32_t i;
    uint32_t polls;
    const uint32_t test_started = gc_sd_test_start(ctx);
    uint32_t started;
    for (i = 0; i < 16; ++i) rq[i] = 0;
    rq[0] = GC_IOS_IOCTLV;
    rq[2] = fd;
    rq[3] = ioctl;
    rq[4] = in_count;
    rq[5] = out_count;
    rq[6] = gc_physical((uint32_t)(uintptr_t)ctx->vec);
    gc_flush((uint32_t)(uintptr_t)rq, sizeof(ctx->ipc));
    ctx->io_reply = 0;
    ctx->io_pending = phys;
    __asm__ volatile("mftb %0" : "=r"(started));
    if (gc_ipc_send(phys, ctx) != 0) return -1000;
    for (;;) {
        /* The launch's reply never comes when IOS takes it (it reloads and
         * the PPC is reset): with ipc_no_timeout the wait has no end, as a
         * timeout would give the game back to a dead IOS; a refusal does
         * come back as a reply. */
        for (polls = 0; !ctx->io_reply && (gc_read32(GC_IPC_PPCCTRL) & GC_IPC_Y1) == 0; ++polls)
            if (!gc_ipc_wait(ctx, started, polls)) return -1000;
        if (ctx->io_reply) break;
        {
            const uint32_t reply = gc_read32(GC_IPC_ARMMSG);
            gc_write32(GC_IPC_PPCCTRL, GC_IPC_Y1);
            gc_write32(GC_IPC_PPCCTRL, GC_IPC_X2);
            if (reply == phys) break;
            gc_ipc_other(ctx, reply);
        }
    }
    ctx->io_pending = 0;
    gc_invalidate((uint32_t)(uintptr_t)rq, sizeof(ctx->ipc));
    gc_guard(ctx);
    if (fd == ctx->sdio_fd) gc_sd_test_wait(ctx, test_started, ctx->request[4] * GC_SECTOR);
    return (int32_t)rq[1];
}

/* CMD18: `count` sectors from `sector` into `target` (32-byte aligned). */
int gc_sd_read(struct gc_rt_context* ctx, uint32_t sector, uint32_t count, uint32_t target) {
    uint32_t* q = ctx->request;
    int32_t result;
    q[0] = GC_SD_READMULTIBLOCK;
    q[1] = GC_SD_CMDTYPE_AC;
    q[2] = GC_SD_RESPONSE_R1;
    q[3] = ctx->sdhc ? sector : sector * GC_SECTOR;
    q[4] = count;
    q[5] = GC_SECTOR;
    q[6] = target;
    q[7] = 1;  /* DMA */
    q[8] = 0;
    ctx->vec[0] = gc_physical((uint32_t)(uintptr_t)q);
    ctx->vec[1] = 36;
    ctx->vec[2] = gc_physical(target);
    ctx->vec[3] = count * GC_SECTOR;
    ctx->vec[4] = gc_physical((uint32_t)(uintptr_t)ctx->response);
    ctx->vec[5] = GC_SD_RESPONSE_BYTES;
    gc_flush((uint32_t)(uintptr_t)q, 36);
    gc_flush((uint32_t)(uintptr_t)ctx->vec, sizeof(ctx->vec));
    gc_flush(target, count * GC_SECTOR);  /* no dirty lines left to land on the DMA's bytes */
    ctx->sd_requests++;
    result = gc_ioctlv(ctx, ctx->sdio_fd, GC_SDIO_SENDCMD, 2, 1);
    gc_invalidate(target, count * GC_SECTOR);
    if (result < 0) {
        ctx->failures++;
        ctx->last_error = result;
        return 0;
    }
    return 1;
}

/* d2x's USB mass-storage read ('U' 'M' 'S' 3): the sector and the count
 * in, the data out. */
#define GC_UMS_READ_SECTORS 0x554D5303u
/* Sectors a request asks for: 32 KB, as the menu reads the drive through
 * d2x (wii/storage/umsdev.cpp). */
#define GC_USB_REQUEST_SECTORS 64u

/* `count` sectors of the USB drive from `sector` into `target`, which is
 * in MEM2 (d2x's USB driver writes nowhere else) and 32-byte aligned. */
int gc_usb_read(struct gc_rt_context* ctx, uint32_t sector, uint32_t count, uint32_t target) {
    uint32_t* q = ctx->request;
    while (count != 0) {
        const uint32_t n = count < GC_USB_REQUEST_SECTORS ? count : GC_USB_REQUEST_SECTORS;
        int32_t result;
        q[0] = sector;
        q[1] = n;
        ctx->vec[0] = gc_physical((uint32_t)(uintptr_t)&q[0]);
        ctx->vec[1] = 4;
        ctx->vec[2] = gc_physical((uint32_t)(uintptr_t)&q[1]);
        ctx->vec[3] = 4;
        ctx->vec[4] = gc_physical(target);
        ctx->vec[5] = n * GC_SECTOR;
        gc_flush((uint32_t)(uintptr_t)q, 32);
        gc_flush((uint32_t)(uintptr_t)ctx->vec, sizeof(ctx->vec));
        gc_flush(target, n * GC_SECTOR);  /* no dirty lines left to land on the DMA's bytes */
        ctx->usb_requests++;
        result = gc_ioctlv(ctx, ctx->usb_fd, GC_UMS_READ_SECTORS, 2, 1);
        gc_invalidate(target, n * GC_SECTOR);
        __asm__ volatile("mftb %0" : "=r"(ctx->usb_last_read));
        if (result < 0) {
            ctx->usb_failures++;
            ctx->failures++;
            ctx->last_error = result;
            return 0;
        }
        sector += n;
        count -= n;
        target += n * GC_SECTOR;
    }
    return 1;
}

/* `length` bytes of a card or USB run from byte `at` of it into `dst`.
 * From the card, whole sectors go straight into the game's buffer when it
 * is aligned for the DMA; the rest, and everything from USB (the game's
 * memory is MEM1), through the bounce buffer. */
static int gc_read_sd_run(struct gc_rt_context* ctx, const struct gc_rt_run* run, uint32_t at, uint8_t* dst,
                          uint32_t length) {
    const int usb = run->kind == GC_RUN_USB;
    while (length != 0) {
        const uint32_t byte = run->skip + at;  /* from the start of sector `source` */
        const uint32_t sector = run->source + byte / GC_SECTOR;
        const uint32_t into = byte % GC_SECTOR;
        if (!usb && into == 0 && length >= GC_SECTOR && ((uint32_t)(uintptr_t)dst & 31u) == 0) {
            uint32_t count = length / GC_SECTOR;
            if (count > 128) count = 128;
            if (!gc_sd_read(ctx, sector, count, (uint32_t)(uintptr_t)dst)) return 0;
            dst += count * GC_SECTOR;
            at += count * GC_SECTOR;
            length -= count * GC_SECTOR;
        } else {
            uint32_t want = into + length;
            uint32_t count;
            uint32_t got;
            if (want > ctx->bounce_bytes) want = ctx->bounce_bytes;
            count = (want + GC_SECTOR - 1) / GC_SECTOR;
            if (usb ? !gc_usb_read(ctx, sector, count, ctx->bounce) : !gc_sd_read(ctx, sector, count, ctx->bounce))
                return 0;
            got = count * GC_SECTOR - into;
            if (got > length) got = length;
            gc_copy(dst, (const uint8_t*)(uintptr_t)(ctx->bounce + into), got);
            gc_io_checkpoint(ctx);
            dst += got;
            at += got;
            length -= got;
        }
    }
    return 1;
}

#ifdef RT_RVZ
/* gc_rvz.c's reads (gc_rvz.h): `io` is the context. Its buffers are all
 * in MEM2, so the USB drive's reads go there directly. */
int gc_rvz_device_read(void* io, uint32_t usb, uint32_t sector, uint32_t count, uintptr_t target) {
    struct gc_rt_context* ctx = (struct gc_rt_context*)io;
    if (usb) return gc_usb_read(ctx, sector, count, (uint32_t)target);
    return gc_sd_read(ctx, sector, count, (uint32_t)target);
}

/* `length` bytes of the image from its disc offset `offset`, out of the
 * RVZ (gc_rvz.h), through the cache slot of the game's reads or the
 * stream's. The time it takes is counted for the trace. */
static int gc_read_rvz(struct gc_rt_context* ctx, uint8_t* dst, uint32_t length, uint32_t offset, uint32_t stream) {
    struct gc_rvz* rvz = (struct gc_rvz*)(uintptr_t)ctx->rvz;
    uint32_t t0, t1;
    int ok;
    if (rvz == 0) return 0;
    __asm__ volatile("mftb %0" : "=r"(t0));
    ok = gc_rvz_read(rvz, ctx, stream ? GC_RVZ_SLOT_STREAM : GC_RVZ_SLOT_GAME, dst, length, offset);
    __asm__ volatile("mftb %0" : "=r"(t1));
    ctx->rvz_ticks += t1 - t0;
    return ok;
}
#endif

/* The game's view of the disc, `length` bytes from `offset` into `dst`.
 * `stream`: a read for disc audio streaming (an RVZ keeps its own group
 * for it). */
static int gc_read_disc_unlocked(struct gc_rt_context* ctx, uint8_t* dst, uint32_t length, uint32_t offset,
                                 uint32_t stream) {
    const struct gc_rt_run* runs = (const struct gc_rt_run*)(uintptr_t)ctx->runs;
    const uint32_t start = (uint32_t)(uintptr_t)dst;
    const uint32_t total = length;
    uint32_t i = 0;
#ifndef RT_RVZ
    (void)stream;
#endif
    while (length != 0) {
        uint32_t at;
        uint32_t n;
        while (i < ctx->run_count && runs[i].disc + runs[i].length <= offset) ++i;
        if (i == ctx->run_count || runs[i].disc > offset) return 0;
        at = offset - runs[i].disc;
        n = runs[i].length - at;
        if (n > length) n = length;
        if (runs[i].kind == GC_RUN_SD || runs[i].kind == GC_RUN_USB) {
            if (!gc_read_sd_run(ctx, &runs[i], at, dst, n)) return 0;
        } else if (runs[i].kind == GC_RUN_RVZ) {
#ifdef RT_RVZ
            if (!gc_read_rvz(ctx, dst, n, runs[i].source + at, stream)) return 0;
#else
            return 0;  /* the loader gives RVZ runs to the RVZ build only */
#endif
        } else if (runs[i].kind == GC_RUN_MEM) {
            gc_copy(dst, (const uint8_t*)(uintptr_t)(runs[i].source + at), n);
        } else {
            gc_zero(dst, n);
        }
        dst += n;
        offset += n;
        length -= n;
    }
    /* As the DI's DMA leaves it: in memory, not only in the cache. */
    gc_flush(start, total);
    return 1;
}

/* Storage retains the shared buffers across every worker yield. Controller
 * requests have their own buffers and never take or release this ownership. */
static int gc_read_disc(struct gc_rt_context* ctx, uint8_t* dst, uint32_t length, uint32_t offset,
                        uint32_t stream) {
    int ok;
    if (ctx->storage_busy || ctx->io_pending != 0 || ctx->ipc_sending) return 0;
    ctx->storage_busy = 1;
    ok = gc_read_disc_unlocked(ctx, dst, length, offset, stream);
    gc_io_checkpoint(ctx);
    ctx->storage_busy = 0;
    return ok;
}

uint32_t gc_irq_off(void) {
    uint32_t msr;
    __asm__ volatile("mfmsr %0" : "=r"(msr));
    __asm__ volatile("mtmsr %0" : : "r"(msr & ~0x8000u) : "memory");
    return msr;
}
void gc_irq_restore(uint32_t msr) { __asm__ volatile("mtmsr %0" : : "r"(msr) : "memory"); }

/* One ioctl (no vectors), polled like gc_ioctlv: CMD13 takes this form. */
static int32_t gc_ioctl(struct gc_rt_context* ctx, uint32_t fd, uint32_t ioctl, uint32_t in, uint32_t in_len,
                        uint32_t out, uint32_t out_len) {
    uint32_t* rq = ctx->ipc;
    const uint32_t phys = gc_physical((uint32_t)(uintptr_t)rq);
    uint32_t i;
    uint32_t polls;
    const uint32_t test_started = gc_sd_test_start(ctx);
    uint32_t started;
    for (i = 0; i < 16; ++i) rq[i] = 0;
    rq[0] = 6;  /* IOS_IOCTL */
    rq[2] = fd;
    rq[3] = ioctl;
    rq[4] = gc_physical(in);
    rq[5] = in_len;
    rq[6] = gc_physical(out);
    rq[7] = out_len;
    gc_flush(in, in_len);
    gc_flush(out, out_len);
    gc_flush((uint32_t)(uintptr_t)rq, sizeof(ctx->ipc));
    ctx->io_reply = 0;
    ctx->io_pending = phys;
    __asm__ volatile("mftb %0" : "=r"(started));
    if (gc_ipc_send(phys, ctx) != 0) return -1000;
    for (;;) {
        /* The launch's reply never comes when IOS takes it (it reloads and
         * the PPC is reset): with ipc_no_timeout the wait has no end, as a
         * timeout would give the game back to a dead IOS; a refusal does
         * come back as a reply. */
        for (polls = 0; !ctx->io_reply && (gc_read32(GC_IPC_PPCCTRL) & GC_IPC_Y1) == 0; ++polls)
            if (!gc_ipc_wait(ctx, started, polls)) return -1000;
        if (ctx->io_reply) break;
        {
            const uint32_t reply = gc_read32(GC_IPC_ARMMSG);
            gc_write32(GC_IPC_PPCCTRL, GC_IPC_Y1);
            gc_write32(GC_IPC_PPCCTRL, GC_IPC_X2);
            if (reply == phys) break;
            gc_ipc_other(ctx, reply);
        }
    }
    ctx->io_pending = 0;
    gc_invalidate((uint32_t)(uintptr_t)rq, sizeof(ctx->ipc));
    gc_invalidate(out, out_len);
    gc_guard(ctx);
    if (fd == ctx->sdio_fd) gc_sd_test_wait(ctx, test_started, 0);
    return (int32_t)rq[1];
}

/* `count` sectors (at most 16) from `buf` (32-byte aligned) to the card
 * at `sector`: CMD25, then CMD13 until the card has written them. The
 * caller holds ctx->storage_busy. */
static int32_t gc_sd_write_locked(struct gc_rt_context* ctx, uint32_t sector, uint32_t count, uint32_t buf) {
    uint32_t* q = ctx->request;
    int32_t result;
    uint32_t i;
    q[0] = 0x19u;
    q[1] = GC_SD_CMDTYPE_AC;
    q[2] = GC_SD_RESPONSE_R1;
    q[3] = ctx->sdhc ? sector : sector * GC_SECTOR;
    q[4] = count;
    q[5] = GC_SECTOR;
    q[6] = buf;
    q[7] = 1;
    q[8] = 0;
    ctx->vec[0] = gc_physical((uint32_t)(uintptr_t)q);
    ctx->vec[1] = 36;
    ctx->vec[2] = gc_physical(buf);
    ctx->vec[3] = count * GC_SECTOR;
    ctx->vec[4] = gc_physical((uint32_t)(uintptr_t)ctx->response);
    ctx->vec[5] = GC_SD_RESPONSE_BYTES;
    gc_flush((uint32_t)(uintptr_t)q, 36);
    gc_flush((uint32_t)(uintptr_t)ctx->vec, sizeof(ctx->vec));
    gc_flush(buf, count * GC_SECTOR);
    ctx->sd_requests++;
    result = gc_ioctlv(ctx, ctx->sdio_fd, GC_SDIO_SENDCMD, 2, 1);
    if (result >= 0 && ctx->card_rca != 0) {
        for (i = 0; i < 8192; ++i) {
            uint32_t state;
            q[0] = 0x0Du;
            q[1] = GC_SD_CMDTYPE_AC;
            q[2] = GC_SD_RESPONSE_R1;
            q[3] = ctx->card_rca << 16;
            q[4] = q[5] = q[6] = q[7] = q[8] = 0;
            ctx->response[0] = 0;
            result = gc_ioctl(ctx, ctx->sdio_fd, GC_SDIO_SENDCMD, (uint32_t)(uintptr_t)q, 36,
                              (uint32_t)(uintptr_t)ctx->response, 16);
            if (result < 0) return result;  /* leave the save dirty until status can be checked */
            state = (ctx->response[0] >> 9) & 0xFu;
            if (state != 6 && state != 7) break;  /* left receive-data and programming */
        }
    }
    return result;
}

int32_t gc_sd_write(struct gc_rt_context* ctx, uint32_t sector, uint32_t count, uint32_t buf) {
    int32_t result;
    const uint32_t msr = gc_irq_off();
    if (ctx->storage_busy || ctx->io_pending != 0 || ctx->ipc_sending) {
        gc_irq_restore(msr);
        return GC_TRACE_SKIPPED;
    }
    ctx->storage_busy = 1;
    result = gc_sd_write_locked(ctx, sector, count, buf);
    ctx->storage_busy = 0;
    gc_irq_restore(msr);
    return result;
}

/* Record `seq`'s sector: the first records stay (how the game started),
 * the last GC_TRACE_RING go round. */
static uint32_t gc_trace_slot(const struct gc_rt_context* ctx, uint32_t seq) {
    const uint32_t kept = ctx->trace_count > 2 * GC_TRACE_RING ? ctx->trace_count - GC_TRACE_RING : ctx->trace_count / 2;
    const uint32_t ring = ctx->trace_count - kept;
    if (seq <= kept) return ctx->trace_sector + seq - 1;
    return ctx->trace_sector + kept + (seq - kept - 1) % ring;
}

/* The test remote's results take the projection callers' 16 words. */
typedef char gc_fake_log_fits[(GC_TR_RVZ_GROUPS - GC_TR_PROJECTION_CALLERS == GC_BT_FAKE_STEPS * 2) ? 1 : -1];

/* The audio hardware. The audio interface sits where a Wii in Wii mode
 * has it (0xCC006C00 on a GameCube; the loader moves the game's own
 * accesses too); the DSP's registers stay at 0xCC005000. */
#define GC_AI_CR 0xCD006C00u
#define GC_AI_VR 0xCD006C04u
#define GC_AI_IT 0xCD006C0Cu  /* the sample count that raises the AI interrupt */
#define GC_AI_PSTAT 0x01u     /* the stream plays */
#define GC_AI_AFR 0x02u       /* the stream's rate, as the game set it */
#define GC_AI_INTMSK 0x04u    /* the stream's sample count raises an interrupt */
#define GC_AI_INT 0x08u       /* ... which is pending (written 1: cleared) */
#define GC_AI_INTVLD 0x10u
#define GC_AI_SCRESET 0x20u   /* written 1: the sample counter goes back to 0 */
#define GC_AI_32K 0x40u       /* the DMA (the game's own audio) runs at 32 kHz, as the game sets it */
#define GC_DSP_MAIL_TO_DSP 0xCC005000u    /* upper half; bit 15: the DSP has not taken it yet */
#define GC_DSP_MAIL_FROM_DSP 0xCC005004u  /* upper half; bit 15: mail for the CPU */
#define GC_DSP_MAIL_FROM_DSP_LOW 0xCC005006u  /* reading it takes the mail */
#define GC_DSP_CSR 0xCC00500Au
#define GC_DSP_CSR_RESET 0x0001u      /* written 1: the DSP resets; reads 1 until it has */
#define GC_DSP_CSR_HALT 0x0004u
#define GC_DSP_CSR_AIINT 0x0008u      /* the audio DMA's interrupt (written 1: cleared) */
#define GC_DSP_CSR_ARINT 0x0020u      /* an ARAM transfer finished (cleared by the game's handler) */
#define GC_DSP_CSR_ARINTMSK 0x0040u   /* ... and raises the DSP interrupt */
#define GC_DSP_CSR_DSPINT 0x0080u     /* the DSP's own interrupt (written 1: cleared) */
#define GC_DSP_CSR_DMA_BUSY 0x0200u   /* an ARAM transfer is going (ARGetDMAStatus) */
#define GC_DSP_CSR_INIT 0x0400u       /* the DSP is still taking its boot code */
#define GC_DSP_CSR_BOOT_ROM 0x0800u   /* the DSP starts from its ROM at the next reset */
#define GC_AR_SIZE 0xCC005012u
#define GC_AR_REFRESH 0xCC00501Au
#define GC_AI_DMA_CONTROL 0xCC005036u  /* bit 15: the audio DMA runs */
#define GC_AI_DMA_ON 0x8000u

/* The VI's registers (16 bits each). */
#define GC_VI_BASE 0xCC002000u
#define GC_VI_VTR 0xCC002000u   /* bits 4-13: active lines per field */
#define GC_VI_DCR 0xCC002002u   /* bit 0: the display is on */
#define GC_VI_TFBL 0xCC00201Cu  /* the top field's frame buffer */
#define GC_VI_BFBL 0xCC002024u  /* the bottom field's */
#define GC_VI_HSW 0xCC002048u   /* bits 0-7: a field line's stride, bits 8-14: its width, both in 32 bytes */
#define GC_VI_ACTIVE_LINES(vtr) (((vtr) >> 4) & 0x3FFu)
#define GC_VI_CLOCK 0xCC00206Cu  /* bit 0: 54 MHz (progressive) */
#define GC_VI_SEL 0xCC00206Eu    /* bit 0: a component cable is in */

static uint32_t gc_read16(uint32_t address) { return *(volatile uint16_t*)(uintptr_t)address; }
static void gc_write16(uint32_t address, uint32_t value) { *(volatile uint16_t*)(uintptr_t)address = (uint16_t)value; }

/* Read the game's EnabledBits without depending on its startup r13 value.
 * The loader recognises the offset in PADRead; an unfamiliar layout or
 * an invalid small-data address disables the remembered-ownership rule. */
static uint32_t gc_pad_enabled(const struct gc_rt_context* ctx) {
    uint32_t r13, address;
    if (ctx->pad_enabled_offset == 0) return GC_PORTS_ENABLED_UNKNOWN;
    __asm__ volatile("mr %0,13" : "=r"(r13));
    address = r13 + (uint32_t)ctx->pad_enabled_offset;
    if (address < 0x80003100u || address > 0x81800000u - 4u || (address & 3u) != 0)
        return GC_PORTS_ENABLED_UNKNOWN;
    return gc_read32(address);
}

/* One record of the trace (gc_rt.h), with interrupts held off so it
 * cannot meet a read on the card; skipped while a read is going on. */
static int32_t gc_trace(struct gc_rt_context* ctx, uint32_t where) {
    int32_t result;
    uint32_t* w = (uint32_t*)(uintptr_t)ctx->trace_buf;
    const struct gc_rt_stream* st = (const struct gc_rt_stream*)(uintptr_t)ctx->stream;
    uint32_t msr, tbu, tbl, i, sector;
    if (!gc_trace_allowed(ctx->flags, where)) return GC_TRACE_SKIPPED;
    if (ctx->trace_sector == 0 || ctx->trace_count == 0 || w == 0) return GC_TRACE_SKIPPED;
    if (!ctx->io_running && (where == GC_TRACE_READ || where == GC_TRACE_PAD || where == GC_TRACE_AI ||
        where == GC_TRACE_AR || where == GC_TRACE_BEAT || where == GC_TRACE_COMMAND ||
        where == GC_TRACE_PAD_SI || where == GC_TRACE_DISC)) {
        ctx->io_trace_where = where;
        gc_io_start(ctx, GC_IO_TRACE);
        return 0;
    }
    msr = gc_irq_off();
    if (ctx->storage_busy || ctx->io_pending != 0 || ctx->ipc_sending) {
        gc_irq_restore(msr);
        return GC_TRACE_SKIPPED;
    }
    ctx->storage_busy = 1;
    __asm__ volatile("mftbu %0" : "=r"(tbu));
    __asm__ volatile("mftb %0" : "=r"(tbl));
    for (i = 0; i < 128; ++i) w[i] = 0;
    w[GC_TR_MAGIC] = GC_TRACE_MAGIC;
    w[GC_TR_SEQ] = ++ctx->trace_seq;
    w[GC_TR_WHERE] = where;
    w[GC_TR_TBU] = tbu;
    w[GC_TR_TBL] = tbl;
    w[GC_TR_COMMANDS] = ctx->commands;
    w[GC_TR_READS] = ctx->reads;
    w[GC_TR_READ_BYTES] = ctx->read_bytes;
    w[GC_TR_SD_REQUESTS] = ctx->sd_requests;
    w[GC_TR_FAILURES] = ctx->failures;
    w[GC_TR_LAST_ERROR] = (uint32_t)ctx->last_error;
    w[GC_TR_LAST_OFFSET] = ctx->last_offset;
    w[GC_TR_PAD_READS] = ctx->pad_reads;
    if (st) {
        w[GC_TR_STREAM_PLAYING] = st->playing;
        w[GC_TR_STREAM_MIXES] = st->mixes;
        w[GC_TR_STREAM_REFILLS] = st->refills;
        w[GC_TR_STREAM_STARVED] = st->starved;
        w[GC_TR_STREAM_FETCH_BUSY] = st->fetch_busy;
        w[GC_TR_STREAM_BUFFERED] = st->fetched - st->consumed;
    }
    w[GC_TR_AI_DMAS] = ctx->ai_dmas;
    w[GC_TR_AR_DMAS] = ctx->ar_dmas;
    for (i = 0; i < 4; ++i) w[GC_TR_LAST_AR_TYPE + i] = ctx->last_ar[i];
    if (ctx->bt) {
        const struct gc_bt* bt = (const struct gc_bt*)(uintptr_t)ctx->bt;
        w[GC_TR_BT_STAGE] = bt->stage;
        w[GC_TR_BT_ERROR] = (uint32_t)bt->last_error;
        w[GC_TR_BT_EVENTS] = bt->event_count;
        w[GC_TR_BT_ACL] = bt->acl_count;
        w[GC_TR_BT_LAST_EVENT] = bt->last_event;
        w[GC_TR_BT_KEYS] = bt->key_count << 16 | bt->connects;
        w[GC_TR_BT_REMOTES] = (uint32_t)bt->remotes[0].chans << 24 | (uint32_t)bt->remotes[0].ready << 16 |
                              bt->remotes[0].ext_step << 8 | bt->remotes[0].ext_kind;
        w[GC_TR_BT_BUTTONS] = (uint32_t)bt->remotes[0].core[0] << 8 | bt->remotes[0].core[1];
        w[GC_TR_BT_EVENT_LENGTHS] = bt->event_lengths;
        for (i = 0; i < GC_BT_REMOTES; ++i) {
            w[GC_TR_BT_PORTS] = w[GC_TR_BT_PORTS] << 8 | bt->port_of[i];
            w[GC_TR_PAD_ERRS] = w[GC_TR_PAD_ERRS] << 8 | bt->port_err[i];
        }
        gc_copy((uint8_t*)&w[GC_TR_BT_EVENT_LOG], &bt->event_log[0][0], 32);
        w[GC_TR_BT_SHAKES] = bt->shakes;
        w[GC_TR_BT_PROGRESS] = bt->progress;
        w[GC_TR_BT_LINK] = (uint32_t)bt->conn_status << 24 | (uint32_t)bt->auth_status << 16 |
                           (uint32_t)bt->disconnect_reason << 8 | bt->ext_error;
        w[GC_TR_BT_EXT] = (uint32_t)bt->ext_id[0] << 24 | (uint32_t)bt->ext_id[1] << 16 | (uint32_t)bt->last_mode << 8 |
                          bt->ext_given_up;
        w[GC_TR_BT_REPORTS] = bt->data_reports;
        for (i = 0; i < 4; ++i)
            if (bt->pro_axes_seen & (1u << i)) w[GC_TR_BT_PRO_RANGE + i] = (uint32_t)bt->pro_min[i] << 16 | bt->pro_max[i];
        {
            uint32_t slots = 0, ports = 0;
            for (i = 0; i < GC_BT_REMOTES; ++i) {
                slots |= (uint32_t)(bt->remotes[i].rumble & 1u) << i;
                ports |= (uint32_t)(bt->rumble_want[i] & 1u) << i;
            }
            w[GC_TR_BT_RUMBLE] = (bt->motor_calls & 0xFFFFu) << 16 | (bt->rumble_changes & 0xFFu) << 8 |
                                 slots << 4 | ports;
        }
    }
    if (ctx->card) {
        const struct gc_card* card = (const struct gc_card*)(uintptr_t)ctx->card;
        w[GC_TR_CARD_READS] = card->reads;
        w[GC_TR_CARD_WRITES] = card->programs << 16 | (card->erases & 0xFFFFu);
        w[GC_TR_CARD_FLUSHES] = card->flushes;
        w[GC_TR_CARD_DIRTY] = card->dirty_count;
        w[GC_TR_CARD_ERROR] = (uint32_t)card->flush_error;
        w[GC_TR_CARD_LAST] = card->last_cmd << 24 | (card->activity & 0xFFFFFFu);
    }
    w[GC_TR_VI_CONFIGURES] = ctx->vi_configures;
    w[GC_TR_VI_MODE] = ctx->vi_mode;
    w[GC_TR_PROJECTIONS] = ctx->projections;
    w[GC_TR_ORTHOGRAPHICS] = ctx->orthographics;
    for (i = 0; i < GC_PROJECTION_CALLERS * 4; ++i) w[GC_TR_PROJECTION_CALLERS + i] = ctx->projection_callers[i];
    if ((ctx->flags & GC_FLAG_FAKE_REMOTE) && ctx->bt) {
        const struct gc_bt* bt = (const struct gc_bt*)(uintptr_t)ctx->bt;
        gc_copy((uint8_t*)&w[GC_TR_PROJECTION_CALLERS], (const uint8_t*)bt->fake_log, sizeof(bt->fake_log));
    } else if (ctx->projections == 0) {
        /* No projection seen (16:9 off): the words would stay zero, so
         * they carry the VI's registers 0x00-0x3F instead, the timing
         * the TV locks onto. A TV that lost the signal on a Wii (the
         * Collector's Edition menu in 480p, 21 Oct) says nothing in
         * Dolphin, which takes any timing. */
        for (i = 0; i < GC_PROJECTION_CALLERS * 4; ++i) w[GC_TR_PROJECTION_CALLERS + i] = gc_read32(GC_VI_BASE + 4u * i);
    }
#ifdef RT_RVZ
    if (ctx->rvz) {
        const struct gc_rvz* rvz = (const struct gc_rvz*)(uintptr_t)ctx->rvz;
        w[GC_TR_RVZ_GROUPS] = rvz->groups_loaded;
        w[GC_TR_RVZ_TICKS] = ctx->rvz_ticks;
        w[GC_TR_RVZ_ERROR] = rvz->last_error << 24 | (rvz->failures & 0xFFFFFFu);
    }
#endif
    w[GC_TR_RESTARTS] = ctx->restarts;
    w[GC_TR_RESETS] = ctx->resets_asked << 16 | (ctx->reset_presses & 0xFFFFu);
    w[GC_TR_RESET_LAST] = ctx->reset_last;
    if (ctx->gcad) {
        const struct gc_gcad* ad = (const struct gc_gcad*)(uintptr_t)ctx->gcad;
        w[GC_TR_GCAD] = (uint32_t)ad->g.link << 24 | (ad->version & 0xFFu) << 16 | (ad->filled & 0xFFu) << 8 |
                        ((uint32_t)ad->g.dev_id & 0xFFu);
        w[GC_TR_GCAD_REPORTS] = ad->g.reports << 16 | (ad->send_failures & 0xFFFFu);
        w[GC_TR_GCAD_ERROR] = (uint32_t)ad->g.last_error;
    }
    w[GC_TR_USB] = (ctx->usb_requests & 0xFFFFu) << 16 | (ctx->usb_failures & 0xFFu) << 8 |
                   (ctx->usb_keepalives & 0xFFu);
    w[GC_TR_DSP_CSR] = gc_read16(GC_DSP_CSR) << 16 | gc_read16(GC_AI_DMA_CONTROL);
    w[GC_TR_DSP_MAIL] = gc_read16(GC_DSP_MAIL_TO_DSP) << 16 | gc_read16(GC_DSP_MAIL_FROM_DSP);
    w[GC_TR_AI_CR] = gc_read32(GC_AI_CR);
    w[GC_TR_AR_CONFIG] = gc_read16(GC_AR_SIZE) << 16 | gc_read16(GC_AR_REFRESH);
    w[GC_TR_AUDIO_RESET] = ctx->audio_reset;
    w[GC_TR_VI_DCR_CLOCK] = gc_read16(GC_VI_DCR) << 16 | gc_read16(GC_VI_CLOCK);
    w[GC_TR_VI_SEL_HSW] = gc_read16(GC_VI_SEL) << 16 | gc_read16(GC_VI_HSW);
    w[GC_TR_VI_TFBL] = gc_read32(GC_VI_TFBL);
    w[GC_TR_AR_WAITS] = ctx->ar_waits;
    w[GC_TR_AR_WAIT_LONGEST] = ctx->ar_wait_longest;
    w[GC_TR_AR_WAIT_TIMEOUTS] = ctx->ar_wait_timeouts;
    w[GC_TR_PENDING_CB] = ctx->pending_cb;
    w[GC_TR_ALARMS] = ctx->alarms;
    w[GC_TR_BEATS] = ctx->beats;
    w[GC_TR_BEAT_PC] = ctx->beat_pc;
    w[GC_TR_BEAT_LR] = ctx->beat_lr;
    w[GC_TR_BEAT_MSR] = ctx->beat_msr;
    w[GC_TR_EXCEPTION] = ctx->exception;
    w[GC_TR_DSISR] = ctx->dsisr;
    w[GC_TR_DAR] = ctx->dar;
    for (i = 0; i < 5; ++i) w[GC_TR_CRASH_R1 + i] = ctx->crash_gpr[i];
    for (i = 0; i < 4; ++i) w[GC_TR_CRASH_CHAIN + i] = ctx->crash_chain[i];
    w[GC_TR_CRASH_WORD] = ctx->crash_word;
    w[GC_TR_GUARD_HITS] = ctx->guard_hits;
    w[GC_TR_GUARD_FIRST_REQUEST] = ctx->guard_first_request;
    w[GC_TR_GUARD_FIRST_OFFSET] = ctx->guard_first_offset;
    w[GC_TR_GUARD_FIRST_VALUE] = ctx->guard_first_value;
    /* Interrupts: the PI's cause and mask, and Hollywood's lines to the
     * CPU (Wii-only; a GameCube game cannot clear them). */
    w[GC_TR_PI_CAUSE] = gc_read32(0xCC003000u);
    w[GC_TR_PI_MASK] = gc_read32(0xCC003004u);
    w[GC_TR_HW_IRQ_FLAG] = gc_read32(0xCD000030u);
    w[GC_TR_HW_IRQ_MASK] = gc_read32(0xCD000034u);
    if (where == GC_TRACE_PAD_SI) {
        const volatile uint32_t* types = (const volatile uint32_t*)(uintptr_t)ctx->si_type_cache;
        for (i = 0; i < GC_PORTS; ++i) w[GC_TR_PAD_SI_TYPES + i] = types != 0 ? types[i] : 0;
        /* SISR is safe to read: its errors clear on a write of one, and
         * RDST clears on input-buffer reads, which we never make here.
         * It describes past transfers, not continuous physical presence. */
        w[GC_TR_PAD_SISR] = gc_read32(0xCD006438u);
        w[GC_TR_PAD_ENABLED_BEFORE] = ctx->pad_enabled_before;
        w[GC_TR_PAD_ENABLED_AFTER] = gc_pad_enabled(ctx);
        w[GC_TR_PAD_RAW_ERRS] = ctx->pad_raw_errs;
        w[GC_TR_PAD_NATIVE] = (ctx->native_ports & 0xFu) | (ctx->pad_played & 0xFu) << 4 |
                              (types != 0 ? 0x100u : 0u) | (ctx->si_probe != 0 ? 0x200u : 0u);
        w[GC_TR_PAD_PROBES] = (ctx->si_probes_answered & 0xFFFFu) << 16 | (ctx->si_probes & 0xFFFFu);
        w[GC_TR_PAD_ERRS] = ctx->pad_filled_errs;
    }
    if (where == GC_TRACE_DISC && ctx->disc_change != 0) {
        const struct gc_disc_change* dc = (const struct gc_disc_change*)(uintptr_t)ctx->disc_change;
        w[GC_TR_DISC_STATE] = dc->current << 24 | (dc->changes & 0xFFu) << 16 | (dc->raised & 0xFFu) << 8 |
                              (dc->unseen & 0xFFu);
        w[GC_TR_DISC_STOPS] = dc->stops;
        w[GC_TR_DISC_DICVR] = ctx->di_regs != 0 ? gc_read32(ctx->di_regs + 4u * GC_DI_CVR) : 0;
        w[GC_TR_DISC_STEP] = dc->state << 24 | (dc->alarm_queued & 0xFFu) << 16 | (dc->looks & 0xFFFFu);
    }
    sector = gc_trace_slot(ctx, ctx->trace_seq);
    result = gc_sd_write_locked(ctx, sector, 1, ctx->trace_buf);
    ctx->storage_busy = 0;
    gc_irq_restore(msr);
    return result;
}

/* The DI's immediate register, where the SDK reads a status reply. */
static void gc_set_imm(struct gc_rt_context* ctx, uint32_t value) {
    if (ctx->di_regs != 0) *(volatile uint32_t*)(uintptr_t)(ctx->di_regs + 0x20) = value;
}

/* The callback runs from an OSAlarm a moment from now, where the DI
 * interrupt would have run it. A command that comes while the last one's
 * completion is still pending takes that completion over, as the drive
 * has one interrupt and the SDK one callback: the alarm is already in the
 * OS's queue, and setting it again would put it there twice (OSSetAlarm
 * does not take an alarm out first), leaving the queue's links in a loop.
 * The same alarm also carries a read's copy (gc_read_step), so whether it
 * is in the queue is kept in alarm_queued, cleared as it goes off. */
typedef void (*gc_set_alarm_fn)(void* alarm, uint64_t ticks, uint32_t handler);
typedef int32_t (*gc_ogc_create_alarm_fn)(uint32_t* handle);
typedef int32_t (*gc_ogc_set_alarm_fn)(uint32_t handle, const uint32_t* timespec, uint32_t cb, void* arg);

/* OSSetAlarm(alarm, ticks, handler), or with libogc (gc_rt.h, "libogc")
 * SYS_SetAlarm on the handle kept in the alarm's buffer, made by
 * SYS_CreateAlarm the first time. libogc takes a struct timespec (a 64-bit
 * tv_sec, then tv_nsec, as the game's SYS_SetAlarm reads it) and turns it
 * back into ticks with the clock the loader moved to the Wii's, so the
 * ticks go out as microseconds of that clock (60.75 a microsecond). */
void gc_set_alarm(struct gc_rt_context* ctx, void* alarm, uint32_t ticks) {
    uint32_t* handle = (uint32_t*)alarm;
    uint32_t timespec[3];
    uint32_t us;
    if (ctx->ogc_create_alarm == 0) {
        ((gc_set_alarm_fn)(uintptr_t)ctx->set_alarm)(alarm, ticks, ctx->alarm_entry);
        return;
    }
    if (handle[1] != GC_OGC_ALARM_MADE) {
        if (((gc_ogc_create_alarm_fn)(uintptr_t)ctx->ogc_create_alarm)(handle) != 0) return;
        handle[1] = GC_OGC_ALARM_MADE;
    }
    us = ticks / 243u * 4u + (ticks % 243u) * 4u / 243u;
    if (us == 0) us = 1;
    timespec[0] = 0;
    timespec[1] = us / 1000000u;
    timespec[2] = (us % 1000000u) * 1000u;
    ((gc_ogc_set_alarm_fn)(uintptr_t)ctx->set_alarm)(handle[0], timespec, ctx->ogc_alarm_entry, alarm);
}

static void gc_arm(struct gc_rt_context* ctx, uint32_t ticks) {
    const uint32_t msr = gc_irq_off();
    if (!ctx->alarm_queued) {
        ctx->alarm_queued = 1;
        gc_set_alarm(ctx, ctx->alarm, ticks < ctx->delay_ticks ? ctx->delay_ticks : ticks);
    }
    gc_irq_restore(msr);
}
static void gc_complete_after(struct gc_rt_context* ctx, uint32_t cb, uint32_t type, uint32_t ticks) {
    const uint32_t msr = gc_irq_off();
    ctx->pending_cb = cb;
    ctx->pending_type = type;
    gc_arm(ctx, ticks);
    gc_irq_restore(msr);
}
static void gc_complete(struct gc_rt_context* ctx, uint32_t cb, uint32_t type) { gc_complete_after(ctx, cb, type, 0); }

/* How long a GameCube drive takes for a read, in timebase ticks (the Wii's
 * bus clock / 4, as the game is told): the seek, from nothing for a read
 * that follows the last one to about 105 ms across the whole disc, then
 * the transfer, the disc turning at a constant speed so the rate grows
 * from about 2 MB/s at its start to about 3 MB/s at its end. A read
 * served at the SD card's speed finished in microseconds, and some games
 * count on the drive's time (Nintendont emulates it too). A rough model,
 * not a measured one. */
#define GC_TICKS_PER_US_X4 243u  /* 60.75 ticks a microsecond */
static uint32_t gc_drive_ticks(struct gc_rt_context* ctx, uint32_t offset, uint32_t length) {
    const uint32_t distance = offset > ctx->last_read_end ? offset - ctx->last_read_end : ctx->last_read_end - offset;
    uint32_t seek_us = 0, rate_x10, transfer_us, us;
    if (distance >= 0x10000u) {
        seek_us = 20000u + (distance >> 14);
        if (seek_us > 105000u) seek_us = 105000u;
    }
    rate_x10 = 20u + (offset >> 27);  /* bytes a microsecond, times 10: 20 at the start, 30 at 1.4 GB */
    transfer_us = (length / rate_x10) * 10u + (length % rate_x10) * 10u / rate_x10;
    us = seek_us + transfer_us;
    if (us > 4000000u) us = 4000000u;
    return us / 4u * GC_TICKS_PER_US_X4 + (us % 4u) * GC_TICKS_PER_US_X4 / 4u;
}

/* A read's progress (ctx->read_state, gc_on_read). */
#define GC_READ_NONE 0u
#define GC_READ_COPYING 1u
#define GC_READ_PIECE 0x10000u  /* 64 KiB per job; IOS waits yield between alarms */

/* The heartbeat (gc_rt.h): once more, half a second from now. */
static void gc_beat_arm(struct gc_rt_context* ctx) { gc_set_alarm(ctx, ctx->beat_alarm, ctx->beat_ticks); }

/* Every DVDLow command: the first starts the heartbeat (the game's OS is
 * up by then, its alarms work), and the first few that are not reads get
 * a trace record too (reads write their own). */
static void gc_command_seen(struct gc_rt_context* ctx, int traced) {
    ctx->commands++;
    ctx->read_generation++;
    ctx->read_state = GC_READ_NONE;  /* a read still copying is given up: the game moved on (a reset) */
    if (ctx->commands == 1 && ctx->beat_ticks != 0) gc_beat_arm(ctx);
    if (traced && ctx->commands <= 32) gc_trace(ctx, GC_TRACE_COMMAND);
}

static void gc_stream_fetch(struct gc_rt_context* ctx);

/* DVDLowRead(addr, length, offset, cb). A drive takes the command and the
 * SDK's DVDLowRead returns at once: the thread that asked sleeps, and the
 * thread that queued the load runs on while the drive works. So the SD
 * card is not read here but from the alarm, a piece at a time
 * (gc_read_step). Read here, the copy kept the asking thread running for
 * as long as the SD card took, and what became ready meanwhile (the main
 * thread, at a retrace) then ran ahead of the thread that had queued the
 * load, so the read's callback could come before that thread had set up
 * what the callback uses. Luigi's Mansion (a Wii with fast disc reads,
 * and Dolphin with the SD card slowed to 8 ms a request): its loading
 * thread (priority 10) called a load's completion function at 0x80020104,
 * which the queuing code stores at 0x80020428 only after queuing the load
 * at 0x800203ec, and jumped to whatever the heap had there. */
static void gc_on_tgc_read(struct gc_rt_context* ctx, uint32_t offset, uint32_t length);
static uint32_t gc_read_begin(struct gc_rt_context* ctx, uint32_t addr, uint32_t length, uint32_t offset,
                              uint32_t cb, int disk_id) {
    gc_command_seen(ctx, disk_id);
    if (!disk_id) {
        ctx->reads++;
        ctx->read_bytes += length;
        ctx->last_offset = offset;
    }
    __asm__ volatile("mftb %0" : "=r"(ctx->read_started));
    ctx->read_drive_ticks = !disk_id && ctx->drive_timing ? gc_drive_ticks(ctx, offset, length) : 0;
    if (!disk_id) ctx->last_read_end = offset + length;
    ctx->read_addr = addr;
    ctx->read_length = length;
    ctx->read_offset = offset;
    ctx->read_cb = cb;
    ctx->read_failed = 0;
    ctx->read_tgc_watch = !disk_id;
    ctx->pending_cb = 0;  /* a completion still pending is this read's now */
    ctx->pending_type = 0;
    /* Queue the music ring's top-up while the card is free; neither this
     * hook nor PADRead waits for it. The copy starts after that job. */
    if (!disk_id) gc_stream_fetch(ctx);
    ctx->read_state = GC_READ_COPYING;
    /* ReadDiskID previously queued its completion as soon as its bytes
     * arrived, without seek/transfer emulation or a music top-up first.
     * Start it now: ready bytes still arrive before the hook returns,
     * while an unfinished IOS wait returns through the hook's caller. */
    if (disk_id) {
        const uint32_t msr = gc_irq_off();
        if (gc_io_start(ctx, GC_IO_READ)) gc_io_resume(ctx);
        else gc_arm(ctx, ctx->delay_ticks);
        gc_irq_restore(msr);
    } else {
        gc_arm(ctx, ctx->delay_ticks);
    }
    return 1;
}

uint32_t gc_on_read(struct gc_rt_context* ctx, uint32_t addr, uint32_t length, uint32_t offset, uint32_t cb) {
    return gc_read_begin(ctx, addr, length, offset, cb, 0);
}

/* On the worker's private stack, with interrupts off:
 * the next piece of the read. IOS waits return from the alarm, including
 * within a piece. With all bytes there, the callback still waits for the
 * drive's time. */
static void gc_read_step(struct gc_rt_context* ctx) {
    const uint32_t generation = ctx->read_generation;
    uint32_t piece = ctx->read_length < GC_READ_PIECE ? ctx->read_length : GC_READ_PIECE;
    uint32_t now, spent, wait;
#ifdef RT_RVZ
    /* From an RVZ a piece ends where its group does, so one turn of the
     * alarm decodes at most one group and the interrupts get their turn
     * between decodes. The decoding is part of the time the read takes:
     * the callback still waits for a drive's time on top only when that
     * is longer, and with Fast it comes as soon as the bytes are there. */
    if (ctx->rvz != 0) {
        const uint32_t chunk = ((const struct gc_rvz*)(uintptr_t)ctx->rvz)->chunk_bytes;
        const uint32_t to_edge = chunk - ctx->read_offset % chunk;
        if (piece > to_edge) piece = to_edge;
    }
#endif
    if (piece != 0 && !ctx->read_failed) {
        if (ctx->storage_busy) {
            /* The IPC buffers are taken (the way back is starting): later. */
            gc_arm(ctx, ctx->delay_ticks);
            return;
        }
        /* The TGC watch runs before the first bytes are served, with the
         * card free. It used to run at the hook and retry here if a
         * stream refill held the card; keeping it on the worker makes
         * its own SD waits asynchronous too. The Collector's Edition
         * menu reads a TGC header only once: never skip this watch. */
        if (ctx->read_tgc_watch && ctx->tgc_count != 0 && ctx->read_offset == ctx->last_offset)
            gc_on_tgc_read(ctx, ctx->read_offset, ctx->read_length);
        if (generation != ctx->read_generation) return;
        const int ok = gc_read_disc(ctx, (uint8_t*)(uintptr_t)ctx->io_buffer, piece, ctx->read_offset, 0);
        /* A reset or replacement command may have arrived while IOS was
         * working. Its buffers and progress belong to the new command. */
        if (generation != ctx->read_generation) return;
        if (ok && ctx->programs != 0) {
            /* A program the game loads itself, patched as it arrives. */
            const struct gc_programs* programs = (const struct gc_programs*)(uintptr_t)ctx->programs;
            ctx->program_patches += gc_programs_patch((const struct gc_disc_patch*)(uintptr_t)programs->patches,
                                                      programs->patch_count, (uint8_t*)(uintptr_t)ctx->io_buffer,
                                                      ctx->read_offset, piece);
        }
        if (ok) {
            gc_copy((uint8_t*)(uintptr_t)ctx->read_addr, (const uint8_t*)(uintptr_t)ctx->io_buffer, piece);
            gc_flush(ctx->read_addr, piece);
            ctx->read_addr += piece;
            ctx->read_offset += piece;
            ctx->read_length -= piece;
        } else {
            ctx->read_failed = 1;
        }
        if (ctx->read_length != 0 && !ctx->read_failed) {
            gc_arm(ctx, ctx->delay_ticks);
            return;
        }
    }
    ctx->read_state = GC_READ_NONE;
    if (ctx->read_tgc_watch && (ctx->reads <= 32 || (ctx->reads & 63) == 0)) gc_trace(ctx, GC_TRACE_READ);
    /* What the drive would still need after the time the SD card took. */
    __asm__ volatile("mftb %0" : "=r"(now));
    spent = now - ctx->read_started;
    wait = ctx->read_drive_ticks > spent ? ctx->read_drive_ticks - spent : 0;
    gc_complete_after(ctx, ctx->read_cb, ctx->read_failed ? GC_DI_DE : GC_DI_TC, wait);
}

/* Luigi's Mansion's file loader. JSystem's current heap is one global,
 * and the game's loading thread (priority 10) allocates from it whatever
 * a load needs: the file's buffer when it takes the request, then what
 * the load's callbacks decompress and build. It takes a request when the
 * last read finishes, so it runs then in place of whichever thread it
 * preempted, with that thread's heap current. With a drive's tens of
 * milliseconds a read, that was a quiet moment. Answered at the SD card's
 * speed it came while the model-building thread (priority 17) had made a
 * 512 KB solid heap its own for a room's 44 models (0x80174b3c), so a
 * file's buffer (Iwamoto/vrball_M.szp first) and what its callbacks made
 * went into that heap. In Dolphin the heap ran out and the models'
 * allocations came back null (a DSI at 0x801c4198, 0x801c4068 or
 * 0x801ca9c4); on a Wii a buffer freed with that heap stayed in use (an
 * ISI through a vtable of "JKRT" text). So each load remembers the heap
 * current where it was queued, and the loading thread allocates its
 * buffer with that heap current, then puts back the one it found.
 * Callbacks escape temporary heaps in the same way, but keep the game's
 * choice on its two main heaps and JKRHeap's root/system heaps. A load's
 * callback can have a longer lifetime than its buffer: game_usa.szp is
 * queued on the scene heap, but its global archive belongs on the
 * persistent heap the game selects after queueing it. Forcing that
 * callback onto the scene heap leaves a destroyed archive in use after
 * the scene's group cleanup (the progressive prompt exposed this). */
typedef uint32_t (*gc_game_fn0)(void);
typedef uint32_t (*gc_game_fn1)(uint32_t);
typedef uint32_t (*gc_game_fn2)(uint32_t, uint32_t);

/* A heap global in the game's small data (r13), found by the loader. */
static volatile uint32_t* gc_heap_global(uint32_t offset) {
    uint32_t r13;
    __asm__ volatile("mr %0,13" : "=r"(r13));
    return (volatile uint32_t*)(uintptr_t)(r13 + offset);
}

static volatile uint32_t* gc_current_heap(const struct gc_rt_context* ctx) {
    return gc_heap_global(ctx->load_heap_offset);
}

/* The remembered heap of `request`, 0: none. */
static uint32_t gc_load_heap(const struct gc_rt_context* ctx, uint32_t request) {
    uint32_t i;
    for (i = 0; i < GC_LOAD_SLOTS; ++i)
        if (ctx->load_request[i] == request) return ctx->load_heap[i];
    return 0;
}

/* In place of the queue's post(request), in the queuing code's context. */
uint32_t gc_on_load_queued(struct gc_rt_context* ctx, uint32_t request) {
    uint32_t slot = ctx->load_next;
    uint32_t i;
    for (i = 0; i < GC_LOAD_SLOTS; ++i)
        if (ctx->load_request[i] == request) slot = i;  /* requests come from a pool */
    ctx->load_request[slot] = request;
    ctx->load_heap[slot] = *gc_current_heap(ctx);
    if (slot == ctx->load_next) ctx->load_next = (slot + 1) % GC_LOAD_SLOTS;
    return ((gc_game_fn1)(uintptr_t)ctx->load_post)(request);
}

/* In place of the loading thread's alloc(length, alignment) for the buffer. */
uint32_t gc_on_load_alloc(struct gc_rt_context* ctx, uint32_t length, uint32_t alignment, uint32_t request) {
    const uint32_t heap = gc_load_heap(ctx, request);
    volatile uint32_t* current = gc_current_heap(ctx);
    const uint32_t found = *current;
    uint32_t allocated;
    if (heap != 0) *current = heap;
    allocated = ((gc_game_fn2)(uintptr_t)ctx->load_alloc)(length, alignment);
    if (heap != 0) {
        *current = found;
        ctx->loads_steered++;
    }
    return allocated;
}

/* In place of its group(id) before the load's callbacks. */
uint32_t gc_on_load_callbacks(struct gc_rt_context* ctx, uint32_t group, uint32_t unused, uint32_t request) {
    volatile uint32_t* current = gc_current_heap(ctx);
    const uint32_t found = *current;
    uint32_t roots[GC_LOAD_ROOT_HEAPS];
    uint32_t heap;
    uint32_t i;
    (void)unused;  /* the game's r4 */
    ctx->load_saved_heap = 0;
    for (i = 0; i < GC_LOAD_ROOT_HEAPS; ++i) {
        roots[i] = *gc_heap_global(ctx->load_root_heap_offsets[i]);
    }
    heap = gc_load_callback_heap(found, gc_load_heap(ctx, request), roots);
    if (heap != 0) {
        ctx->load_saved_heap = found;
        *current = heap;
    }
    return ((gc_game_fn1)(uintptr_t)ctx->load_group)(group);
}

/* And of group(old) after them. */
uint32_t gc_on_load_callbacks_done(struct gc_rt_context* ctx, uint32_t group) {
    if (ctx->load_saved_heap != 0) {
        *gc_current_heap(ctx) = ctx->load_saved_heap;
        ctx->load_saved_heap = 0;
    }
    return ((gc_game_fn1)(uintptr_t)ctx->load_group)(group);
}

uint32_t gc_on_disk_id(struct gc_rt_context* ctx, uint32_t id, uint32_t cb) {
    return gc_read_begin(ctx, id, 0x20, ctx->disc_id_offset, cb, 1);
}

/* DVDDriveInfo: revision, device code, release date, then padding. */
uint32_t gc_on_inquiry(struct gc_rt_context* ctx, uint32_t info, uint32_t cb) {
    uint32_t* words = (uint32_t*)(uintptr_t)info;
    uint32_t i;
    gc_command_seen(ctx, 1);
    for (i = 0; i < 8; ++i) words[i] = 0;
    words[0] = 0x00020201u;
    words[1] = 0x20020402u;
    gc_flush(info, 0x20);
    gc_complete(ctx, cb, GC_DI_TC);
    return 1;
}

uint32_t gc_on_done(struct gc_rt_context* ctx, uint32_t cb) {
    gc_command_seen(ctx, 1);
    gc_set_imm(ctx, 0);  /* RequestError: no error */
    gc_complete(ctx, cb, GC_DI_TC);
    return 1;
}

/* A frame buffer register's address: bits 0-23, shifted by 5 when bit 28
 * (the page offset) is set, as the SDK writes it. */
static uint32_t gc_vi_fb(uint32_t reg) {
    const uint32_t v = gc_read32(reg);
    return (v & 0x10000000u) ? (v & 0x00FFFFFFu) << 5 : (v & 0x00FFFFFFu);
}
/* Whether the VI shows a picture now: on, with active lines and a frame
 * buffer in MEM1. The SDK blanks the screen by setting no active lines
 * (and longer blanking around them): from VIInit until the game's first
 * frame, and whenever the game calls VISetBlack. */
static int gc_vi_showing(void) {
    const uint16_t vtr = *(volatile uint16_t*)(uintptr_t)GC_VI_VTR;
    const uint16_t dcr = *(volatile uint16_t*)(uintptr_t)GC_VI_DCR;
    const uint32_t top = gc_vi_fb(GC_VI_TFBL);
    return (dcr & 1u) && GC_VI_ACTIVE_LINES(vtr) != 0 && top != 0 && top < 0x01800000u;
}
/* Where gc_rt_context.vi_shown's word `i` is, from the VI's base. */
static uint32_t gc_vi_saved_offset(uint32_t i) {
    if (i < 11u) return 4u * i;
    return i == 11u ? 0x48u : 0x6Cu;
}
/* The registers into vi_shown, when they show a picture. */
static void gc_vi_note(struct gc_rt_context* ctx) {
    uint32_t i;
    if (!gc_vi_showing()) return;
    for (i = 0; i < GC_VI_SAVED_WORDS; ++i) {
        const uint32_t at = GC_VI_BASE + gc_vi_saved_offset(i);
        const uint32_t high = *(volatile uint16_t*)(uintptr_t)at;
        const uint32_t low = *(volatile uint16_t*)(uintptr_t)(at + 2u);
        ctx->vi_shown[i] = high << 16 | low;
    }
}
/* vi_shown back into the VI, the timing last, when it holds a picture.
 * The VI takes it at its next field. */
static void gc_vi_put_back(const struct gc_rt_context* ctx) {
    uint32_t i;
    if (GC_VI_ACTIVE_LINES(ctx->vi_shown[0] >> 16) == 0) return;  /* nothing noted */
    for (i = GC_VI_SAVED_WORDS; i-- > 0;) {
        const uint32_t at = GC_VI_BASE + gc_vi_saved_offset(i);
        *(volatile uint16_t*)(uintptr_t)(at + 2u) = (uint16_t)ctx->vi_shown[i];
        *(volatile uint16_t*)(uintptr_t)at = (uint16_t)(ctx->vi_shown[i] >> 16);
    }
}

/* The hand-over to the next title (gc_leave). That title's libogc finds
 * the VI still on and keeps going from the mode it sees there, but the
 * display control register only says interlaced or not: a progressive
 * picture at 54 MHz (VI_CLK, 0x6C), which the game set up for 480p, reads
 * as a 240-line one, the likely reason the Homebrew Channel ran with the
 * TV black on a Wii set to 480p (seen on 18 Oct). So the VI goes back to the registers RiftWii's own
 * libogc left at the jump (vi_at_jump: the game's TV format, interlaced,
 * 27 MHz), blanked, as a libogc program leaves it when it starts another. */
#define GC_VI_REGISTERS (GC_VI_HANDOVER_WORDS * 2u)  /* 16-bit registers from 0x00 */
#define GC_VI_DCR_RESET 0x0002u                       /* DCR bit 1: the VI resets */
#define GC_VI_DI_STATUS 0x8000u                       /* a display interrupt's status bit (0x30-0x3C, upper halves) */
static uint32_t gc_vi_register(const uint32_t* words, uint32_t r) {
    const uint32_t word = words[r / 2u];
    return (r & 1u) ? (word & 0xFFFFu) : (word >> 16);
}
static void gc_vi_set_register(uint32_t* words, uint32_t r, uint32_t value) {
    uint32_t* word = &words[r / 2u];
    *word = (r & 1u) ? ((*word & 0xFFFF0000u) | (value & 0xFFFFu)) : ((*word & 0xFFFFu) | value << 16);
}
static void gc_vi_read_all(uint32_t* words) {
    uint32_t r;
    for (r = 0; r < GC_VI_REGISTERS; r += 2u) {
        const uint32_t high = *(volatile uint16_t*)(uintptr_t)(GC_VI_BASE + 2u * r);
        const uint32_t low = *(volatile uint16_t*)(uintptr_t)(GC_VI_BASE + 2u * r + 2u);
        words[r / 2u] = high << 16 | low;
    }
}
/* Registers that are not settings: the beam's position (0x2C, 0x2E), the
 * light gun latches (0x40-0x46) and 0x6E, whose bit 0 is the cable. */
static int gc_vi_is_setting(uint32_t r) {
    const uint32_t offset = 2u * r;
    if (offset == 0x2Cu || offset == 0x2Eu || offset == 0x6Eu) return 0;
    return offset < 0x40u || offset > 0x46u;
}
/* `words` into the VI as a libogc start-up does it: a reset, every
 * setting with the display off, the display control last. A display
 * interrupt's status bit is written as 0, which clears it. */
static void gc_vi_write_all(const uint32_t* words) {
    volatile uint16_t* const dcr = (volatile uint16_t*)(uintptr_t)GC_VI_DCR;
    uint32_t r, spin;
    *dcr = GC_VI_DCR_RESET;
    for (spin = 0; spin < 1000u; ++spin) __asm__ volatile("" ::: "memory");
    *dcr = 0;
    for (r = 0; r < GC_VI_REGISTERS; ++r) {
        uint32_t value = gc_vi_register(words, r);
        const uint32_t offset = 2u * r;
        if (offset == 0x02u || !gc_vi_is_setting(r)) continue;
        if (offset >= 0x30u && offset <= 0x3Cu && (offset & 2u) == 0) value &= ~GC_VI_DI_STATUS;
        *(volatile uint16_t*)(uintptr_t)(GC_VI_BASE + offset) = (uint16_t)value;
    }
    *dcr = (uint16_t)gc_vi_register(words, 1u);
}
/* The mode in `words` with no active lines: the lines go to the blanking
 * before and after them, so the timing stays the same (VTR 0x00: lines
 * per field in bits 4-13; 0x0C and 0x10 the blanking after each field,
 * 0x0E and 0x12 before it). The VI then reads no frame buffer at all. */
static void gc_vi_blank(uint32_t* words) {
    const uint32_t vtr = gc_vi_register(words, 0u);
    const uint32_t lines = GC_VI_ACTIVE_LINES(vtr);
    uint32_t r;
    if (lines == 0) return;
    gc_vi_set_register(words, 0u, vtr & ~(0x3FFu << 4));
    for (r = 0x0Cu / 2u; r <= 0x12u / 2u; ++r) {
        const int before_field = (r & 1u) != 0;  /* 0x0E and 0x12 */
        const uint32_t now = gc_vi_register(words, r);
        gc_vi_set_register(words, r, before_field ? now + 2u * lines - 2u : now + 2u);
    }
}
/* The screen blank from the jump to the game's first picture, in the
 * mode noted at the jump: the blanking registers first, the VTR last, so
 * the retrace interrupts keep their lines. Left showing, RiftWii's last
 * frame went green for about a second before Super Mario Sunshine's first
 * picture on Jack's Wii in 480p (21 Oct): the game clears that memory
 * before it blanks the screen itself, and memory of zeros is green in
 * YUYV. In 480i the TV's re-sync to the game's mode hid it. A Wii game's
 * launch blanks the screen the same way (VIDEO_SetBlack). */
static void gc_vi_blank_now(const struct gc_rt_context* ctx) {
    uint32_t mode[GC_VI_HANDOVER_WORDS];
    uint32_t r;
    if (gc_vi_register(ctx->vi_at_jump, 1u) == 0) return;  /* nothing noted at the jump */
    for (r = 0; r < GC_VI_HANDOVER_WORDS; ++r) mode[r] = ctx->vi_at_jump[r];
    gc_vi_blank(mode);
    for (r = 0x12u / 2u; r >= 0x0Cu / 2u; --r)
        *(volatile uint16_t*)(uintptr_t)(GC_VI_BASE + 2u * r) = (uint16_t)gc_vi_register(mode, r);
    *(volatile uint16_t*)(uintptr_t)GC_VI_VTR = (uint16_t)gc_vi_register(mode, 0u);
}
/* Before ES_LaunchTitle: RiftWii's own mode back, blank. `saved` gets the
 * game's registers, for gc_vi_take_back when IOS says no. */
static void gc_vi_hand_over(const struct gc_rt_context* ctx, uint32_t* saved) {
    uint32_t mode[GC_VI_HANDOVER_WORDS];
    uint32_t i;
    gc_vi_read_all(saved);
    if (gc_vi_register(ctx->vi_at_jump, 1u) == 0) return;  /* nothing noted at the jump */
    for (i = 0; i < GC_VI_HANDOVER_WORDS; ++i) mode[i] = ctx->vi_at_jump[i];
    gc_vi_blank(mode);
    gc_vi_write_all(mode);
}
static void gc_vi_take_back(const struct gc_rt_context* ctx, const uint32_t* saved) {
    if (gc_vi_register(ctx->vi_at_jump, 1u) == 0) return;
    gc_vi_write_all(saved);
}
/* The restart (gc_on_hot_reset): the VI as the first start found it, on,
 * in the game's TV format, blanked. The SDK's hot reset switches the VI
 * off (display control 0) before it resets, and on a GameCube the IPL then
 * sets it up again; here nothing does, so VIInit found it off and set it
 * up as NTSC, writing NTSC into 0x800000CC as well. Doshin the Giant PAL,
 * restarted through its own OSResetSystem(0) (made a hot reset by
 * gc_on_reset_system), then never got a retrace interrupt and waited in
 * VIWaitForRetrace for good, the screen black (Dolphin). With the VI on,
 * VIInit keeps it as at the first start. */
static void gc_vi_restart(const struct gc_rt_context* ctx) {
    uint32_t mode[GC_VI_HANDOVER_WORDS];
    uint32_t i;
    if (gc_vi_register(ctx->vi_at_jump, 1u) == 0) return;  /* nothing noted at the jump */
    for (i = 0; i < GC_VI_HANDOVER_WORDS; ++i) mode[i] = ctx->vi_at_jump[i];
    gc_vi_blank(mode);
    gc_vi_write_all(mode);
}

typedef void (*gc_callback_fn)(uint32_t type);
static void gc_di_raise(struct gc_rt_context* ctx, uint32_t type);
static void gc_disc_on_alarm(struct gc_rt_context* ctx, uint32_t os_context);
static void gc_disc_motor_stop(struct gc_rt_context* ctx);
/* An OSContext's saved LR, SRR0 and SRR1 (the SDK's layout). */
#define GC_OSCONTEXT_LR 0x84u
#define GC_OSCONTEXT_SRR0 0x198u
#define GC_OSCONTEXT_SRR1 0x19Cu

/* From the heartbeat: a game on the USB drive reads one sector of its
 * image when the drive has been quiet for usb_keep_ticks. Many hard
 * drives spin down after a few idle minutes, and the next read then
 * takes seconds, which a game sitting on a menu or with a level loaded
 * can turn into a disc error. Only while nothing else reads. */
static void gc_usb_keep_alive(struct gc_rt_context* ctx) {
    uint32_t now;
    if (ctx->usb_fd == 0xFFFFFFFFu || ctx->usb_keep_ticks == 0) return;
    if (ctx->storage_busy || ctx->io_pending != 0 || ctx->ipc_sending || ctx->read_state != GC_READ_NONE) return;
    __asm__ volatile("mftb %0" : "=r"(now));
    if (now - ctx->usb_last_read < ctx->usb_keep_ticks) return;
    ctx->storage_busy = 1;
    ctx->usb_keepalives++;
    gc_usb_read(ctx, ctx->usb_keep_sector, 1, ctx->bounce);
    ctx->storage_busy = 0;
}

void gc_on_alarm(struct gc_rt_context* ctx, uint32_t alarm, uint32_t os_context) {
    const uint32_t cb = ctx->pending_cb;
    if (alarm == (uint32_t)(uintptr_t)ctx->io_alarm) {
        ctx->io_queued = 0;
        if (ctx->io_job != 0) gc_io_resume(ctx);
        else if (ctx->io_stream_restart) {
            if (!gc_io_start(ctx, GC_IO_STREAM)) gc_io_arm(ctx);
        } else if (ctx->io_trace_where != 0) gc_io_start(ctx, GC_IO_TRACE);
        else if (ctx->io_maintenance && ctx->read_state == GC_READ_NONE) gc_io_start(ctx, GC_IO_BEAT);
        return;
    }
    if (alarm == (uint32_t)(uintptr_t)ctx->beat_alarm) {
        ctx->beats++;
        if (os_context != 0) {
            ctx->beat_pc = *(const uint32_t*)(uintptr_t)(os_context + GC_OSCONTEXT_SRR0);
            ctx->beat_lr = *(const uint32_t*)(uintptr_t)(os_context + GC_OSCONTEXT_LR);
            ctx->beat_msr = *(const uint32_t*)(uintptr_t)(os_context + GC_OSCONTEXT_SRR1);
        }
        gc_guard(ctx);
        gc_vi_note(ctx);
        if (ctx->bt && !(ctx->flags & GC_FLAG_FAKE_REMOTE)) {
            struct gc_bt* bt = (struct gc_bt*)(uintptr_t)ctx->bt;
            if (bt->stage == GC_BT_STAGE_READY && !gc_bt_wants_frame_polls(bt)) gc_bt_poll(ctx, bt);
        }
        gc_io_start(ctx, GC_IO_BEAT);
        gc_beat_arm(ctx);
        return;
    }
    if (ctx->disc_change != 0 &&
        alarm == (uint32_t)(uintptr_t)((struct gc_disc_change*)(uintptr_t)ctx->disc_change)->alarm) {
        gc_disc_on_alarm(ctx, os_context);
        return;
    }
    if (ctx->card && alarm == (uint32_t)(uintptr_t)((struct gc_card*)(uintptr_t)ctx->card)->alarm) {
        gc_card_on_alarm(ctx, (struct gc_card*)(uintptr_t)ctx->card, os_context);
        return;
    }
    ctx->alarm_queued = 0;  /* out of the OS's queue now: gc_arm may set it again */
    if (ctx->read_state == GC_READ_COPYING) {
        if (!gc_io_start(ctx, GC_IO_READ)) {
            uint32_t now;
            __asm__ volatile("mftb %0" : "=r"(now));
            /* A dead request keeps its DMA memory quarantined until IOS
             * replies. At ten seconds, queue the waiting read's drive
             * error rather than polling forever while storage is down. */
            if (ctx->io_job == 0 && (ctx->io_pending != 0 || ctx->ipc_sending) &&
                now - ctx->read_started >= 2u * GC_IO_TIMEOUT_TICKS) {
                ctx->read_failed = 1;
                ctx->read_state = GC_READ_NONE;
                gc_complete(ctx, ctx->read_cb, GC_DI_DE);
            } else {
                gc_arm(ctx, GC_IO_POLL_TICKS);
            }
        }
        return;
    }
    if (ctx->io_job == GC_IO_READ) {
        /* A replacement command cannot finish ahead of the old DMA. */
        gc_arm(ctx, GC_IO_POLL_TICKS);
        return;
    }
    {
        const uint32_t type = ctx->pending_type;
        ctx->alarms++;
        ctx->pending_cb = 0;
        ctx->pending_type = 0;
        /* A finished transfer leaves the DI's length register at 0, the
         * bytes it still had to move; the SDK's callback counts what was
         * read as the length it asked for minus that register. The hooked
         * function never writes it, but an SDK whose DVDLowRead writes the
         * length before calling an inner function (the one found and
         * hooked) would leave the whole length there, and every read
         * would look as if nothing came: the SDK reads the same block
         * forever. */
        if (type == GC_DI_TC && ctx->di_regs != 0) *(volatile uint32_t*)(uintptr_t)(ctx->di_regs + 0x18) = 0;
        if (cb == GC_CB_DI_RAISE) gc_di_raise(ctx, type);
        else if (cb != 0) ((gc_callback_fn)(uintptr_t)cb)(type);
    }
}

/* ---- The DI at its registers (libogc) ------------------------------ */

/* The DI's registers, as words of the stand-in (yagcd, wiibrew). */
#define GC_DI_SR 0      /* DISR: BRK 0, DEINTMASK 1, DEINT 2, TCINTMASK 3, TCINT 4, BRKINTMASK 5, BRKINT 6 */
#define GC_DI_CMD0 2
#define GC_DI_CMD1 3
#define GC_DI_CMD2 4
#define GC_DI_MAR 5
#define GC_DI_LENGTH 6
#define GC_DI_CR 7      /* DICR: start 0, DMA 1, write 2 */
#define GC_DI_IMM 8
#define GC_DISR_DEINT 0x04u
#define GC_DISR_TCINT 0x10u
#define GC_DISR_BRKINT 0x40u
#define GC_DI_IRQ 21u   /* libogc's IRQ_PI_DI, the handler's first argument */

typedef void (*gc_irq_handler_fn)(uint32_t irq, void* context);

/* The command has finished: DICR's start bit drops, DISR gets the
 * interrupt the drive would raise (its masks stay as the game set them),
 * and the game's DI interrupt handler runs, as from the interrupt. */
static void gc_di_raise(struct gc_rt_context* ctx, uint32_t type) {
    volatile uint32_t* di = (volatile uint32_t*)(uintptr_t)ctx->di_regs;
    di[GC_DI_CR] &= ~1u;
    di[GC_DI_SR] |= type == GC_DI_TC ? GC_DISR_TCINT : GC_DISR_DEINT;
    if (ctx->di_handler != 0) ((gc_irq_handler_fn)(uintptr_t)ctx->di_handler)(GC_DI_IRQ, 0);
}

/* A DICR store with the start bit (the slot has made it): the command in
 * the stand-in's registers, carried out from the card like the DVDLow
 * hooks' (gc_on_read) and finished from the alarm. Commands that only a
 * drive's firmware knows (libogc's unlock, firmware writes and calls,
 * motor control, offsets, extensions, status) finish as done; the drive
 * information names a Wii's drive, which libogc does not patch. Reads of
 * an immediate (the error, the audio status) get 0: no error, no stream. */
void gc_on_di_start(struct gc_rt_context* ctx) {
    volatile uint32_t* di = (volatile uint32_t*)(uintptr_t)ctx->di_regs;
    const uint32_t control = di[GC_DI_CR];
    const uint32_t command = di[GC_DI_CMD0];
    const uint32_t addr = (di[GC_DI_MAR] & 0x01FFFFFFu) | 0x80000000u;
    const uint32_t length = di[GC_DI_LENGTH];
    if (!(control & 1u)) return;
    switch (command >> 24) {
    case 0xA8:
        if ((command & 0xFFu) == 0x40) gc_on_read(ctx, addr, length, ctx->disc_id_offset, GC_CB_DI_RAISE);  /* the disc ID */
        else gc_on_read(ctx, addr, length, di[GC_DI_CMD1] << 2, GC_CB_DI_RAISE);
        return;
    case 0x12: {
        /* Inquiry: revision 2, device 0, a release date whose month byte
         * is 05 (a Wii's drive), the rest zero. */
        uint32_t* info = (uint32_t*)(uintptr_t)addr;
        uint32_t i;
        if (length >= 0x20) {
            info[0] = 0x00020000u;
            info[1] = 0x20060526u;
            for (i = 2; i < 8; ++i) info[i] = 0;
        }
        di[GC_DI_LENGTH] = 0;
        break;
    }
    case 0xE3:
        /* Stop motor: a two-disc game's change starts (gc_disc.h); for
         * any other game, answered as the default below. */
        gc_disc_motor_stop(ctx);
        if (control & 2u) di[GC_DI_LENGTH] = 0;
        di[GC_DI_IMM] = 0;
        break;
    default:
        if (control & 2u) di[GC_DI_LENGTH] = 0;
        di[GC_DI_IMM] = 0;
        break;
    }
    gc_command_seen(ctx, 1);
    gc_complete(ctx, GC_CB_DI_RAISE, GC_DI_TC);
}

/* ---- A two-disc game's disc change (gc_disc.h) ---------------------- */

/* The SDK's interrupt handlers, a word for each of the OS's 32 interrupts
 * (OSInterrupt's table at 0x80003040, physical 0x3040); the DI's is
 * number 21, as libogc's IRQ_PI_DI. */
#define GC_OS_INTERRUPT_TABLE 0x80003040u
#define GC_DISC_HANDLER_LOW 0x80003100u  /* a handler is game code, above low memory */
#define GC_DISC_HANDLER_HIGH 0x81800000u
/* How soon to look again when a read or a completion is under way as the
 * other disc is due: about a millisecond. */
#define GC_DISC_BUSY_TICKS 60750u

/* The disc in now serves every read: its runs and its RVZ reader. The
 * RVZ readers share their buffers (gcnative.cpp), so the one coming in
 * forgets what it had decoded there. */
static void gc_disc_insert(struct gc_rt_context* ctx, struct gc_disc_change* dc) {
    const struct gc_disc_image* disc = &dc->discs[dc->current];
    ctx->runs = disc->runs;
    ctx->run_count = disc->run_count;
    ctx->rvz = disc->rvz;
    ctx->last_read_end = 0;  /* the new disc's first read seeks */
#ifdef RT_RVZ
    if (ctx->rvz != 0) {
        struct gc_rvz* rvz = (struct gc_rvz*)(uintptr_t)ctx->rvz;
        uint32_t s;
        for (s = 0; s < GC_RVZ_SLOTS; ++s) rvz->slots[s].group = GC_RVZ_NO_GROUP;
        rvz->entry_sector = GC_RVZ_NO_GROUP;
    }
#endif
}

/* The motor stopped (DVDLowStopMotor, or libogc's command 0xE3): with a
 * second disc, the cover opens and the disc-change alarm is set. The
 * drive stops playing disc audio when its motor stops. */
static void gc_disc_motor_stop(struct gc_rt_context* ctx) {
    struct gc_disc_change* dc = (struct gc_disc_change*)(uintptr_t)ctx->disc_change;
    uint32_t msr;
    uint32_t now;
    if (dc == 0) return;
    ctx->stream_generation++;
    if (ctx->stream != 0) ((struct gc_rt_stream*)(uintptr_t)ctx->stream)->playing = 0;
    msr = gc_irq_off();
    __asm__ volatile("mftb %0" : "=r"(now));
    if (gc_disc_motor_stopped(dc, now)) gc_set_alarm(ctx, dc->alarm, dc->cover_ticks);
    gc_irq_restore(msr);
    gc_trace(ctx, GC_TRACE_DISC);
}

uint32_t gc_on_stop_motor(struct gc_rt_context* ctx, uint32_t cb) {
    gc_disc_motor_stop(ctx);
    return gc_on_done(ctx, cb);
}

/* The cover interrupt, for a game waiting on the cover: DICVR's interrupt
 * bit, then the game's DI interrupt handler as the interrupt would run it
 * (libogc's, found by the loader, or the one the SDK's DVDInit put in the
 * OS's table), with the context the alarm interrupted. The stand-in is
 * memory, not registers, so the bits the drive clears when the game
 * writes a one to them are cleared here: the transfer ones first (every
 * completion has reached the game by now, gc_disc_on_alarm waits for
 * that), the cover's once the handler has taken it. */
static void gc_disc_raise_cover(struct gc_rt_context* ctx, uint32_t os_context) {
    volatile uint32_t* di = (volatile uint32_t*)(uintptr_t)ctx->di_regs;
    uint32_t handler = ctx->di_handler;
    if (handler == 0) handler = gc_read32(GC_OS_INTERRUPT_TABLE + 4u * GC_DI_IRQ);
    di[GC_DI_SR] &= ~(GC_DISR_TCINT | GC_DISR_DEINT | GC_DISR_BRKINT);
    di[GC_DI_CVR] |= GC_DICVR_INT;
    if (handler < GC_DISC_HANDLER_LOW || handler >= GC_DISC_HANDLER_HIGH || (handler & 3u) != 0) return;
    ((gc_irq_handler_fn)(uintptr_t)handler)(GC_DI_IRQ, (void*)(uintptr_t)os_context);
    di[GC_DI_CVR] &= ~GC_DICVR_INT;
}

/* The disc-change alarm: the other disc goes in when it is due, and the
 * game hears the cover close (gc_disc_cover_alarm says what to do). Not
 * in the middle of a read, nor before a command's completion has reached
 * the game: the change waits a moment for those. */
static void gc_disc_on_alarm(struct gc_rt_context* ctx, uint32_t os_context) {
    struct gc_disc_change* dc = (struct gc_disc_change*)(uintptr_t)ctx->disc_change;
    volatile uint32_t* di = (volatile uint32_t*)(uintptr_t)ctx->di_regs;
    uint32_t now;
    uint32_t ticks;
    uint32_t todo;
    if (ctx->storage_busy || ctx->io_pending != 0 || ctx->ipc_sending || ctx->io_job != 0 ||
        ctx->read_state != GC_READ_NONE || ctx->pending_cb != 0) {
        gc_set_alarm(ctx, dc->alarm, GC_DISC_BUSY_TICKS);  /* still queued, as far as gc_disc.h knows */
        return;
    }
    __asm__ volatile("mftb %0" : "=r"(now));
    todo = gc_disc_cover_alarm(dc, now, di[GC_DI_CVR], &ticks);
    if (todo & GC_DISC_SWAPPED) gc_disc_insert(ctx, dc);
    if (todo & GC_DISC_LATCH) di[GC_DI_CVR] |= GC_DICVR_INT;
    if (todo & GC_DISC_RAISE) gc_disc_raise_cover(ctx, os_context);
    if (todo & GC_DISC_AGAIN) gc_set_alarm(ctx, dc->alarm, ticks);
    /* A record for each step but the quiet looks in between. */
    if (todo & (GC_DISC_SWAPPED | GC_DISC_RAISE | GC_DISC_LATCH)) gc_trace(ctx, GC_TRACE_DISC);
}

/* ---- Disc audio streaming ------------------------------------------ */

#define GC_ADP_BLOCK 32u    /* 28 stereo samples */

static void gc_stream_reset(struct gc_rt_stream* st) {
    st->hist[0] = st->hist[1] = st->hist[2] = st->hist[3] = 0;
    st->sample = 28;
    st->phase = 0;
    st->fetched = 0;
    st->consumed = 0;
    st->ended = 0;
}

/* One worker top-up of the ring: up to GC_STREAM_CHUNK bytes, never
 * across the ring's end. Only fully fetched bytes become visible to the
 * AI decoder. Moves on to the queued stream when one ends. */
static void gc_stream_fill(struct gc_rt_context* ctx) {
    struct gc_rt_stream* st = (struct gc_rt_stream*)(uintptr_t)ctx->stream;
    const uint32_t generation = ctx->stream_generation;
    uint32_t buffered;
    uint32_t free_bytes;
    uint32_t at;
    uint32_t n;
    if (st == 0 || !st->playing || st->ended) return;
    if (st->pos >= st->start + st->length) {
        if (st->next_length == 0) {
            st->ended = 1;
            return;
        }
        st->start = st->next_start;
        st->length = st->next_length;
        st->pos = st->start;
    }
    buffered = st->fetched - st->consumed;
    free_bytes = st->buf_bytes - buffered;
    /* Fewer, larger reads: each is one IOS request, a millisecond or more
     * on a Wii. Whole sectors keep them going straight into the ring (a
     * stream starts on a sector, as the drive's own blocks do). */
    if (free_bytes < GC_STREAM_CHUNK / 2) return;
    at = st->fetched % st->buf_bytes;
    n = GC_STREAM_CHUNK;
    if (n > free_bytes) n = free_bytes;
    if (n > st->buf_bytes - at) n = st->buf_bytes - at;
    n &= ~(GC_SECTOR - 1);
    if (n > st->start + st->length - st->pos) n = (st->start + st->length - st->pos) & ~(GC_ADP_BLOCK - 1);
    if (n == 0) return;
    const int ok = gc_read_disc(ctx, (uint8_t*)(uintptr_t)ctx->io_buffer, n, st->pos, 1);
    if (generation != ctx->stream_generation) return;
    if (!ok) {
        st->fetch_busy++;
        return;
    }
    gc_copy((uint8_t*)(uintptr_t)(st->buf + at), (const uint8_t*)(uintptr_t)ctx->io_buffer, n);
    st->pos += n;
    st->fetched += n;  /* last: the decoder may take these bytes from now on */
    st->refills++;
}

/* PADRead and DVDLow* only request a top-up. They may themselves run
 * inside an audio/retrace interrupt, so none of them waits for storage. */
static void gc_stream_fetch(struct gc_rt_context* ctx) {
    const struct gc_rt_stream* st = (const struct gc_rt_stream*)(uintptr_t)ctx->stream;
    if (st != 0 && st->playing && !st->ended) gc_io_start(ctx, GC_IO_STREAM);
}

static int gc_io_start(struct gc_rt_context* ctx, uint32_t job) {
    const uint32_t msr = gc_irq_off();
    if (ctx->io_job == 0 && (ctx->io_pending != 0 || ctx->ipc_sending)) {
        uint32_t reply;
        while ((reply = gc_ipc_take()) != 0) gc_ipc_route(ctx, reply);
        /* The reply may have been routed before the send timed out, or
         * arrive before its late Y2. Recover these two owners independently. */
        if (ctx->io_reply) ctx->io_pending = 0;
        /* A send timeout may leave Y2 unconsumed. Its continuation is
         * gone now, so this recovery path owns the final acknowledgment. */
        if (gc_read32(GC_IPC_PPCCTRL) & GC_IPC_Y2) {
            gc_write32(GC_IPC_PPCCTRL, GC_IPC_Y2);
            ctx->ipc_sending = 0;
        }
    }
    if (ctx->io_stack == 0 || ctx->io_job != 0 || ctx->storage_busy || ctx->io_pending != 0 || ctx->ipc_sending) {
        gc_irq_restore(msr);
        return 0;
    }
    if (ctx->io_sp == 0) gc_io_init(&ctx->io_sp, ctx->io_stack + GC_IO_STACK_BYTES, ctx);
    ctx->io_job = job;
    if (job == GC_IO_STREAM) ctx->io_stream_restart = 0;
    gc_io_arm(ctx);
    gc_irq_restore(msr);
    return 1;
}

static void gc_io_resume(struct gc_rt_context* ctx) {
    __asm__ volatile("mftb %0" : "=r"(ctx->io_ticks));
    ctx->io_running = 1;
    gc_io_switch(&ctx->io_caller_sp, ctx->io_sp);
    if (ctx->io_job != 0 || ctx->io_trace_where != 0 ||
        (ctx->io_maintenance && ctx->read_state == GC_READ_NONE)) gc_io_arm(ctx);
}

/* Entered once on the loader's MEM2 stack, never on the game's stack.
 * A job's C frames survive an IOS wait. Only the alarm's small caller
 * frame (or ReadDiskID's initial hook frame) is unwound, letting the OS
 * service AI/DSP and run game threads. */
void gc_io_worker(struct gc_rt_context* ctx) {
    for (;;) {
        if (ctx->io_job == GC_IO_READ) {
            if (ctx->read_state == GC_READ_COPYING) gc_read_step(ctx);
        } else if (ctx->io_job == GC_IO_STREAM) {
            gc_stream_fill(ctx);
            gc_stream_fill(ctx);
        } else if (ctx->io_job == GC_IO_BEAT) {
            struct gc_card* card = (struct gc_card*)(uintptr_t)ctx->card;
            const uint32_t before = card != 0 ? card->dirty_count : 0;
            if (card != 0) gc_card_flush(ctx, card, 0);
            /* One bounded batch per job, so a whole-card erase cannot
             * monopolize the worker while a game is waiting for a read. */
            ctx->io_maintenance = card != 0 && card->dirty_count != 0 && card->dirty_count < before;
            gc_usb_keep_alive(ctx);
            if ((ctx->flags & GC_FLAG_PAD_TRACE_PENDING) && gc_trace(ctx, GC_TRACE_PAD_SI) != GC_TRACE_SKIPPED)
                ctx->flags &= ~GC_FLAG_PAD_TRACE_PENDING;
            gc_trace(ctx, GC_TRACE_BEAT);
        } else if (ctx->io_job == GC_IO_TRACE) {
            const uint32_t where = ctx->io_trace_where;
            ctx->io_trace_where = 0;
            if (where != 0) gc_trace(ctx, where);
        }
        ctx->io_job = 0;
        gc_io_pause(ctx);
    }
}

/* A game-driven reset/exec can arrive while the worker is suspended.
 * Its OS may stop delivering alarms next. Drain the one DMA in place
 * before dropping the continuation or resetting its buffers; saved card
 * sectors remain dirty and the exit's forced flush writes them again.
 * A failed device gets five seconds; failure keeps its buffers quarantined.
 * keep_read is for the pad hook's leave: a failed save or refused launch
 * returns to that same game, so its read and music top-up must restart. */
static int gc_io_drain(struct gc_rt_context* ctx, int keep_read) {
    uint32_t msr;
    uint32_t started;
    int storage_ready = 1;
    msr = gc_irq_off();
    __asm__ volatile("mftb %0" : "=r"(started));
    /* A worker's TGC/leave trace can itself time out. Finish that DMA
     * before another storage operation reuses the shared buffers, but
     * keep the active worker's stack if IOS refuses the launch. */
    while (ctx->ipc_sending || (ctx->io_pending != 0 && !ctx->io_reply)) {
        uint32_t now;
        const uint32_t reply = gc_ipc_take();
        if (reply != 0) gc_ipc_route(ctx, reply);
        if (gc_read32(GC_IPC_PPCCTRL) & GC_IPC_Y2) {
            gc_write32(GC_IPC_PPCCTRL, GC_IPC_Y2);
            ctx->ipc_sending = 0;
        }
        __asm__ volatile("mftb %0" : "=r"(now));
        if (now - started >= GC_IO_TIMEOUT_TICKS) {
            /* IOS still owns this memory. Neither a reset nor diagnostics
             * may reuse it merely because the deadline expired. */
            storage_ready = 0;
            break;
        }
    }
    if (!storage_ready && !keep_read) {
        gc_irq_restore(msr);
        return 0;
    }
    if (storage_ready) ctx->io_pending = 0;
    if (ctx->io_running) {
        gc_irq_restore(msr);
        return storage_ready;
    }
    ctx->read_generation++;
    ctx->stream_generation++;
    if (keep_read) {
        /* HOME/Reset from PADRead can return after a failed save or ES
         * refusal. Discard only the continuation: staged bytes have not
         * advanced read progress or the ring, so the piece can be redone. */
        if (ctx->io_job == GC_IO_STREAM) ctx->io_stream_restart = 1;
        if (ctx->read_state != GC_READ_NONE || ctx->pending_type != 0) gc_arm(ctx, GC_IO_POLL_TICKS);
        if (ctx->io_stream_restart) gc_io_arm(ctx);
    } else {
        ctx->read_state = GC_READ_NONE;
        ctx->pending_cb = 0;
        ctx->pending_type = 0;
        ctx->io_stream_restart = 0;
    }
    ctx->io_sp = 0;
    ctx->io_job = 0;
    if (storage_ready) ctx->io_reply = 0;
    ctx->io_maintenance = 0;
    ctx->io_trace_where = 0;
    ctx->storage_busy = 0;
    ctx->io_buffers_idle = 0;
    /* Leave an already queued io_alarm linked until it fires, or until
     * the old OS is discarded. Queuing that same alarm twice corrupts
     * the SDK's alarm list. */
    gc_irq_restore(msr);
    return storage_ready;
}

/* E1: set the stream (or the next one), or cancel it. */
uint32_t gc_on_audio_stream(struct gc_rt_context* ctx, uint32_t sub, uint32_t length, uint32_t offset, uint32_t cb) {
    struct gc_rt_stream* st = (struct gc_rt_stream*)(uintptr_t)ctx->stream;
    gc_command_seen(ctx, 1);
    if (st != 0) {
        if (sub & 0x00010000u) {
            ctx->stream_generation++;
            st->playing = 0;  /* cancel at once */
        } else if (length != 0) {
            if (!st->playing) {
                ctx->stream_generation++;
                st->start = offset;
                st->length = length;
                st->pos = offset;
                gc_stream_reset(st);
                st->playing = 1;
                /* A good start in the ring before the AI asks for any. */
                gc_stream_fetch(ctx);
            }
            st->next_start = offset;  /* the drive loops a stream until told otherwise */
            st->next_length = length;
        } else {
            st->next_length = 0;  /* stop at its end */
        }
    }
    gc_set_imm(ctx, 0);
    gc_complete(ctx, cb, GC_DI_TC);
    return 1;
}

/* E2: the stream's state, address, start or length, in the immediate
 * register (addresses in 4-byte words, as the drive gives them). */
uint32_t gc_on_audio_status(struct gc_rt_context* ctx, uint32_t sub, uint32_t cb) {
    const struct gc_rt_stream* st = (const struct gc_rt_stream*)(uintptr_t)ctx->stream;
    uint32_t value = 0;
    gc_command_seen(ctx, 1);
    if (st != 0) {
        const uint32_t which = (sub >> 16) & 0xFFu;
        const uint32_t played = st->pos - (st->fetched - st->consumed);
        if (which == 0) value = st->playing ? 1u : 0u;
        else if (which == 1) value = played >> 2;
        else if (which == 2) value = st->start >> 2;
        else if (which == 3) value = st->length;
    }
    gc_set_imm(ctx, value);
    gc_complete(ctx, cb, GC_DI_TC);
    return 1;
}

static int32_t gc_clamp16(int32_t v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : v; }

/* One 4-bit sample of the drive's ADPCM: the filter in the header's top
 * nibble, the shift in its bottom one (the format as documented in yagcd
 * and implemented in Dolphin's StreamADPCM). */
static int16_t gc_adp_sample(int32_t nibble, uint32_t header, int32_t* hist1, int32_t* hist2) {
    int32_t predict = 0;
    int32_t cur;
    switch (header >> 4) {
    case 1: predict = *hist1 * 0x3C; break;
    case 2: predict = *hist1 * 0x73 - *hist2 * 0x34; break;
    case 3: predict = *hist1 * 0x62 - *hist2 * 0x37; break;
    default: break;
    }
    predict = (predict + 0x20) >> 6;
    if (predict < -0x200000) predict = -0x200000;
    if (predict > 0x1FFFFF) predict = 0x1FFFFF;
    cur = (((int32_t)(int16_t)(nibble << 12) >> (header & 0xF)) << 6) + predict;
    *hist2 = *hist1;
    *hist1 = cur;
    return (int16_t)gc_clamp16(cur >> 6);
}

/* The next ADPCM block from the ring into left/right; 0 when the ring
 * is empty (the stream has ended, or the card side has not caught up).
 * Runs in the AI interrupt: no card access here. */
static int gc_stream_block(struct gc_rt_stream* st) {
    const uint8_t* b;
    uint32_t i;
    if (st->fetched - st->consumed < GC_ADP_BLOCK) {
        if (st->ended) st->playing = 0;
        return 0;
    }
    b = (const uint8_t*)(uintptr_t)(st->buf + st->consumed % st->buf_bytes);
    for (i = 0; i < 28; ++i) {
        st->left[i] = gc_adp_sample(b[4 + i] & 0xF, b[0], &st->hist[0], &st->hist[1]);
        st->right[i] = gc_adp_sample(b[4 + i] >> 4, b[1], &st->hist[2], &st->hist[3]);
    }
    st->consumed += GC_ADP_BLOCK;  /* the card side may reuse the block from now on */
    st->sample = 0;
    return 1;
}

/* The next 48 kHz stereo sample; 0 when there is none. */
static int gc_stream_next(struct gc_rt_stream* st, int32_t* l, int32_t* r) {
    if (st->sample >= 28 && !gc_stream_block(st)) return 0;
    *l = st->left[st->sample];
    *r = st->right[st->sample];
    st->sample++;
    return 1;
}

/* AIInitDMA: the buffer the AI plays next is the game's finished mix;
 * the stream goes into it at the AI's stream volume. */
void gc_on_ai_dma(struct gc_rt_context* ctx, uint32_t start, uint32_t length) {
    struct gc_rt_stream* st = (struct gc_rt_stream*)(uintptr_t)ctx->stream;
    const uint32_t cr = gc_read32(GC_AI_CR);
    const uint32_t vr = gc_read32(GC_AI_VR);
    gc_guard(ctx);
    const int32_t vol_l = (int32_t)(vr & 0xFFu);
    const int32_t vol_r = (int32_t)((vr >> 8) & 0xFFu);
    const uint32_t addr = 0x80000000u | (start & 0x01FFFFE0u);
    int16_t* out = (int16_t*)(uintptr_t)addr;
    uint32_t frames = length / 4;
    uint32_t i;
    if ((++ctx->ai_dmas & 1023) == 0) gc_trace(ctx, GC_TRACE_AI);
    if (st == 0 || !st->playing || (cr & GC_AI_PSTAT) == 0 || length == 0) return;
    gc_invalidate(addr, length);
    for (i = 0; i < frames; ++i) {
        int32_t l;
        int32_t r;
        if (!gc_stream_next(st, &l, &r)) {
            if (st->playing) st->starved++;
            break;
        }
        if (cr & GC_AI_32K) {
            /* 48 to 32 kHz: of every three samples, the first, then the
             * other two averaged. */
            if (st->phase == 0) {
                st->phase = 1;
            } else {
                int32_t l2;
                int32_t r2;
                if (!gc_stream_next(st, &l2, &r2)) {
                    if (st->playing) st->starved++;
                    break;
                }
                l = (l + l2) >> 1;
                r = (r + r2) >> 1;
                st->phase = 0;
            }
        }
        if (ctx->flags & GC_FLAG_STREAM_ONLY) out[2 * i] = out[2 * i + 1] = 0;
        out[2 * i] = (int16_t)gc_clamp16(out[2 * i] + ((l * vol_l) >> 8));
        out[2 * i + 1] = (int16_t)gc_clamp16(out[2 * i + 1] + ((r * vol_r) >> 8));
    }
    gc_flush(addr, length);
    st->mixes++;
}

/* Back to RiftWii: the title the loader picked (the Homebrew Channel, the
 * RiftWii channel or the Wii Menu, gcnative.cpp's way_back_title),
 * launched through /dev/es, which IOS still runs in Wii mode. The picture
 * goes black first, then the VI is handed over on, in RiftWii's own
 * interlaced mode from the jump, blanked (gc_vi_hand_over). Neither the
 * display turned off (DCR bit 0: the Homebrew Channel, 00010001-JODI, ran
 * with the TV black on a tester's Wii) nor the game's own mode left on
 * (black again with 480p on, 18 Oct) gave the next title a picture.
 * Returns only when IOS refused, with the game's VI registers back. */
#define GC_ES_LAUNCHTITLE 8u
#define GC_YUYV_BLACK 0x10801080u
static int gc_crash_frame(const struct gc_rt_context* ctx, const uint8_t* card, int black);
static int32_t gc_leave_launch(struct gc_rt_context* ctx) {
    const uint32_t buf = ctx->leave_buf;
    uint32_t* const request = (uint32_t*)(uintptr_t)(buf + GC_LEAVE_REQUEST_OFFSET);
    uint32_t* const vectors = (uint32_t*)(uintptr_t)(buf + GC_LEAVE_VECTOR_OFFSET);
    const uint32_t physical = gc_physical((uint32_t)(uintptr_t)request);
    uint32_t reply;
    uint32_t i;
    int replied = 0;
    for (i = 0; i < 16; ++i) request[i] = 0;
    request[0] = GC_IOS_IOCTLV;
    request[2] = ctx->leave_fd;
    request[3] = GC_ES_LAUNCHTITLE;
    request[4] = 2;
    request[6] = gc_physical((uint32_t)(uintptr_t)vectors);
    vectors[0] = gc_physical(buf);
    vectors[1] = 8;
    vectors[2] = gc_physical(buf + 0x20);
    vectors[3] = 0xD8;
    gc_flush(buf, GC_LEAVE_BYTES);
    /* Abandon a stalled storage doorbell, but retain its DMA quarantine.
     * ES owns a different request, vectors and input data. A late storage
     * reply can touch only the old buffers until ES reloads IOS. */
    gc_write32(GC_IPC_PPCCTRL, GC_IPC_Y2);
    ctx->ipc_sending = 1;
    gc_write32(GC_IPC_PPCMSG, physical);
    gc_write32(GC_IPC_PPCCTRL, GC_IPC_X1);
    /* Once ES takes the launch, IOS can reload and reset the PPC without
     * a reply. Only an explicit refusal may return to the game. */
    while (ctx->ipc_sending || !replied) {
        reply = gc_ipc_take();
        if (reply == physical) replied = 1;
        else if (reply != 0) gc_ipc_route(ctx, reply);
        if (gc_read32(GC_IPC_PPCCTRL) & GC_IPC_Y2) {
            gc_write32(GC_IPC_PPCCTRL, GC_IPC_Y2);
            ctx->ipc_sending = 0;
        }
    }
    gc_invalidate((uint32_t)(uintptr_t)request, 64);
    return (int32_t)request[1];
}

static void gc_leave_finish(struct gc_rt_context* ctx, int storage_ready) {
    const uint16_t game_ai_dma = gc_read16(GC_AI_DMA_CONTROL);
    const uint32_t game_ai_cr = gc_read32(GC_AI_CR) & ~(GC_AI_INT | GC_AI_SCRESET);
    uint32_t game_vi[GC_VI_HANDOVER_WORDS];
    int32_t result;
    uint32_t msr;
    /* A running DMA loops the game's last buffer while saves and the IOS
     * reload hold the game here: a loud buzz until the menu starts. Keep
     * the stream quiet too, without clearing its interrupt or counter. */
    gc_write16(GC_AI_DMA_CONTROL, game_ai_dma & ~GC_AI_DMA_ON);
    if (game_ai_cr & GC_AI_PSTAT) {
        gc_write32(GC_AI_CR, game_ai_cr & ~GC_AI_PSTAT);
    }
    /* The memory card first: what the game saved is in MEM2 until a
     * heartbeat writes it back. A sector that will not write keeps the
     * game here, to try again, unless IOS has stopped answering entirely.
     * Then HOME still leaves; retain the dirty bits and timeout in RAM. */
    if (ctx->card) {
        struct gc_card* card = (struct gc_card*)(uintptr_t)ctx->card;
        if (storage_ready) {
            gc_card_flush(ctx, card, 1);
            if (card->dirty_count != 0) {
                storage_ready = gc_io_drain(ctx, 1);
                if (storage_ready) goto restore_audio;
            }
        }
        if (!storage_ready && card->dirty_count != 0) card->flush_error = -1000;
    }
    if (storage_ready) {
        gc_trace(ctx, GC_TRACE_LEAVE);
        /* The exit's own trace can time out too. It must not leave the
         * worker's continuation running during the title launch. */
        storage_ready = gc_io_drain(ctx, 1);
    }
    /* A Wii Remote's motor would otherwise run on until its link drops. */
    if (ctx->bt) gc_bt_stop_rumble(ctx, (struct gc_bt*)(uintptr_t)ctx->bt);
    msr = gc_irq_off();  /* the game stops here: IOS takes the console over */
    gc_crash_frame(ctx, 0, 1);
    gc_vi_hand_over(ctx, game_vi);
    result = gc_leave_launch(ctx);
    /* Still here: IOS said no (the title went, or its ticket). The game
     * comes back (its next frame repaints the screen, as the crash screen
     * redraws itself), and the trace says why. */
    ctx->last_error = result;
    gc_vi_take_back(ctx, game_vi);
    gc_irq_restore(msr);
    if (storage_ready) gc_trace(ctx, GC_TRACE_LEAVE);
restore_audio:
    /* A failed save or refused launch leaves the game running. */
    if (game_ai_cr & GC_AI_PSTAT) {
        gc_write32(GC_AI_CR, game_ai_cr);
    }
    gc_write16(GC_AI_DMA_CONTROL, game_ai_dma);
}

static void gc_leave(struct gc_rt_context* ctx) {
    gc_leave_finish(ctx, gc_io_drain(ctx, 1));
}

/* HOME on a Wii Remote with an extension or on a Classic Controller (HOME
 * and - on a Wii Remote alone, whose HOME is the game's Y), or L, R, Z and
 * Start together on a GameCube controller (the pads as the game reads
 * them), held for leave_hold ticks; checked at every pad read. Not while
 * storage is preparing buffers, nor within half a second of the game's last
 * memory card access (a save may be under way): the next read tries
 * again. */
#define GC_CARD_QUIET_TICKS 30375000u  /* half a second of the timebase */
#define GC_LEAVE_PAD 0x1070u  /* Start, L, R, Z */
/* Since when the memory card has had no access (leave_card_quiet), noted
 * while something waits for it to be quiet. */
static void gc_card_note(struct gc_rt_context* ctx, uint32_t now) {
    const struct gc_card* card = (const struct gc_card*)(uintptr_t)ctx->card;
    if (card == 0) return;
    if (card->activity != ctx->leave_card_activity || ctx->leave_card_quiet == 0) {
        ctx->leave_card_activity = card->activity;
        ctx->leave_card_quiet = now | 1u;
    }
}
static int gc_card_quiet(const struct gc_rt_context* ctx, uint32_t now) {
    return ctx->card == 0 || now - ctx->leave_card_quiet >= GC_CARD_QUIET_TICKS;
}
static void gc_leave_check(struct gc_rt_context* ctx, const uint8_t* status) {
    uint32_t port, now;
    int held = 0;
    if (ctx->leave_buf == 0 || ctx->leave_fd == 0xFFFFFFFFu) return;
    if (ctx->bt && gc_bt_home_held((const struct gc_bt*)(uintptr_t)ctx->bt)) held = 1;
    for (port = 0; port < 4 && !held; ++port) {
        const uint8_t* pad = status + 12 * port;
        const uint32_t buttons = ((uint32_t)pad[0] << 8) | pad[1];
        if (pad[10] == 0 && (buttons & GC_LEAVE_PAD) == GC_LEAVE_PAD) held = 1;
    }
    if (!held) {
        ctx->leave_since = 0;
        return;
    }
    __asm__ volatile("mftb %0" : "=r"(now));
    gc_card_note(ctx, now);
    if (ctx->leave_since == 0) {
        ctx->leave_since = now | 1u;
        return;
    }
    if (now - ctx->leave_since < ctx->leave_hold || (ctx->storage_busy && ctx->io_pending == 0)) return;
    if (!gc_card_quiet(ctx, now)) return;
    gc_leave(ctx);
    ctx->leave_since = 0;
}

/* ---- Reset: the game restarted -------------------------------------- */

/* On a GameCube the game watches Reset itself (the SDK's
 * OSGetResetButtonState reads PI's bit below) and, once it has faded out
 * and finished any save, calls OSResetSystem. The OS runs its reset
 * functions (the drive, the memory cards, the controllers), stops the DSP
 * and the audio, turns the picture off and has the hardware reset the
 * console through PI's reset register; the start-up code then boots the
 * disc again. In Wii mode nothing would boot it: the hardware reset, or
 * the OS's own reboot through the apploader (reset 0, which loads the
 * game without RiftWii's changes), would leave the console stopped. So
 * OSResetSystem always takes the hot reset here, and the reset routine
 * puts MEM1 back as the loader left it at the jump and starts the game
 * at its entry again (gc_on_hot_reset).
 *
 * The Wii's Reset button reaches the same bit in Wii mode (libogc's
 * SYS_ResetButtonDown reads it there too). Whether a game sees it on a
 * Wii is not known, so the blob watches it as well, at each pad read:
 * a press that the game has not answered with its own OSResetSystem two
 * seconds after it ended (a fade-out takes less) is answered by calling
 * OSResetSystem for it, from the game's own thread, once the memory card
 * is quiet.
 *
 * Only for games whose restart is known to work (the loader's list,
 * riftwii/gxpatches.hpp): for the others Reset, and the game's own
 * OSResetSystem, go back to RiftWii as holding HOME does. */
#define GC_PI_INTSR 0xCC003000u
#define GC_PI_RESET_UP 0x00010000u          /* bit 16: the Reset button is up (0 while it is pressed) */
#define GC_RESET_DEBOUNCE_TICKS 303750u     /* 5 ms of the timebase */
#define GC_RESET_GRACE_TICKS 121500000u     /* 2 s */
#define GC_RESET_TEST_FIRST 1500u           /* GC_FLAG_RESET_TEST's presses: pad reads */
#define GC_RESET_TEST_SECOND 3600u
#define GC_MSR_EE 0x8000u
typedef void (*gc_reset_system_fn)(uint32_t reset, uint32_t code, uint32_t menu);

/* The button, at every pad read (`n` is the read's number). */
static void gc_reset_check(struct gc_rt_context* ctx, uint32_t n) {
    uint32_t now, msr, kind;
    int pressed;
    const int restart = ctx->restart_count != 0 && ctx->reset_system != 0;
    const int way_back = ctx->leave_buf != 0 && ctx->leave_fd != 0xFFFFFFFFu;
    if (!restart && !way_back) return;
    __asm__ volatile("mftb %0" : "=r"(now));
    pressed = (gc_read32(GC_PI_INTSR) & GC_PI_RESET_UP) == 0;
    if ((ctx->flags & GC_FLAG_RESET_TEST) && (n == GC_RESET_TEST_FIRST || n == GC_RESET_TEST_SECOND)) {
        /* A press that has just ended, as the button gives one. */
        pressed = 0;
        ctx->reset_down_since = (now - 2u * GC_RESET_DEBOUNCE_TICKS) | 1u;
    }
    if (pressed) {
        if (ctx->reset_down_since == 0) ctx->reset_down_since = now | 1u;
    } else if (ctx->reset_down_since != 0) {
        const uint32_t held = now - ctx->reset_down_since;
        ctx->reset_down_since = 0;
        if (held >= GC_RESET_DEBOUNCE_TICKS) {
            ctx->reset_presses++;
            ctx->reset_request_at = now | 1u;
            ctx->reset_asked_before = ctx->resets_asked;
            /* Waited for from the next read on: `now | 1` can be a tick
             * past `now`, and the difference would wrap. */
            return;
        }
    }
    if (ctx->reset_request_at == 0) return;
    if (ctx->resets_asked != ctx->reset_asked_before) {
        ctx->reset_request_at = 0;  /* the game answered it itself */
        return;
    }
    gc_card_note(ctx, now);
    if (now - ctx->reset_request_at < GC_RESET_GRACE_TICKS) return;
    /* From the game's thread only: a pad read in an interrupt handler (some
     * games read the pads at the retrace) waits for the next one. */
    __asm__ volatile("mfmsr %0" : "=r"(msr));
    if ((msr & GC_MSR_EE) == 0 || (ctx->storage_busy && ctx->io_pending == 0) || !gc_card_quiet(ctx, now)) return;
    ctx->reset_request_at = 0;
    if (!restart) {
        gc_leave(ctx);  /* a game whose restart is not known to work: back to RiftWii, as holding HOME */
        return;
    }
    /* The test's first press takes the OS's own restart (reset 0), turned
     * into the hot reset as a game's would be; the button the hot reset. */
    kind = (ctx->flags & GC_FLAG_RESET_TEST) && ctx->restarts == 0 ? GC_OS_RESET_RESTART : GC_OS_RESET_HOTRESET;
    ((gc_reset_system_fn)(uintptr_t)ctx->reset_system)(kind, 0, 0);
}

/* OSResetSystem(reset, code, menu), at its start: args[0..2]. */
void gc_on_reset_system(struct gc_rt_context* ctx, uint32_t* args) {
    ctx->resets_asked++;
    ctx->reset_last = args[0] << 8 | (args[2] & 0xFFu);
    if (args[0] == GC_OS_RESET_SHUTDOWN) return;  /* the OS shuts down and the game goes on: as made */
    if (!gc_io_drain(ctx, 0)) {
        /* MEM1 cannot be restored under a late DMA. Use the private way
         * back instead; on refusal the SDK's reset continues as before. */
        if (ctx->leave_buf != 0 && ctx->leave_fd != 0xFFFFFFFFu) gc_leave_finish(ctx, 0);
        return;
    }
    /* A restart in a game that switches programs (Sonic Gems Collection):
     * the SDK boots the program the game named (OSSetBootDol) through
     * __OSBootDolSimple, which gc_on_boot_dol takes, or else resets the
     * hardware, which gc_on_hot_reset takes back to RiftWii. */
    if (args[0] == GC_OS_RESET_RESTART && args[2] == 0 && ctx->exec_sector != 0 && ctx->restart_count == 0) return;
    if (ctx->restart_count == 0) {
        /* No restart for this game: back to RiftWii, rather than the
         * hardware reset that would stop the console. Returns only when
         * IOS says no; the game's reset then goes on as made. */
        if (ctx->leave_buf != 0 && ctx->leave_fd != 0xFFFFFFFFu) gc_leave(ctx);
        return;
    }
    /* To the start-up menu: RiftWii's own way back. It returns when IOS
     * says no; the game restarts then. */
    if (args[2] != 0) {
        gc_leave(ctx);
        args[2] = 0;
    }
    args[0] = GC_OS_RESET_HOTRESET;
}

/* A switch to another program on the disc: the SDK's OSExec (Sonic Gems
 * Collection starts Sonic CD, Sonic R and Sonic the Fighters, each a
 * program of its own, this way) and its restart into a named program both
 * end in __OSBootDolSimple. That loads the disc's apploader and runs a
 * small program after it, which reads the disc with code of its own: none
 * of it is RiftWii's, and the next program would run unpatched. So the
 * switch goes back through RiftWii, which starts the same image again
 * with that program: sd:/riftwii/gc/exec.txt (one sector, loader-made)
 * holds "--idle--" and the image's path and ID, one per line; its first
 * line becomes "--launch" and the program, the restart code and the
 * arguments are added, a headless launch (docs/HEADLESS.md) that
 * RiftWii's next start carries out. Then the game leaves as holding HOME
 * does. args: doloffset, restartCode, regionStart, regionEnd,
 * argsUseDefault, argc, argv. A doloffset of 0xFFFFFFFF (from OSExec and
 * the restart, through __OSBootDol) is "the program after the
 * apploader", which takes the real one from argv[0] in decimal. */
/* The text, from words: the blob has no constant data (Makefile.wii
 * checks), so each key is its characters in immediates, big-endian. */
#define GC_EXEC_IDLE_0 0x2D2D6964u   /* "--id" */
#define GC_EXEC_IDLE_1 0x6C652D2Du   /* "le--" */
#define GC_EXEC_LAUNCH_0 0x2D2D6C61u /* "--la" */
#define GC_EXEC_LAUNCH_1 0x756E6368u /* "unch" */
#define GC_EXEC_ARG_BYTES 160u  /* of each argument kept */
#define GC_EXEC_ARGS 32u  /* Sonic Gems Collection passes 12; the sector holds the rest */
static uint32_t gc_exec_bytes(uint8_t* buf, uint32_t at, uint32_t word, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n && at < GC_SECTOR - 2u; ++i) buf[at++] = (uint8_t)(word >> (24u - 8u * i));
    return at;
}
static uint32_t gc_exec_word(const uint8_t* buf, uint32_t at) {
    return (uint32_t)buf[at] << 24 | (uint32_t)buf[at + 1] << 16 | (uint32_t)buf[at + 2] << 8 | buf[at + 3];
}
static uint32_t gc_exec_text(uint8_t* buf, uint32_t at, const char* text) {
    uint32_t i;
    for (i = 0; text[i] != 0 && i < GC_EXEC_ARG_BYTES && at < GC_SECTOR - 2u; ++i) {
        const char c = text[i];
        buf[at++] = (uint8_t)(c >= ' ' && c <= '~' ? c : '?');  /* one line each, printable */
    }
    return at;
}
static uint32_t gc_exec_number(uint8_t* buf, uint32_t at, uint32_t value) {
    uint8_t digits[11];
    uint32_t n = 0;
    do {
        digits[n++] = (uint8_t)('0' + value % 10u);
        value /= 10u;
    } while (value != 0);
    while (n > 0 && at < GC_SECTOR - 2u) buf[at++] = digits[--n];
    return at;
}
static uint32_t gc_exec_end_line(uint8_t* buf, uint32_t at) {
    if (at < GC_SECTOR - 1u) buf[at++] = '\n';
    return at;
}
void gc_on_boot_dol(struct gc_rt_context* ctx, uint32_t* args) {
    uint8_t* const buf = (uint8_t*)(uintptr_t)ctx->trace_buf;
    const char* const* argv = (const char* const*)(uintptr_t)args[6];
    const uint32_t argc = args[4] == 0 && argv != 0 ? args[5] : 0;
    uint32_t target = args[0], first = 0, at, i;
    uint32_t msr;
    int read;
    /* Even an unredirected SDK boot can discard the old OS and its alarms. */
    if (!gc_io_drain(ctx, 0)) return;  /* leave exec.txt and the quarantined buffers alone */
    if (ctx->exec_sector == 0 || ctx->leave_buf == 0 || ctx->leave_fd == 0xFFFFFFFFu) return;
    if (target == 0xFFFFFFFFu) {
        const char* p = argc != 0 ? argv[0] : 0;
        if (p == 0 || *p < '0' || *p > '9') return;  /* not a program RiftWii can name */
        for (target = 0; *p >= '0' && *p <= '9'; ++p) target = target * 10u + (uint32_t)(*p - '0');
        first = 1;
    }
    msr = gc_irq_off();  /* the trace's buffer is this one: no heartbeat in between */
    ctx->storage_busy = 1;
    read = gc_sd_read(ctx, ctx->exec_sector, 1, (uint32_t)(uintptr_t)buf);
    if (!read || gc_exec_word(buf, 0) != GC_EXEC_IDLE_0 || gc_exec_word(buf, 4) != GC_EXEC_IDLE_1) {
        ctx->storage_busy = 0;
        gc_irq_restore(msr);
        return;
    }
    (void)gc_exec_bytes(buf, 0, GC_EXEC_LAUNCH_0, 4);
    (void)gc_exec_bytes(buf, 4, GC_EXEC_LAUNCH_1, 4);
    for (at = 0; at < GC_SECTOR && buf[at] != 0; ++at) {
    }
    at = gc_exec_bytes(buf, at, 0x67635F64u, 4);  /* "gc_dol=" */
    at = gc_exec_bytes(buf, at, 0x6F6C3D00u, 3);
    at = gc_exec_end_line(buf, gc_exec_number(buf, at, target));
    at = gc_exec_bytes(buf, at, 0x67635F72u, 4);  /* "gc_restart=" */
    at = gc_exec_bytes(buf, at, 0x65737461u, 4);
    at = gc_exec_bytes(buf, at, 0x72743D00u, 3);
    at = gc_exec_end_line(buf, gc_exec_number(buf, at, args[1]));
    for (i = first; i < argc && i - first < GC_EXEC_ARGS; ++i) {
        if (argv[i] == 0) continue;
        at = gc_exec_bytes(buf, at, 0x67635F61u, 4);  /* "gc_arg=" */
        at = gc_exec_bytes(buf, at, 0x72673D00u, 3);
        at = gc_exec_end_line(buf, gc_exec_text(buf, at, argv[i]));
    }
    (void)gc_sd_write_locked(ctx, ctx->exec_sector, 1, (uint32_t)(uintptr_t)buf);
    ctx->storage_busy = 0;
    gc_trace(ctx, GC_TRACE_EXEC);
    gc_irq_restore(msr);
    gc_leave(ctx);
    /* Still here: IOS said no. The file back to idle, so no later start
     * of RiftWii takes the switch up, and the game's own goes on. */
    if (!gc_io_drain(ctx, 0)) return;  /* no idle-file repair while IOS owns its buffers */
    msr = gc_irq_off();
    ctx->storage_busy = 1;
    if (gc_sd_read(ctx, ctx->exec_sector, 1, (uint32_t)(uintptr_t)buf)) {
        (void)gc_exec_bytes(buf, 0, GC_EXEC_IDLE_0, 4);
        (void)gc_exec_bytes(buf, 4, GC_EXEC_IDLE_1, 4);
        (void)gc_sd_write_locked(ctx, ctx->exec_sector, 1, (uint32_t)(uintptr_t)buf);
    }
    ctx->storage_busy = 0;
    gc_irq_restore(msr);
}

/* A bonus disc's menu (the Zelda Ocarina of Time / Master Quest disc,
 * the Wind Waker demo disc) does not switch programs through
 * __OSBootDolSimple: each game it offers is a whole disc packed in a TGC
 * image, and the menu reads the chosen one's program with a loader of its
 * own and jumps to it. That program would run with none of RiftWii's
 * patches, its disc reads unanswered, and the screen stayed black. So the
 * menu's read of a TGC's header, or its first read of a TGC's program
 * (the loader found where each lies, tgc_offset and tgc_dol_start/end;
 * gc_tgc.h says why both) is taken as the start of that game: exec.txt
 * (gc_on_boot_dol's file) becomes a launch with
 * gc_tgc=<the TGC's offset on the disc>, and the game leaves as holding
 * HOME does, before the read is served. RiftWii's next start launches the
 * image again with that TGC's game. Returns when that could not be done;
 * the watch then ends, the read is served, and the menu's own start runs
 * as it would have. */
static void gc_on_tgc_read(struct gc_rt_context* ctx, uint32_t offset, uint32_t length) {
    uint8_t* const buf = (uint8_t*)(uintptr_t)ctx->trace_buf;
    uint32_t tgc = 0, at, i, msr;
    int found = 0, read;
    for (i = 0; i < ctx->tgc_count && i < GC_TGC_MAX && !found; ++i) {
        /* The TGC's header or its program (gc_tgc.h). */
        found = gc_tgc_read_starts_game(ctx->tgc_offset[i], ctx->tgc_dol_start[i], ctx->tgc_dol_end[i], offset, length);
        if (found) tgc = ctx->tgc_offset[i];
    }
    if (!found) return;
    /* An SD request under way (this read broke into it): the read's first
     * piece (gc_read_step) asks again once the card is free. */
    if (ctx->storage_busy) return;
    if (ctx->tgc_sector == 0 || ctx->leave_buf == 0 || ctx->leave_fd == 0xFFFFFFFFu) {
        ctx->tgc_count = 0;
        return;
    }
    msr = gc_irq_off();  /* the trace's buffer is this one: no heartbeat in between */
    ctx->storage_busy = 1;
    read = gc_sd_read(ctx, ctx->tgc_sector, 1, (uint32_t)(uintptr_t)buf);
    if (!read || gc_exec_word(buf, 0) != GC_EXEC_IDLE_0 || gc_exec_word(buf, 4) != GC_EXEC_IDLE_1) {
        ctx->storage_busy = 0;
        ctx->tgc_count = 0;
        gc_irq_restore(msr);
        return;
    }
    (void)gc_exec_bytes(buf, 0, GC_EXEC_LAUNCH_0, 4);
    (void)gc_exec_bytes(buf, 4, GC_EXEC_LAUNCH_1, 4);
    for (at = 0; at < GC_SECTOR && buf[at] != 0; ++at) {
    }
    at = gc_exec_bytes(buf, at, 0x67635F74u, 4);  /* "gc_tgc=" */
    at = gc_exec_bytes(buf, at, 0x67633D00u, 3);
    (void)gc_exec_end_line(buf, gc_exec_number(buf, at, tgc));
    if (gc_sd_write_locked(ctx, ctx->tgc_sector, 1, (uint32_t)(uintptr_t)buf) < 0) {
        ctx->tgc_count = 0;
        ctx->storage_busy = 0;
        gc_irq_restore(msr);
        return;
    }
    ctx->storage_busy = 0;
    gc_trace(ctx, GC_TRACE_EXEC);
    gc_irq_restore(msr);
    gc_leave(ctx);
    /* Still here: IOS said no. The file back to idle, so no later start
     * of RiftWii takes the start up, and the menu's own goes on. */
    ctx->tgc_count = 0;
    if (!gc_io_drain(ctx, 0)) return;
    msr = gc_irq_off();
    ctx->storage_busy = 1;
    if (gc_sd_read(ctx, ctx->tgc_sector, 1, (uint32_t)(uintptr_t)buf)) {
        (void)gc_exec_bytes(buf, 0, GC_EXEC_IDLE_0, 4);
        (void)gc_exec_bytes(buf, 4, GC_EXEC_IDLE_1, 4);
        (void)gc_sd_write_locked(ctx, ctx->tgc_sector, 1, (uint32_t)(uintptr_t)buf);
    }
    ctx->storage_busy = 0;
    gc_irq_restore(msr);
}

#define GC_CP_SR 0xCC000000u
#define GC_CP_SR_IDLE 0x000Cu       /* the GP idle for reading and for commands */
#define GC_GP_IDLE_TICKS 6075000u   /* 100 ms */
#define GC_CP_CR 0xCC000002u        /* the GP's FIFO reads, link, breakpoint and their interrupts */
#define GC_CP_CLEAR 0xCC000004u     /* written 3: the FIFO's overflow and underflow cleared */
#define GC_PI_FIFO_RESET 0xCC003018u
#define GC_GP_ABORT_TICKS 607u      /* 10 us of the timebase */

static void gc_spin_ticks(uint32_t ticks) {
    uint32_t start, now;
    __asm__ volatile("mftb %0" : "=r"(start));
    do {
        __asm__ volatile("mftb %0" : "=r"(now));
    } while (now - start < ticks);
}

/* The graphics chip as the first start found it (gc_on_hot_reset). A
 * GameCube's reset resets the GP with the rest of the console; here it
 * keeps running, still set up for the old game's FIFO. Before the first
 * start RiftWii's menu stops GX with GX_AbortFrame, which resets the
 * GP's command processor through PI's FIFO reset register, and nothing
 * did so before a restart. Super Mario Sunshine parks the GP at a FIFO
 * breakpoint several times a frame (its draw sync manager) and, restarted
 * on a Wii, slept for good in its first frame's GXDrawDone: no VIConfigure
 * and no picture followed, while its loading thread read the whole
 * nintendo.szs (21 Oct, 2610-564 and 2610-611), where Doshin the Giant
 * restarted fine on the same build. So the GP's FIFO reads, link,
 * breakpoint and interrupts go off, the command processor is reset and
 * the FIFO's overflow and underflow are cleared; the game's GXInit then
 * sets up its own FIFO on a GP as fresh as at the first start. */
static void gc_gp_abort(void) {
    *(volatile uint16_t*)(uintptr_t)GC_CP_CR = 0;
    gc_write32(GC_PI_FIFO_RESET, 1);
    gc_spin_ticks(GC_GP_ABORT_TICKS);
    gc_write32(GC_PI_FIFO_RESET, 0);
    gc_spin_ticks(GC_GP_ABORT_TICKS);
    *(volatile uint16_t*)(uintptr_t)GC_CP_CLEAR = 3;
}

static void gc_icache_invalidate(uint32_t address, uint32_t length) {
    uint32_t a = address & ~31u;
    const uint32_t end = address + length;
    for (; a < end; a += 32) __asm__ volatile("icbi 0,%0" : : "r"(a) : "memory");
    __asm__ volatile("sync\n\tisync" : : : "memory");
}

static void gc_copy_region(uint32_t to, uint32_t from, uint32_t length) {
    if (((to | from | length) & 3u) == 0) {
        uint32_t* dst = (uint32_t*)(uintptr_t)to;
        const uint32_t* src = (const uint32_t*)(uintptr_t)from;
        uint32_t words = length / 4;
        while (words--) *dst++ = *src++;
        return;
    }
    gc_copy((uint8_t*)(uintptr_t)to, (const uint8_t*)(uintptr_t)from, length);
}

/* What the blob keeps about the game that the restart ends: the drive's
 * command under way, the alarms in the old OS's queue, the music stream,
 * the card's transfer, Luigi's Mansion's loads. What the player has (the
 * Wii Remotes, the card's contents) stays. */
static void gc_forget_game(struct gc_rt_context* ctx, int restart) {
    uint32_t i;
    ctx->commands = 0;  /* the first command after the restart starts the heartbeat again */
    ctx->pending_cb = 0;
    ctx->pending_type = 0;
    ctx->read_state = GC_READ_NONE;
    ctx->read_generation++;
    ctx->stream_generation++;
    ctx->io_sp = 0;
    ctx->io_job = 0;
    ctx->io_running = 0;
    ctx->io_buffers_idle = 0;
    ctx->io_stream_restart = 0;
    ctx->io_maintenance = 0;
    ctx->io_queued = 0;
    ctx->io_trace_where = 0;
    gc_zero(ctx->io_alarm, sizeof(ctx->io_alarm));
    ctx->alarm_queued = 0;
    ctx->last_read_end = 0;
    ctx->load_saved_heap = 0;
    ctx->load_next = 0;
    for (i = 0; i < GC_LOAD_SLOTS; ++i) {
        ctx->load_request[i] = 0;
        ctx->load_heap[i] = 0;
    }
    ctx->leave_since = 0;
    ctx->reset_down_since = 0;
    ctx->reset_request_at = 0;
    ctx->native_ports = 0;
    ctx->pad_enabled_before = GC_PORTS_ENABLED_UNKNOWN;
    ctx->pad_raw_errs = 0xFFFFFFFFu;
    ctx->pad_filled_errs = 0xFFFFFFFFu;
    ctx->pad_played = 0;
    /* A two-disc game restarts from disc 1, as MEM1 (its FST, its ID) is
     * put back as it was at the jump. A program the game starts itself
     * goes on with the disc that is in. */
    if (restart && ctx->disc_change) {
        struct gc_disc_change* dc = (struct gc_disc_change*)(uintptr_t)ctx->disc_change;
        gc_disc_restart(dc);
        gc_disc_insert(ctx, dc);
    }
    if (ctx->stream) {
        struct gc_rt_stream* st = (struct gc_rt_stream*)(uintptr_t)ctx->stream;
        st->playing = 0;
        st->next_length = 0;
        gc_stream_reset(st);
    }
    if (ctx->card) {
        struct gc_card* card = (struct gc_card*)(uintptr_t)ctx->card;
        card->selected = 0;
        card->locked = 0;
        card->irq_on = 0;
        card->count = 0;
        card->tc_callback = 0;
        card->exi_pending = 0;
        card->alarm_set = 0;
    }
    /* The restarted game has not asked for rumble yet: the first pad read
     * after the restart switches a rumbling Wii Remote's motor off, instead
     * of it running on until the game's own PADControlMotor. The remotes
     * themselves stay connected. */
    if (ctx->bt) {
        struct gc_bt* bt = (struct gc_bt*)(uintptr_t)ctx->bt;
        for (i = 0; i < sizeof(bt->rumble_want); ++i) bt->rumble_want[i] = 0;
    }
}

/* MEM1 back as it was at the jump, then the game's entry: on the stack
 * gc_on_hot_reset moved to, as the game's own stack is in what is put
 * back (its BSS, or its sections). Reached by a branch from there. */
static void gc_restore_and_start(struct gc_rt_context* ctx) __attribute__((noinline, used, noreturn));
static void gc_restore_and_start(struct gc_rt_context* ctx) {
    const struct gc_restart_region* regions = (const struct gc_restart_region*)(uintptr_t)ctx->restart_regions;
    uint32_t i;
    for (i = 0; i < ctx->restart_count; ++i) {
        const struct gc_restart_region* r = &regions[i];
        if (r->source == 0)
            gc_zero((uint8_t*)(uintptr_t)r->address, r->length);
        else
            gc_copy_region(r->address, r->source, r->length);
        gc_flush(r->address, r->length);
        gc_icache_invalidate(r->address, r->length);
    }
    gc_guard(ctx);  /* IOS's counter, as at every IOS request */
    __asm__ volatile("mtctr %0\n\tbctr" : : "r"(ctx->entry) : "memory");
    for (;;) {
    }
}

/* The audio hardware as a restart must leave it (gc_on_hot_reset). A
 * GameCube's Reset button resets the whole console and the game starts
 * from a DSP, audio interface and ARAM interface fresh from that reset;
 * the restart here keeps the console running, and on a Wii the restarted
 * game stopped where its audio starts (Super Mario Sunshine, GMSE01 and
 * GMSE04: no AI or ARAM transfer, no pad read after the restart), while
 * every disc command finished. The game's own OSResetSystem has already
 * stopped the DSP and the audio DMA. On top of that: the audio DMA and
 * the stream off with their interrupts cleared and the sample counter
 * back to 0, any ARAM transfer finished, the DSP held in reset to start
 * from its ROM with every interrupt cleared, the mailboxes emptied, and
 * the ARAM interface's size and refresh as they were at the first start.
 * The waits are bounded; one that runs out is noted (audio_reset, the
 * trace's GC_TR_AUDIO_RESET) and the restart goes on. */
#define GC_AUDIO_WAIT_TICKS 607500u   /* 10 ms of the timebase */
#define GC_AUDIO_DMA_SLOW 1u          /* an ARAM transfer was still going */
#define GC_AUDIO_INIT_SLOW 2u         /* the DSP was still taking boot code */
#define GC_AUDIO_RESET_SLOW 4u        /* the DSP's reset did not finish */
#define GC_RESTART_GP_SLOW 8u         /* the GP was still busy when it was reset (gc_gp_abort) */
/* Waits until `mask` of the DSP's control register reads 0; 1 when it
 * did in time. */
static int gc_dsp_wait_clear(uint32_t mask) {
    uint32_t start, now;
    __asm__ volatile("mftb %0" : "=r"(start));
    do {
        if ((gc_read16(GC_DSP_CSR) & mask) == 0) return 1;
        __asm__ volatile("mftb %0" : "=r"(now));
    } while (now - start < GC_AUDIO_WAIT_TICKS);
    return 0;
}
static void gc_audio_reset(struct gc_rt_context* ctx) {
    const uint32_t before = gc_read16(GC_DSP_CSR);
    const uint32_t ai_kept = ctx->ai_cr_at_jump & (GC_AI_AFR | GC_AI_32K);
    const uint32_t acks = GC_DSP_CSR_DSPINT | GC_DSP_CSR_ARINT | GC_DSP_CSR_AIINT;
    uint32_t slow = 0, start, now;
    /* The audio DMA and the stream. */
    gc_write16(GC_AI_DMA_CONTROL, gc_read16(GC_AI_DMA_CONTROL) & ~GC_AI_DMA_ON);
    gc_write32(GC_AI_CR, ai_kept | GC_AI_INT | GC_AI_SCRESET);
    gc_write32(GC_AI_IT, 0);
    /* The DSP: no ARAM transfer under way, then halted, to start from its
     * ROM, its interrupts cleared and masked. */
    if (!gc_dsp_wait_clear(GC_DSP_CSR_DMA_BUSY)) slow |= GC_AUDIO_DMA_SLOW;
    gc_write16(GC_DSP_CSR, GC_DSP_CSR_BOOT_ROM | GC_DSP_CSR_HALT | acks);
    if (!gc_dsp_wait_clear(GC_DSP_CSR_INIT)) slow |= GC_AUDIO_INIT_SLOW;
    /* The mailboxes: the CPU's to the DSP written empty, the DSP's to the
     * CPU taken. */
    gc_write16(GC_DSP_MAIL_TO_DSP, 0);
    if (gc_read16(GC_DSP_MAIL_FROM_DSP) & 0x8000u) (void)gc_read16(GC_DSP_MAIL_FROM_DSP_LOW);
    /* A moment for the DSP to see the halt, then its reset. */
    __asm__ volatile("mftb %0" : "=r"(start));
    do {
        __asm__ volatile("mftb %0" : "=r"(now));
    } while (now - start < 600u);  /* 10 us */
    gc_write16(GC_DSP_CSR, GC_DSP_CSR_BOOT_ROM | GC_DSP_CSR_HALT | GC_DSP_CSR_RESET | acks);
    if (!gc_dsp_wait_clear(GC_DSP_CSR_RESET)) slow |= GC_AUDIO_RESET_SLOW;
    /* The ARAM interface as at the first start. */
    if (ctx->ar_config_at_jump != 0) {
        gc_write16(GC_AR_SIZE, ctx->ar_config_at_jump >> 16);
        gc_write16(GC_AR_REFRESH, ctx->ar_config_at_jump & 0xFFFFu);
    }
    ctx->audio_reset = before << 16 | slow << 12 | (gc_read16(GC_DSP_CSR) & 0xFFFu);
}

static void gc_hot_reset_leave(struct gc_rt_context* ctx, int storage_ready) {
    if (ctx->leave_buf != 0 && ctx->leave_fd != 0xFFFFFFFFu) gc_leave_finish(ctx, storage_ready);
    for (;;) {
    }  /* IOS said no: stopped, as the routine itself would be */
}

/* The SDK's reset routine(code << 3), in place of PI's reset register:
 * the game's OSResetSystem has run its reset functions, stopped the DSP
 * and the audio and turned the picture off, with interrupts off. MEM1
 * goes back as it was at the jump and the game starts again. */
void gc_on_hot_reset(struct gc_rt_context* ctx, uint32_t code) {
    uint32_t i, start, now;
    int gp_idle;
    int storage_ready;
    (void)code;  /* the game reads PI's reset register for it, which stays as it was at the first start */
    (void)gc_irq_off();
    storage_ready = gc_io_drain(ctx, 0);
    if (!storage_ready || ctx->restart_count == 0) {
        /* No restart, or storage DMA that still owns the old memory:
         * back to RiftWii, as holding HOME. A game that switches
         * programs lets OSResetSystem's restart through to here
         * (gc_on_reset_system) when it names no program to boot. */
        gc_hot_reset_leave(ctx, storage_ready);
    }
    ctx->restarts++;
    gc_trace(ctx, GC_TRACE_RESTART);
    /* What the game saved, into the card file first; tried again a few
     * times, as the game cannot be given back now. */
    if (ctx->card) {
        struct gc_card* card = (struct gc_card*)(uintptr_t)ctx->card;
        for (i = 0; i < 3 && card->dirty_count != 0; ++i) gc_card_flush(ctx, card, 1);
    }
    storage_ready = gc_io_drain(ctx, 0);
    if (!storage_ready) gc_hot_reset_leave(ctx, 0);
    /* The GP's last commands done, then the GP reset, before the game
     * sets up a new FIFO. Whether it was still busy goes into the trace,
     * with the audio reset's waits. */
    __asm__ volatile("mftb %0" : "=r"(start));
    do {
        gp_idle = (*(volatile uint16_t*)(uintptr_t)GC_CP_SR & GC_CP_SR_IDLE) == GC_CP_SR_IDLE;
        __asm__ volatile("mftb %0" : "=r"(now));
    } while (!gp_idle && now - start < GC_GP_IDLE_TICKS);
    gc_gp_abort();
    gc_audio_reset(ctx);
    if (!gp_idle) ctx->audio_reset |= GC_RESTART_GP_SLOW << 12;
    gc_vi_restart(ctx);
    gc_trace(ctx, GC_TRACE_RESTART);  /* the audio and the VI as the restarted game finds them */
    storage_ready = gc_io_drain(ctx, 0);
    /* Neither the snapshot nor the new OS may overwrite late DMA. */
    if (!storage_ready) gc_hot_reset_leave(ctx, 0);
    gc_forget_game(ctx, 1);
    __asm__ volatile("mr 1,%0\n\tmr 3,%1\n\tb gc_restore_and_start" : : "r"(ctx->restart_stack), "r"(ctx) : "memory");
    for (;;) {
    }
}

/* A program the game loaded itself starts (gc_programs.h, from
 * gc_hook_program): EA's loader has read it from the disc, shut its own OS
 * down (OSResetSystem's shutdown) and jumped to it. Like a restart, the
 * old program's drive command, alarms, music and card transfer end here,
 * its I/O drained first so no DMA lands in the new one; then RiftWii's
 * words in MEM1 become this program's, as the plugin left them for it:
 * the hooks' replay slots, the context's function addresses, the ARAM
 * stubs. The interrupts stay off until the new program's OSInit. */
void gc_on_program_start(struct gc_rt_context* ctx, uint32_t index) {
    const struct gc_programs* programs = (const struct gc_programs*)(uintptr_t)ctx->programs;
    uint32_t i;
    (void)gc_irq_off();
    /* I/O that will not finish would land in the new program: back to
     * RiftWii, as Reset does then. */
    if (!gc_io_drain(ctx, 0)) gc_hot_reset_leave(ctx, 0);
    if (ctx->card) {
        struct gc_card* card = (struct gc_card*)(uintptr_t)ctx->card;
        for (i = 0; i < 3 && card->dirty_count != 0; ++i) gc_card_flush(ctx, card, 1);
        if (!gc_io_drain(ctx, 0)) gc_hot_reset_leave(ctx, 0);
    }
    if (programs != 0 && programs->magic == GC_PROGRAMS_MAGIC && index < programs->program_count) {
        const uint32_t* addresses = (const uint32_t*)(uintptr_t)programs->addresses;
        const uint32_t* values = (const uint32_t*)(uintptr_t)programs->values + index * programs->word_count;
        for (i = 0; i < programs->word_count; ++i) {
            /* Many are instructions (replay slots, the moved code, the ARAM
             * stubs) that may have run for the program before: out to
             * memory, and out of the instruction cache. */
            *(volatile uint32_t*)(uintptr_t)addresses[i] = values[i];
            gc_flush(addresses[i], 4);
            gc_icache_invalidate(addresses[i], 4);
        }
    }
    ctx->program_starts++;
    ctx->program_current = index + 1;  /* 0: the disc's own */
    gc_forget_game(ctx, 0);
    gc_trace(ctx, GC_TRACE_PROGRAM);
}

/* Test presses: every 240 reads, 10 reads of A, then B, then Start, on
 * port 1 (PADStatus: buttons at 0, err at 10; A 0x100, B 0x200, Start
 * 0x1000). */
/* VIConfigure(rm) with 480p or a picture width on: the mode goes on as
 * gc_video_pick (gc_video.h) changes its copy, else as the game made it. */
uint32_t gc_on_vi_configure(struct gc_rt_context* ctx, uint32_t mode) {
    const uint8_t* bytes = (const uint8_t*)(uintptr_t)mode;
    uint8_t* copy = (uint8_t*)ctx->vi_copy;
    const uint32_t progressive = (ctx->flags & GC_FLAG_PROGRESSIVE) != 0;
    uint32_t used = mode;
    ++ctx->vi_configures;
    if (gc_video_pick(bytes, progressive, ctx->vi_width, copy)) used = (uint32_t)(uintptr_t)copy;
    ctx->vi_mode = (uint32_t)bytes[GC_RM_TV_MODE + 3] << 8 | ((const uint8_t*)(uintptr_t)used)[GC_RM_TV_MODE + 3];
    if (gc_video_width_kept(bytes, ctx->vi_width)) ctx->vi_mode |= GC_VI_WIDTH_KEPT;
    return used;
}

/* A float's bits times 3/4, in integers: the blob does no floating point
 * (it runs on the game's FPU state, which it must leave as it found it),
 * so the widening is done on the bits. Zero, denormals, infinities and
 * NaNs come back as they are; a projection has none of them where it is
 * widened. */
static uint32_t gc_float_three_quarters(uint32_t bits) {
    const uint32_t sign = bits & 0x80000000u;
    uint32_t exponent = (bits >> 23) & 0xFFu;
    uint32_t mantissa;
    if (exponent <= 1u || exponent == 0xFFu) return bits;
    /* The 24-bit mantissa with its hidden 1, times 3: 25 or 26 bits. The
     * value is that times 2^(exponent - 152); back to 24 bits, rounded. */
    mantissa = ((bits & 0x7FFFFFu) | 0x800000u) * 3u;
    if (mantissa >= 0x2000000u) {
        mantissa = (mantissa + 2u) >> 2;
    } else {
        mantissa = (mantissa + 1u) >> 1;
        exponent -= 1u;
    }
    if (mantissa == 0x1000000u) {  /* rounded up to the next power of two */
        mantissa >>= 1;
        exponent += 1u;
    }
    return sign | exponent << 23 | (mantissa & 0x7FFFFFu);
}

/* Notes the caller (its return address) of a perspective projection for
 * the trace: which of the game's passes are widened, with the shape of
 * the first matrix each one gave. */
static void gc_note_projection_caller(struct gc_rt_context* ctx, uint32_t caller, const uint32_t* matrix) {
    uint32_t i;
    for (i = 0; i < GC_PROJECTION_CALLERS; ++i) {
        uint32_t* slot = &ctx->projection_callers[i * 4];
        if (slot[0] == caller) {
            ++slot[1];
            return;
        }
        if (slot[0] == 0) {
            slot[0] = caller;
            slot[1] = 1;
            slot[2] = matrix[0];
            slot[3] = matrix[5];
            return;
        }
    }
}

/* GXSetProjection(mtx, type) with 16:9 on: a perspective projection
 * (type 0) is copied with its x scale, mtx[0][0], times 3/4: the same
 * view drawn into a picture a widescreen TV stretches by 4/3. An
 * orthographic one (type 1: menus, text, the 2D layers) is left alone.
 * GXSetProjection reads the copy at once, so one is enough, and the
 * game's own matrix is never changed (games set the same one every
 * frame). gc_hook_projection runs the original on what this returns. */
#define GC_GX_PERSPECTIVE 0u

uint32_t gc_on_projection(struct gc_rt_context* ctx, uint32_t mtx, uint32_t type, uint32_t caller) {
    const uint32_t* matrix = (const uint32_t*)(uintptr_t)mtx;
    uint32_t i;
    if (type != GC_GX_PERSPECTIVE) {
        ++ctx->orthographics;
        return mtx;
    }
    ++ctx->projections;
    gc_note_projection_caller(ctx, caller, matrix);
    for (i = 0; i < 16; ++i) ctx->projection[i] = matrix[i];
    if (ctx->flags & GC_FLAG_WIDEN) ctx->projection[0] = gc_float_three_quarters(matrix[0]);
    return (uint32_t)(uintptr_t)ctx->projection;
}

/* PADControlMotor(chan, command) has run (gc_hook_pad_motor): a GameCube
 * controller on that port took it from the original; a Wii Remote that
 * plays on the port gets it from the next PADRead (gc_bt_poll). */
void gc_on_pad_motor(struct gc_rt_context* ctx, uint32_t chan, uint32_t command) {
    if (ctx->bt) gc_bt_motor((struct gc_bt*)(uintptr_t)ctx->bt, chan, command, (ctx->flags & GC_FLAG_REMOTE_RUMBLE) != 0);
    if (ctx->gcad) gc_gcad_motor((struct gc_gcad*)(uintptr_t)ctx->gcad, chan, command);
}

/* SIProbe(chan) has run (gc_hook_si_probe) and found `type`: the game
 * gets gc_ports_probe's answer, so a game that only reads a port SIProbe
 * calls a controller (Mario Kart: Double Dash!!) takes a Wii Remote's
 * port where no GameCube port answers (a Wii U, an empty port). */
uint32_t gc_on_si_probe(struct gc_rt_context* ctx, uint32_t chan, uint32_t type) {
    const uint32_t answer = gc_ports_probe(chan, type, ctx->pad_played);
    ++ctx->si_probes;
    if (answer != type) ++ctx->si_probes_answered;
    return answer;
}

/* The enabled state must be sampled before the SDK's read: its unplug
 * path clears EnabledBits as well as replacing the cached type with 8. */
void gc_before_pad_read(struct gc_rt_context* ctx) {
    const uint32_t msr = gc_irq_off();
    ctx->pad_enabled_before = gc_pad_enabled(ctx);
    ctx->native_ports |= gc_ports_native((const volatile uint32_t*)(uintptr_t)ctx->si_type_cache);
    gc_irq_restore(msr);
}

static uint32_t gc_pad_errors(const uint8_t* status) {
    uint32_t port, errors = 0;
    if (status == 0) return 0xFFFFFFFFu;
    for (port = 0; port < GC_PORTS; ++port) errors = errors << 8 | status[12 * port + 10];
    return errors;
}

uint32_t gc_on_pad_read(struct gc_rt_context* ctx, uint32_t status, uint32_t result) {
    const uint32_t n = ctx->pad_reads++;
    const uint32_t previous_native = ctx->native_ports;
    const uint32_t previous_errors = ctx->pad_raw_errs;
    const uint32_t previous_played = ctx->pad_played;
    uint32_t native;
    /* The four PADStatus (12 bytes each) the game asked for, read and
     * written below: anything else (a function found as PADRead that takes
     * other arguments) is left alone rather than followed. */
    if (status < 0x80000000u || status > 0x81800000u - 4u * 12u || (status & 1u) != 0) status = 0;
    ctx->pad_raw_errs = gc_pad_errors((const uint8_t*)(uintptr_t)status);
    native = gc_ports_native_update((const volatile uint32_t*)(uintptr_t)ctx->si_type_cache,
                                    (const uint8_t*)(uintptr_t)status, ctx->pad_enabled_before, previous_native);
    ctx->native_ports = native;
    gc_guard(ctx);  /* once a frame: IOS rewrites its counter there all the time */
    gc_stream_fetch(ctx);  /* the music ring's top-up, once a frame */
    /* The adapter's controllers first: they take their ports before the
     * Wii Remotes fill what is left (gc_gcad.h). */
    if (ctx->gcad && status != 0) {
        struct gc_gcad* ad = (struct gc_gcad*)(uintptr_t)ctx->gcad;
        gc_gcad_poll(ctx, ad);
        gc_gcad_fill(ad, (uint8_t*)(uintptr_t)status, native);
    }
    if (ctx->bt && status != 0) {
        struct gc_bt* bt = (struct gc_bt*)(uintptr_t)ctx->bt;
        if (ctx->flags & GC_FLAG_FAKE_REMOTE) {
            gc_bt_fake(bt, (uint8_t*)(uintptr_t)status, n, (ctx->flags & GC_FLAG_FAKE_PRO) != 0, native);
        } else {
            /* Keep start-up's frame-counted timeouts unchanged. Once page
             * scan is ready, an idle controller needs no per-frame IPC. */
            if (bt->stage != GC_BT_STAGE_READY || gc_bt_wants_frame_polls(bt)) gc_bt_poll(ctx, bt);
            gc_bt_fill(bt, (uint8_t*)(uintptr_t)status, native);
        }
    }
    ctx->pad_filled_errs = gc_pad_errors((const uint8_t*)(uintptr_t)status);
    /* Before the test presses below: only a real fill makes a port played. */
    ctx->pad_played = gc_ports_played(ctx->pad_raw_errs, ctx->pad_filled_errs);
    if (ctx->flags & GC_FLAG_TRACE_GAMEPLAY) {
        const int changed = native != previous_native || ctx->pad_raw_errs != previous_errors ||
                            ctx->pad_enabled_before != gc_pad_enabled(ctx) || ctx->pad_played != previous_played;
        if (n % 300 == 0 || changed) ctx->flags |= GC_FLAG_PAD_TRACE_PENDING;
    }
    /* The test of the motor: the game's own PADControlMotor, rumble at
     * the 30th read of every 240 and stop at the 150th. */
    if ((ctx->flags & GC_FLAG_RUMBLE_TEST) && ctx->test_motor != 0 && ctx->bt && (n % 120) == 30) {
        /* The port the test remote plays on: Dolphin's own GameCube
         * controller may hold port 1, which moves the remote to port 2. */
        const uint32_t port = ((const struct gc_bt*)(uintptr_t)ctx->bt)->port_of[0];
        if (port < 4u) ((void (*)(uint32_t, uint32_t))(uintptr_t)ctx->test_motor)(port, (n % 240) == 30 ? 1u : 0u);
    }
    if ((ctx->flags & GC_FLAG_PRESS) && status != 0 && (n % 240) < 10) {
        uint8_t* p = (uint8_t*)(uintptr_t)status;
        const uint32_t which = (n / 240) % 3;
        const uint32_t bit = which == 0 ? 0x0100u : which == 1 ? 0x0200u : 0x1000u;
        const uint32_t now = ((uint32_t)p[0] << 8) | p[1];
        p[0] = (uint8_t)((now | bit) >> 8);
        p[1] = (uint8_t)(now | bit);
        p[10] = 0;
    }
    if ((ctx->flags & GC_FLAG_LEAVE) && status != 0 && n >= 1800) {
        uint8_t* p = (uint8_t*)(uintptr_t)status;
        p[0] |= (uint8_t)(GC_LEAVE_PAD >> 8);
        p[1] |= (uint8_t)GC_LEAVE_PAD;
        p[10] = 0;
    }
    if (status != 0) gc_leave_check(ctx, (const uint8_t*)(uintptr_t)status);
    gc_reset_check(ctx, n);
    /* The test of a disc change, with no game that asks for one: the
     * motor stopped as DVDLowStopMotor would stop it, disc 2 going in two
     * seconds later and disc 1 again after the second stop. */
    if ((ctx->flags & GC_FLAG_DISC_TEST) && (n == GC_DISC_TEST_FIRST || n == GC_DISC_TEST_SECOND))
        gc_disc_motor_stop(ctx);
    if ((ctx->flags & GC_FLAG_EXEC_TEST) && n == 1500) {
        /* As OSExec("", {"rwtest"}) would: __OSBootDol's "0" (the disc's
         * own program) first, the restart code OSExec passes. Built a
         * character at a time: the blob has no constant data. */
        char program[2], argument[7];
        uint32_t argv[3], args[7];
        program[0] = '0';
        program[1] = 0;
        argument[0] = 'r';
        argument[1] = 'w';
        argument[2] = 't';
        argument[3] = 'e';
        argument[4] = 's';
        argument[5] = 't';
        argument[6] = 0;
        argv[0] = (uint32_t)(uintptr_t)program;
        argv[1] = (uint32_t)(uintptr_t)argument;
        argv[2] = 0;
        args[0] = 0xFFFFFFFFu;
        args[1] = 0xC0000000u;
        args[2] = args[3] = 0;
        args[4] = 0;
        args[5] = 2;
        args[6] = (uint32_t)(uintptr_t)argv;
        gc_on_boot_dol(ctx, args);
    }
    if ((ctx->flags & GC_FLAG_CRASH) && n == 600) {
        /* The crash screen's test on a blanked screen: no active lines, as
         * the SDK has from VIInit to the game's first frame. */
        if (ctx->flags & GC_FLAG_CRASH_BLACK)
            *(volatile uint16_t*)(uintptr_t)GC_VI_VTR = *(volatile uint16_t*)(uintptr_t)GC_VI_VTR & 0x000Fu;
        __asm__ volatile("trap");
    }
    return result;
}

/* ARAM on a Wii. In Wii mode the ARAM DMA engine reports each transfer
 * done (its interrupt still comes) but moves nothing: there is no ARAM
 * behind it (measured on a Wii with test transfers; Dolphin emulates one).
 * So ARAM lives in MEM2 at physical 0x10000000 + the ARAM address, where
 * the DSP finds it too once its microcode is pointed there, and every
 * ARStartDMA is done here by the CPU before the game's own call runs, on
 * a scratch buffer, for the interrupt. Main memory comes as an address
 * the hardware masks. */
#define GC_ARAM_BASE 0x90000000u  /* MEM2, cached (Broadway's BAT4, left on) */
#define GC_ARAM_SIZE 0x01000000u
static void gc_aram_copy(uint32_t to_main, uint32_t main, uint32_t aram, uint32_t length) {
    const uint32_t m = 0x80000000u | (main & 0x01FFFFE0u);
    const uint32_t a = GC_ARAM_BASE | (aram & (GC_ARAM_SIZE - 1) & ~31u);
    const uint32_t* from = (const uint32_t*)(uintptr_t)(to_main ? a : m);
    uint32_t* to = (uint32_t*)(uintptr_t)(to_main ? m : a);
    uint32_t i;
    length &= ~31u;
    if ((aram & (GC_ARAM_SIZE - 1)) + length > GC_ARAM_SIZE) return;
    /* What the game flushed for the DMA is in memory; what it will read is
     * taken from memory after its DCInvalidate, so the copy goes there. */
    gc_invalidate((uint32_t)(uintptr_t)from, length);
    for (i = 0; i < length / 4; ++i) to[i] = from[i];
    gc_flush((uint32_t)(uintptr_t)to, length);
}

uint32_t gc_on_ar_dma(struct gc_rt_context* ctx, uint32_t type, uint32_t main, uint32_t aram, uint32_t length) {
    gc_aram_copy(type != 0, main, aram, length);
    ctx->ar_dmas++;
    ctx->last_ar[0] = type;
    ctx->last_ar[1] = main;
    ctx->last_ar[2] = aram;
    ctx->last_ar[3] = length;
    if (ctx->ar_dmas <= 16 || (ctx->ar_dmas & 255) == 0) gc_trace(ctx, GC_TRACE_AR);
    /* The game's own call still runs, for the interrupt it waits for, on
     * this buffer for 32 bytes, with its own ARAM address: an ARAM read on
     * a Wii writes zeros (there is no ARAM behind it) and would undo the
     * copy above; with the scratch's own address for ARAM instead, a Wii's
     * Luigi's Mansion stopped after 281 transfers, waiting.
     * Dolphin does perform the call, into MEM2 at the ARAM address, which
     * is the copy's own place: so the buffer holds the copy's first 32
     * bytes, and a transfer into ARAM writes them again unchanged. */
    {
        uint32_t* scratch = (uint32_t*)(uintptr_t)ctx->aram_scratch;
        const uint32_t* first = (const uint32_t*)(uintptr_t)(GC_ARAM_BASE | (aram & (GC_ARAM_SIZE - 1) & ~31u));
        uint32_t i;
        for (i = 0; i < 8; ++i) scratch[i] = first[i];
        gc_flush((uint32_t)(uintptr_t)scratch, 32);
    }
    return gc_physical((uint32_t)(uintptr_t)ctx->aram_scratch);
}

/* After the game's own ARStartDMA has started the hardware's 32 bytes:
 * with interrupts off, wait until the transfer's interrupt is pending, so
 * it is taken the moment the caller turns them back on.
 *
 * A Wii finishes those 32 bytes almost at once, and the interrupt used to
 * come a few instructions after the call, wherever the game had got to.
 * The SDK's ARQ queue starts a request's next 4 KB chunk from the ARAM
 * interrupt, so a long request's chunks finish one after another while
 * the thread that posted it runs on, a few instructions between each,
 * and the request's callback lands somewhere after ARQPostRequest has
 * returned. JSystem's ARAM streaming does `count++` (load, add, store,
 * interrupts on) right after posting, and its callback does `count--`: a
 * callback between that load and store was lost, the count stayed at its
 * limit and the game waited for it forever (Super Mario Eclipse after
 * about 560 transfers, Luigi's Mansion after 272, with Disc reads Like a
 * GameCube, on a Wii only). A GameCube's ARAM takes long enough that the
 * increment always comes first; Dolphin's JIT never stops between the
 * load and the store.
 *
 * With the interrupt pending before the hook returns, the whole chain
 * runs inside the OSRestoreInterrupts at the end of ARQPostRequest: each
 * chunk's interrupt is taken straight after the last one's rfi, before
 * the thread runs another instruction, and the callback comes before the
 * caller's own bookkeeping, always, as a short request's already did.
 * Only registers are read here (the DSP's control register and the PI's
 * cause): nothing is masked, delayed or written. When the caller has
 * interrupts on, the interrupt is taken as it comes and nothing waits. */
#define GC_PI_CAUSE 0xCC003000u
#define GC_PI_CAUSE_DSP 0x00000040u
#define GC_MSR_EE 0x8000u
#define GC_AR_WAIT_TICKS 6075u        /* 100 us of a Wii's timebase; 32 bytes take far less */
void gc_after_ar_dma(struct gc_rt_context* ctx) {
    uint32_t msr, start, now, waited;
    __asm__ volatile("mfmsr %0" : "=r"(msr));
    if (msr & GC_MSR_EE) return;
    __asm__ volatile("mftb %0" : "=r"(start));
    for (;;) {
        const uint16_t csr = *(volatile uint16_t*)(uintptr_t)GC_DSP_CSR;
        const int finished = (csr & GC_DSP_CSR_DMA_BUSY) == 0 && (csr & GC_DSP_CSR_ARINT) != 0;
        /* With the game's ARAM interrupt masked there is nothing to wait
         * for beyond the transfer itself. */
        const int raised = (csr & GC_DSP_CSR_ARINTMSK) == 0 || (gc_read32(GC_PI_CAUSE) & GC_PI_CAUSE_DSP) != 0;
        __asm__ volatile("mftb %0" : "=r"(now));
        waited = now - start;
        if (finished && raised) break;
        if (waited > GC_AR_WAIT_TICKS) {
            ctx->ar_wait_timeouts++;
            break;
        }
    }
    ctx->ar_waits++;
    if (waited > ctx->ar_wait_longest) ctx->ar_wait_longest = waited;
}

/* The loader's record just before the jump: the first one written through
 * the blob's own IPC, as the game will use it. */
void gc_on_trace_now(struct gc_rt_context* ctx, uint32_t where) {
    /* The jump's: the VI shows RiftWii's last frame, in the game's TV
     * format; its registers are noted as they are, then the screen goes
     * blank until the game's own picture. */
    if (where == GC_TRACE_JUMP) {
        gc_vi_note(ctx);
        gc_vi_read_all(ctx->vi_at_jump);  /* for the hand-over (gc_vi_hand_over) */
        /* and the audio hardware as the first start has it (gc_audio_reset) */
        ctx->ar_config_at_jump = gc_read16(GC_AR_SIZE) << 16 | gc_read16(GC_AR_REFRESH);
        ctx->ai_cr_at_jump = gc_read32(GC_AI_CR);
        gc_vi_blank_now(ctx);
    }
    gc_trace(ctx, where);
}

/* A crashed context's registers and the LRs saved up its stack (each
 * frame's back chain at 0, its caller's LR at 4 of the frame above),
 * while the frames stay in MEM1 or the private MEM2 worker stack. */
static int gc_in_mem1(uint32_t a) { return a >= 0x80000000u && a < 0x81800000u && (a & 3u) == 0; }
static int gc_stack_frame(const struct gc_rt_context* ctx, uint32_t frame) {
    if ((frame & 3u) != 0) return 0;
    if (frame >= 0x80000000u && frame <= 0x81800000u - 8u) return 1;
    return ctx->io_stack != 0 && frame >= ctx->io_stack &&
           frame - ctx->io_stack <= GC_IO_STACK_BYTES - 8u;
}
static void gc_crash_stack(struct gc_rt_context* ctx, const uint32_t* gpr) {
    uint32_t frame = gpr[1];
    uint32_t i;
    ctx->crash_gpr[0] = gpr[1];
    ctx->crash_gpr[1] = gpr[3];
    ctx->crash_gpr[2] = gpr[4];
    ctx->crash_gpr[3] = gpr[5];
    ctx->crash_gpr[4] = gpr[31];
    for (i = 0; i < 4; ++i) {
        ctx->crash_chain[i] = 0;
    }
    for (i = 0; i < 4; ++i) {
        if (!gc_stack_frame(ctx, frame)) break;
        frame = *(const uint32_t*)(uintptr_t)frame;  /* the caller's frame */
        if (!gc_stack_frame(ctx, frame)) break;
        ctx->crash_chain[i] = *(const uint32_t*)(uintptr_t)(frame + 4);
    }
}

/* After a crash: the frame on screen blue (through the uncached mirror,
 * so the VI shows it at once) and the disc slot's blue light flashing
 * (Hollywood GPIO, Broadway's bank, bit 5), since the game's own picture
 * may hide the frame. With the menu's card (crash_card), the header, the
 * register dump and the hint go on it in white (gc_crash.h). */
#define GC_GPIO_OUT 0xCD0000C0u
#define GC_GPIO_SLOT_LED 0x20u
/* The card's coverage (0-255) at the frame's (x, y): 0 outside it, or
 * with no card. */
static uint32_t gc_card_at(const uint8_t* card, int32_t x, int32_t y) {
    if (card == 0) return 0;
    if (x < 0 || y < 0 || x >= (int32_t)GC_CRASH_CARD_W || y >= (int32_t)GC_CRASH_CARD_H) return 0;
    return gc_crash_level(card, GC_CRASH_CARD_W, (uint32_t)x, (uint32_t)y) * 85u;
}
/* RiftWii's own memory at MEM1's top starts this far below the context
 * at most (BI2, then the blob, the context near its start); the crash
 * screen draws nothing there, as the blob is running and the memory
 * card's sectors are still to be written back. */
#define GC_CRASH_KEEP_BELOW_CONTEXT 0x8000u
/* The picture the VI shows now, both fields: blue, with the card in the
 * middle, or all black when `black` is 1. 0 when the VI's registers give
 * no frame to draw in. */
static int gc_crash_frame(const struct gc_rt_context* ctx, const uint8_t* card, int black) {
    const uint32_t keep_from = gc_physical((uint32_t)(uintptr_t)ctx) - GC_CRASH_KEEP_BELOW_CONTEXT;
    const uint32_t top = gc_vi_fb(GC_VI_TFBL), bottom = gc_vi_fb(GC_VI_BFBL);
    const uint16_t hsw = *(volatile uint16_t*)(uintptr_t)GC_VI_HSW;
    const uint32_t stride = (hsw & 0xFFu) * 32u, words = ((hsw >> 8) & 0x7Fu) * 32u / 4u;
    const uint32_t lines = GC_VI_ACTIVE_LINES(*(volatile uint16_t*)(uintptr_t)GC_VI_VTR);
    /* A bottom field only when its register holds a frame buffer too. */
    const uint32_t fields = bottom != top && bottom != 0 && bottom < keep_from ? 2u : 1u;
    const int32_t width = (int32_t)(words * 2u), height = (int32_t)(lines * fields);
    const int32_t card_left = (width - (int32_t)GC_CRASH_CARD_W) / 2;
    const int32_t card_top = (height - (int32_t)GC_CRASH_CARD_H) / 2;
    uint32_t field, k, w;
    if (top == 0 || top >= keep_from || stride == 0 || words == 0 || lines == 0) return 0;
    for (field = 0; field < fields; ++field) {
        const uint32_t base = field == 0 ? top : bottom;
        for (k = 0; k < lines; ++k) {
            volatile uint32_t* row = (volatile uint32_t*)(uintptr_t)((base + k * stride) | 0xC0000000u);
            const int32_t y = (int32_t)(k * fields + field) - card_top;
            if (base + k * stride + words * 4u > keep_from) break;
            for (w = 0; w < words; ++w) {
                /* Two pixels a word: Y0 Cb Y1 Cr. */
                const int32_t x = (int32_t)(w * 2u) - card_left;
                row[w] = black ? GC_YUYV_BLACK : gc_crash_yuyv(gc_card_at(card, x, y), gc_card_at(card, x + 1, y));
            }
        }
    }
    return 1;
}
/* The crash screen into the frame the VI shows. A screen the game had
 * blanked, or one it had not set up yet (a crash in its first moments),
 * gets back the registers of the last picture shown (vi_shown) first:
 * drawing into a frame the VI does not scan out left the TV black. */
static void gc_crash_draw(const struct gc_rt_context* ctx) {
    const uint8_t* card = (const uint8_t*)(uintptr_t)ctx->crash_card;
    const uint32_t keep_from = gc_physical((uint32_t)(uintptr_t)ctx) - GC_CRASH_KEEP_BELOW_CONTEXT;
    volatile uint32_t* p;
    uint32_t i;
    if (!gc_vi_showing()) gc_vi_put_back(ctx);
    if (gc_crash_frame(ctx, card, 0)) return;
    /* No frame in the VI's registers: the one on screen at the jump. */
    if (ctx->xfb == 0 || gc_physical(ctx->xfb) + ctx->xfb_bytes > keep_from) return;
    p = (volatile uint32_t*)(uintptr_t)(gc_physical(ctx->xfb) | 0xC0000000u);
    for (i = 0; i < ctx->xfb_bytes / 4; ++i) p[i] = gc_crash_yuyv(0, 0);
}
/* The SDK's OSContext: the GPRs from 0, then CR, LR, CTR and XER; SRR0
 * and SRR1 at 0x198 (gc_entry.S's early and stray entries fill the same
 * places). */
#define GC_OSCONTEXT_CR 0x80u
#define GC_OSCONTEXT_CTR 0x88u
#define GC_OSCONTEXT_XER 0x8Cu
static void gc_crash_screen(const struct gc_rt_context* ctx, uint32_t os_context) {
    /* The dump, which only now knows the registers, from the strip the
     * menu drew after the card. */
    if (ctx->crash_card != 0) {
        uint8_t* card = (uint8_t*)(uintptr_t)ctx->crash_card;
        const uint32_t strip_addr = ctx->crash_card + GC_CRASH_CARD_BYTES;
        const struct gc_crash_strip* strip = (const struct gc_crash_strip*)(uintptr_t)strip_addr;
        const uint32_t* saved = (const uint32_t*)(uintptr_t)os_context;
        struct gc_crash_regs regs;
        uint32_t i;
        for (i = 0; i < 32u; ++i) regs.gpr[i] = saved ? saved[i] : 0u;
        regs.cr = saved ? saved[GC_OSCONTEXT_CR / 4u] : 0u;
        regs.lr = ctx->beat_lr;
        regs.ctr = saved ? saved[GC_OSCONTEXT_CTR / 4u] : 0u;
        regs.xer = saved ? saved[GC_OSCONTEXT_XER / 4u] : 0u;
        regs.srr0 = ctx->beat_pc;
        regs.srr1 = ctx->beat_msr;
        regs.dsisr = ctx->dsisr;
        regs.dar = ctx->dar;
        regs.exception = ctx->exception;
        for (i = 0; i < 4u; ++i) regs.chain[i] = ctx->crash_chain[i];
        gc_crash_dump(strip, card, &regs);
    }
    gc_crash_draw(ctx);
}
/* A (or HOME) pressed on a Wii Remote or a GameCube controller in port 1 (its SI
 * buffer, which the SI goes on filling with interrupts off: A is bit 24 of
 * the first word; in Wii mode the SI sits at 0xCD006400). */
#define GC_SI_C0INBUFH 0xCD006404u
/* A down now: port 1's SI word with no error (ERRSTAT, bit 31), a Wii
 * Remote's A or HOME, or a Classic Controller's a or HOME (its sixth
 * byte's 0x10 and its fifth's 0x08, active low). A GameCube controller has
 * no HOME. */
static int gc_crash_a_down(struct gc_rt_context* ctx) {
    uint32_t i;
    const uint32_t si = gc_read32(GC_SI_C0INBUFH);
    if (!(si & 0x80000000u) && (si & 0x01000000u)) return 1;
    if (ctx->bt) {
        struct gc_bt* bt = (struct gc_bt*)(uintptr_t)ctx->bt;
        gc_bt_poll(ctx, bt);
        for (i = 0; i < GC_BT_REMOTES; ++i) {
            const struct gc_bt_remote* r = &bt->remotes[i];
            if (!r->used || !r->ready) continue;
            /* A or HOME (0x08, 0x80); the Classic Controller's a or HOME. */
            if (r->core[1] & 0x88u) return 1;
            if (r->ext_kind == GC_BT_EXT_CLASSIC && r->ext_step == 4 && (!(r->ext[5] & 0x10u) || !(r->ext[4] & 0x08u)))
                return 1;
        }
    }
    return 0;
}
/* A pressed: down now and up at the last look, so the A the game crashed
 * under (often its main button) or one still held after IOS refused does
 * not count; the player sees the message first. */
static int gc_crash_a(struct gc_rt_context* ctx) {
    const int down = gc_crash_a_down(ctx);
    const int pressed = down && !ctx->crash_a_was_down;
    ctx->crash_a_was_down = (uint32_t)down;
    return pressed;
}
/* The light flashes; A or HOME goes back (to the title in leave_buf) when
 * there is a way back. */
/* About a frame (16 ms of the timebase): how often the controllers are
 * looked at, the pace gc_bt's start-up counts in. */
#define GC_CRASH_POLL_TICKS 972000u
/* Tests in Dolphin, which has no hand on a controller (GC_FLAG_CRASH with
 * GC_FLAG_LEAVE): A taken as pressed twenty seconds after the screen
 * shows, time enough to look at it. */
#define GC_CRASH_TEST_A_TICKS (20u * 60750000u)
static void gc_crash_wait(struct gc_rt_context* ctx) {
    const int test_a = (ctx->flags & GC_FLAG_CRASH) && (ctx->flags & GC_FLAG_LEAVE);
    uint32_t start, now, polled, shown;
    uint32_t toggles = 0;
    __asm__ volatile("mftb %0" : "=r"(start));
    polled = start;
    shown = start;
    ctx->crash_a_was_down = 1;  /* the first look only learns where A is */
    for (;;) {
        __asm__ volatile("mftb %0" : "=r"(now));
        if (now - start >= 10000000u) {
            start = now;
            gc_write32(GC_GPIO_OUT, gc_read32(GC_GPIO_OUT) ^ GC_GPIO_SLOT_LED);
            /* About once a second the screen again: the graphics processor
             * may still have finished a copy of the game's over it. */
            if (++toggles % 8u == 0) gc_crash_draw(ctx);
        }
        if (now - polled < GC_CRASH_POLL_TICKS) continue;
        polled = now;
        if (test_a && now - shown >= GC_CRASH_TEST_A_TICKS) {
            shown = now;  /* and again twenty seconds later, when IOS said no */
            if (ctx->leave_buf != 0 && ctx->leave_fd != 0xFFFFFFFFu) gc_leave(ctx);
            continue;
        }
        if (gc_crash_a(ctx) && ctx->leave_buf != 0 && ctx->leave_fd != 0xFFFFFFFFu) gc_leave(ctx);
    }
}

/* The game crashed: the GameCube OS's last stop for an exception nothing
 * handles, which would halt with a blank screen on a retail console. The
 * crash goes to the trace (the exception, SRR0/LR/SRR1, DSISR, DAR, some
 * registers and the callers up the stack), then the screen turns blue. */
void gc_on_fatal(struct gc_rt_context* ctx, uint32_t exception, uint32_t os_context, uint32_t dsisr, uint32_t dar) {
    ctx->exception = exception;
    ctx->dsisr = dsisr;
    ctx->dar = dar;
    if (os_context != 0) {
        ctx->beat_pc = *(const uint32_t*)(uintptr_t)(os_context + GC_OSCONTEXT_SRR0);
        ctx->beat_lr = *(const uint32_t*)(uintptr_t)(os_context + GC_OSCONTEXT_LR);
        ctx->beat_msr = *(const uint32_t*)(uintptr_t)(os_context + GC_OSCONTEXT_SRR1);
        gc_crash_stack(ctx, (const uint32_t*)(uintptr_t)os_context);
        if (gc_in_mem1(ctx->beat_pc))
            ctx->crash_word = gc_read32(gc_physical(ctx->beat_pc) | 0xC0000000u);
    }
    /* The crashed continuation will never resume, but IOS can still DMA
     * into its buffers. Drain it before the fatal trace reuses them. */
    (void)gc_irq_off();
    ctx->io_running = 0;
    if (gc_io_drain(ctx, 0)) gc_trace(ctx, GC_TRACE_FATAL);
    gc_crash_screen(ctx, os_context);
    /* Nothing reads the pads now to switch a Wii Remote's motor off. */
    if (ctx->bt) gc_bt_stop_rumble(ctx, (struct gc_bt*)(uintptr_t)ctx->bt);
    gc_crash_wait(ctx);
}

/* A jump into RiftWii's own data (gc_entry.S's gc_stray_entry): the crash
 * screen and the trace, with CTR in DAR. */
void gc_on_stray(struct gc_rt_context* ctx, uint32_t os_context, uint32_t ctr) {
    gc_on_fatal(ctx, GC_EXCEPTION_STRAY, os_context, 0, ctr);
}
