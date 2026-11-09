/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The GameCube blob's storage requests as separate per-call code paths
 * (one polled ioctlv, one polled ioctl, the SD and USB builders each with
 * its own request block), for tests/gcstorage_tests.cpp to compare with the
 * shared gc_ios_call of runtime/gc/gc_storage.h. This is gc_rt.c's code
 * as it was in the commit just before that change (abd92674), where the
 * IPC buffers had already moved to struct gc_rt_cold (read through
 * gc_cold); it is not the code from before the blob diet. The one edit:
 * its time-base reads (mftb) go through gc_timebase(), as the new code's
 * do, so the host can build it. */
/* One ioctlv to IOS and its reply, by polling (the Wii Remotes' reads may
 * be in flight meanwhile; their replies go to gc_bt.c). Returns IOS's
 * result, or -1000 when IOS does not answer. */
static int32_t gc_ioctlv(struct gc_rt_context* ctx, uint32_t fd, uint32_t ioctl, uint32_t in_count,
                         uint32_t out_count) {
    struct gc_rt_cold* const cold = gc_cold(ctx);
    uint32_t* rq = cold->ipc;
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
    rq[6] = gc_physical((uint32_t)(uintptr_t)cold->vec);
    gc_flush((uint32_t)(uintptr_t)rq, sizeof(cold->ipc));
    ctx->io_reply = 0;
    ctx->io_pending = phys;
    started = gc_timebase();
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
    gc_invalidate((uint32_t)(uintptr_t)rq, sizeof(cold->ipc));
    gc_guard(ctx);
    if (fd == ctx->sdio_fd) gc_sd_test_wait(ctx, test_started, cold->request[4] * GC_SECTOR);
    return (int32_t)rq[1];
}

/* CMD18: `count` sectors from `sector` into `target` (32-byte aligned). */
int gc_sd_read(struct gc_rt_context* ctx, uint32_t sector, uint32_t count, uint32_t target) {
    struct gc_rt_cold* const cold = gc_cold(ctx);
    uint32_t* q = cold->request;
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
    cold->vec[0] = gc_physical((uint32_t)(uintptr_t)q);
    cold->vec[1] = 36;
    cold->vec[2] = gc_physical(target);
    cold->vec[3] = count * GC_SECTOR;
    cold->vec[4] = gc_physical((uint32_t)(uintptr_t)cold->response);
    cold->vec[5] = GC_SD_RESPONSE_BYTES;
    gc_flush((uint32_t)(uintptr_t)q, 36);
    gc_flush((uint32_t)(uintptr_t)cold->vec, sizeof(cold->vec));
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

/* UMS write: sector, count and bytes are all input vectors. The caller
 * owns the storage buffers and has checked every extent before the jump. */
int32_t gc_usb_write(struct gc_rt_context* ctx, uint32_t sector, uint32_t count, uint32_t source) {
    struct gc_rt_cold* const cold = gc_cold(ctx);
    uint32_t* q = cold->request;
    q[0] = sector;
    q[1] = count;
    cold->vec[0] = gc_physical((uint32_t)(uintptr_t)&q[0]);
    cold->vec[1] = 4;
    cold->vec[2] = gc_physical((uint32_t)(uintptr_t)&q[1]);
    cold->vec[3] = 4;
    cold->vec[4] = gc_physical(source);
    cold->vec[5] = count * GC_SECTOR;
    gc_flush((uint32_t)(uintptr_t)q, 32);
    gc_flush((uint32_t)(uintptr_t)cold->vec, sizeof(cold->vec));
    gc_flush(source, count * GC_SECTOR);
    return gc_ioctlv(ctx, ctx->usb_fd, 0x554D5304u, 3, 0);
}


/* `count` sectors of the USB drive from `sector` into `target`, which is
 * in MEM2 (d2x's USB driver writes nowhere else) and 32-byte aligned. */
int gc_usb_read(struct gc_rt_context* ctx, uint32_t sector, uint32_t count, uint32_t target) {
    struct gc_rt_cold* const cold = gc_cold(ctx);
    uint32_t* q = cold->request;
    while (count != 0) {
        const uint32_t n = count < GC_USB_REQUEST_SECTORS ? count : GC_USB_REQUEST_SECTORS;
        int32_t result;
        q[0] = sector;
        q[1] = n;
        cold->vec[0] = gc_physical((uint32_t)(uintptr_t)&q[0]);
        cold->vec[1] = 4;
        cold->vec[2] = gc_physical((uint32_t)(uintptr_t)&q[1]);
        cold->vec[3] = 4;
        cold->vec[4] = gc_physical(target);
        cold->vec[5] = n * GC_SECTOR;
        gc_flush((uint32_t)(uintptr_t)q, 32);
        gc_flush((uint32_t)(uintptr_t)cold->vec, sizeof(cold->vec));
        gc_flush(target, n * GC_SECTOR);  /* no dirty lines left to land on the DMA's bytes */
        ctx->usb_requests++;
        result = gc_ioctlv(ctx, ctx->usb_fd, GC_UMS_READ_SECTORS, 2, 1);
        gc_invalidate(target, n * GC_SECTOR);
        ctx->usb_last_read = gc_timebase();
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

/* One ioctl (no vectors), polled like gc_ioctlv: CMD13 takes this form. */
static int32_t gc_ioctl(struct gc_rt_context* ctx, uint32_t fd, uint32_t ioctl, uint32_t in, uint32_t in_len,
                        uint32_t out, uint32_t out_len) {
    struct gc_rt_cold* const cold = gc_cold(ctx);
    uint32_t* rq = cold->ipc;
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
    gc_flush((uint32_t)(uintptr_t)rq, sizeof(cold->ipc));
    ctx->io_reply = 0;
    ctx->io_pending = phys;
    started = gc_timebase();
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
    gc_invalidate((uint32_t)(uintptr_t)rq, sizeof(cold->ipc));
    gc_invalidate(out, out_len);
    gc_guard(ctx);
    if (fd == ctx->sdio_fd) gc_sd_test_wait(ctx, test_started, 0);
    return (int32_t)rq[1];
}

/* `count` sectors (at most 16) from `buf` (32-byte aligned) to the card
 * at `sector`: CMD25, then CMD13 until the card has written them. The
 * caller holds ctx->storage_busy. */
static int32_t gc_sd_write_locked(struct gc_rt_context* ctx, uint32_t sector, uint32_t count, uint32_t buf) {
    struct gc_rt_cold* const cold = gc_cold(ctx);
    uint32_t* q = cold->request;
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
    cold->vec[0] = gc_physical((uint32_t)(uintptr_t)q);
    cold->vec[1] = 36;
    cold->vec[2] = gc_physical(buf);
    cold->vec[3] = count * GC_SECTOR;
    cold->vec[4] = gc_physical((uint32_t)(uintptr_t)cold->response);
    cold->vec[5] = GC_SD_RESPONSE_BYTES;
    gc_flush((uint32_t)(uintptr_t)q, 36);
    gc_flush((uint32_t)(uintptr_t)cold->vec, sizeof(cold->vec));
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
            cold->response[0] = 0;
            result = gc_ioctl(ctx, ctx->sdio_fd, GC_SDIO_SENDCMD, (uint32_t)(uintptr_t)q, 36,
                              (uint32_t)(uintptr_t)cold->response, 16);
            if (result < 0) return result;  /* leave the save dirty until status can be checked */
            state = (cold->response[0] >> 9) & 0xFu;
            if (state != 6 && state != 7) break;  /* left receive-data and programming */
        }
    }
    return result;
}

