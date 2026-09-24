/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Zstandard for the resident runtime of RVZ games. On the console the
 * vendored single-file decoder (vendor-zstd/zstddeclib.c, zstd 1.5.7,
 * BSD) is compiled into this file, freestanding: no allocation (the
 * decoder lives in a loader-provided workspace), a small literal buffer,
 * and the few libc functions it calls provided here. Its constant tables
 * make the RVZ blob the one that needs relocating (tools/rtreloc.py).
 * On the host the same two functions wrap the host library.
 */
#include "rt_hook.h"

#ifdef RT_TARGET_PPC
#define ZSTD_DECODER_INTERNAL_BUFFER (1 << 12)
#include "zstddeclib.c"

/* zstddeclib.c lowers some copies to calls; memcpy is in rt_hook.c. */
void* memset(void* dst, int value, __SIZE_TYPE__ n) {
    unsigned char* d = (unsigned char*)dst;
    while (n--) *d++ = (unsigned char)value;
    return dst;
}

void* memmove(void* dst, const void* src, __SIZE_TYPE__ n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        while (n--) d[n] = s[n];
    }
    return dst;
}

/* Reached only through the decoder's dictionary cleanup, which frees
 * nothing in a static decoder. */
void free(void* p) {
    (void)p;
}
#else
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#endif

uint32_t rt_zstd_init(uint32_t workspace, uint32_t bytes) {
    ZSTD_DCtx* dctx = ZSTD_initStaticDCtx((void*)(uintptr_t)workspace, bytes);
    return (uint32_t)(uintptr_t)dctx;
}

int32_t rt_zstd_decode(uint32_t dctx, uint8_t* dst, uint32_t capacity, const uint8_t* src, uint32_t length) {
    const size_t got = ZSTD_decompressDCtx((ZSTD_DCtx*)(uintptr_t)dctx, dst, capacity, src, length);
    if (ZSTD_isError(got) || got > 0x7FFFFFFFu) return -1;
    return (int32_t)got;
}
