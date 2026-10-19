/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Zstandard for the packed DOL's stub: the vendored single-file decoder
 * (vendor-zstd/zstddeclib.c, zstd 1.5.7, BSD), freestanding as the RVZ
 * runtime has it (runtime/resident/rt_zstd.c), in a workspace the stub
 * gives it. memcpy, memset, memmove and free are dolpack.c's. */
#include <stdint.h>

#define ZSTD_DECODER_INTERNAL_BUFFER (1 << 12)
#include "zstddeclib.c"

uint32_t dolpack_zstd_init(void* workspace, uint32_t bytes) {
    return (uint32_t)(uintptr_t)ZSTD_initStaticDCtx(workspace, bytes);
}

/* The bytes decoded, or -1. */
int32_t dolpack_zstd_decode(uint32_t dctx, void* dst, uint32_t capacity, const void* src, uint32_t length) {
    const size_t got = ZSTD_decompressDCtx((ZSTD_DCtx*)(uintptr_t)dctx, dst, capacity, src, length);
    if (ZSTD_isError(got) || got > 0x7FFFFFFFu) return -1;
    return (int32_t)got;
}
