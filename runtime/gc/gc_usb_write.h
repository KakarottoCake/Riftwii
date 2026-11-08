/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Included after the target or host storage primitives. */

/* d2x's USB mass-storage requests ('U' 'M' 'S'): the sector and the count
 * as two input words of the SENDCMD block's room, then the data, an input
 * for a write and an output for a read. The caller owns the storage
 * buffers. */
static int32_t gc_usb_transfer(struct gc_rt_context* ctx, uint32_t ioctl, uint32_t sector, uint32_t count,
                               uint32_t buf, uint32_t in_count, uint32_t out_count) {
    struct gc_rt_cold* const cold = gc_cold(ctx);
    uint32_t* q = cold->request;
    q[0] = sector;
    q[1] = count;
    cold->vec[0] = gc_physical((uint32_t)(uintptr_t)&q[0]);
    cold->vec[1] = 4;
    cold->vec[2] = gc_physical((uint32_t)(uintptr_t)&q[1]);
    cold->vec[3] = 4;
    cold->vec[4] = gc_physical(buf);
    cold->vec[5] = count * GC_SECTOR;
    gc_flush((uint32_t)(uintptr_t)q, 32);
    gc_flush((uint32_t)(uintptr_t)cold->vec, sizeof(cold->vec));
    gc_flush(buf, count * GC_SECTOR);  /* no dirty lines left to land on the DMA's bytes */
    return gc_ioctlv(ctx, ctx->usb_fd, ioctl, in_count, out_count);
}

/* UMS write: sector, count and bytes are all input vectors. The caller
 * has checked every extent before the jump. */
int32_t gc_usb_write(struct gc_rt_context* ctx, uint32_t sector, uint32_t count, uint32_t source) {
    return gc_usb_transfer(ctx, 0x554D5304u, sector, count, source, 3, 0);
}
