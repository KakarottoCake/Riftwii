/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Included after the target or host storage primitives. */
/* UMS write: sector, count and bytes are all input vectors. The caller
 * owns the storage buffers and has checked every extent before the jump. */
int32_t gc_usb_write(struct gc_rt_context* ctx, uint32_t sector, uint32_t count, uint32_t source) {
    uint32_t* q = ctx->request;
    q[0] = sector;
    q[1] = count;
    ctx->vec[0] = gc_physical((uint32_t)(uintptr_t)&q[0]);
    ctx->vec[1] = 4;
    ctx->vec[2] = gc_physical((uint32_t)(uintptr_t)&q[1]);
    ctx->vec[3] = 4;
    ctx->vec[4] = gc_physical(source);
    ctx->vec[5] = count * GC_SECTOR;
    gc_flush((uint32_t)(uintptr_t)q, 32);
    gc_flush((uint32_t)(uintptr_t)ctx->vec, sizeof(ctx->vec));
    gc_flush(source, count * GC_SECTOR);
    return gc_ioctlv(ctx, ctx->usb_fd, 0x554D5304u, 3, 0);
}

