/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Included after the target or host storage primitives. */
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

