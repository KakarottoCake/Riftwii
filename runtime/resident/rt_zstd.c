/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* RVZ decoder adapter. Keep the GameCube worker in reserved r31 across
 * all decoder calls, including entropy loops and long output copies. */
#include "rt_hook.h"

#ifdef RT_GC_IO
void gc_io_checkpoint(void* io);
static void gc_zstd_checkpoint(void) {
    void* io;
    __asm__ volatile("mr %0,31" : "=r"(io));
    gc_io_checkpoint(io);
}
#define RTZSTD_CHECKPOINT() gc_zstd_checkpoint()
#endif

/* Compile in this translation unit so -ffixed-r31 also covers every
 * decoder helper. The host exercises exactly the same decoder. */
#include "../rtzstd.c"

uint32_t rt_zstd_init(uint32_t workspace, uint32_t bytes) {
    return (uint32_t)(uintptr_t)rtzstd_init((void*)(uintptr_t)workspace, bytes);
}

int32_t rt_zstd_decode(uint32_t dctx, uint8_t* dst, uint32_t capacity, const uint8_t* src, uint32_t length) {
    return rtzstd_decode((void*)(uintptr_t)dctx, dst, capacity, src, length);
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

/* r31 remains nonvolatile across worker switches. This compilation alone
 * reserves it; the rest of the GameCube runtime keeps its normal ABI. */
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
