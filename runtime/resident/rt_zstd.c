/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
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
#ifdef RT_GC_IO
void gc_io_checkpoint(void* io);
/* Only this compilation reserves r31. The decode wrapper installs its
 * worker there and restores the caller's value afterwards, so even the
 * literal decoder (which has no DCtx argument) can yield without globals. */
static void gc_zstd_checkpoint(void) {
    void* io;
    __asm__ volatile("mr %0,31" : "=r"(io));
    gc_io_checkpoint(io);
}
#define RIFTWII_ZSTD_CHECKPOINT(dctx) gc_zstd_checkpoint()
#endif
#include "zstddeclib.c"

/* zstddeclib.c lowers some copies to calls. The Wii blob's memcpy is in
 * rt_hook.c; the GameCube one below also returns from long copies. */
void* memset(void* dst, int value, __SIZE_TYPE__ n) {
    unsigned char* d = (unsigned char*)dst;
#ifdef RT_GC_IO
    while (n != 0) {
        const __SIZE_TYPE__ piece = n < 4096 ? n : 4096;
        __SIZE_TYPE__ i;
        for (i = 0; i < piece; ++i) d[i] = (unsigned char)value;
        d += piece;
        n -= piece;
        gc_zstd_checkpoint();
    }
#else
    while (n--) *d++ = (unsigned char)value;
#endif
    return dst;
}

#ifdef RT_GC_IO
/* Only the GameCube decoder calls this. Its r31 worker stays installed
 * through all libc substitutes, including initialization's clears. */
void* memcpy(void* dst, const void* src, __SIZE_TYPE__ n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n != 0) {
        const __SIZE_TYPE__ piece = n < 4096 ? n : 4096;
        __SIZE_TYPE__ i;
        for (i = 0; i < piece; ++i) d[i] = s[i];
        d += piece;
        s += piece;
        n -= piece;
        gc_zstd_checkpoint();
    }
    return dst;
}
#endif

void* memmove(void* dst, const void* src, __SIZE_TYPE__ n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
#ifdef RT_GC_IO
    if (d < s) {
        while (n != 0) {
            const __SIZE_TYPE__ piece = n < 4096 ? n : 4096;
            __SIZE_TYPE__ i;
            for (i = 0; i < piece; ++i) d[i] = s[i];
            d += piece;
            s += piece;
            n -= piece;
            gc_zstd_checkpoint();
        }
    } else {
        while (n != 0) {
            const __SIZE_TYPE__ piece = n < 4096 ? n : 4096;
            __SIZE_TYPE__ i;
            for (i = 0; i < piece; ++i) {
                --n;
                d[n] = s[n];
            }
            gc_zstd_checkpoint();
        }
    }
#else
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        while (n--) d[n] = s[n];
    }
#endif
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

#ifdef RT_GC_IO
uint32_t rt_zstd_init_io(uint32_t workspace, uint32_t bytes, void* io) {
    void* saved_worker;
    uint32_t result;
    __asm__ volatile("mr %0,31\n\tmr 31,%1" : "=&r"(saved_worker) : "r"(io) : "memory");
    result = rt_zstd_init(workspace, bytes);
    gc_zstd_checkpoint();
    __asm__ volatile("mr 31,%0" : : "r"(saved_worker) : "memory");
    return result;
}

/* r31 is reserved by -ffixed-r31 for this compilation. It remains an
 * ordinary nonvolatile register across every call and worker switch. */
int32_t rt_zstd_decode_io(uint32_t dctx, uint8_t* dst, uint32_t capacity, const uint8_t* src, uint32_t length,
                          void* io) {
    void* saved_worker;
    int32_t result;
    __asm__ volatile("mr %0,31\n\tmr 31,%1" : "=&r"(saved_worker) : "r"(io) : "memory");
    gc_zstd_checkpoint();
    result = rt_zstd_decode(dctx, dst, capacity, src, length);
    gc_zstd_checkpoint();
    __asm__ volatile("mr 31,%0" : : "r"(saved_worker) : "memory");
    return result;
}
#endif
