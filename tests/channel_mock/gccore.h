// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint32_t u32;

#define ARGV_MAGIC 0x5f617267
#define SYS_SHUTDOWN 0
#define SYS_RETURNTOMENU 3

struct __argv {
    int argvMagic;
    char* commandLine;
    int length;
    int argc;
    char** argv;
    char** endARGV;
};

static inline void DCFlushRange(void* data, u32 size) { (void)data; (void)size; }
static inline void ICInvalidateRange(void* data, u32 size) { (void)data; (void)size; }
static inline void SYS_ResetSystem(int reset, int a, int b) { (void)reset; (void)a; (void)b; }
static inline void IRQ_Disable(void) {}

// MEM2's free part as libogc's arena 2 reports it; a test may move it.
static u32 mock_arena2_lo = 0x90800000u;
static u32 mock_arena2_hi = 0x933e0000u;
static inline void* SYS_GetArena2Lo(void) { return (void*)(uintptr_t)mock_arena2_lo; }
static inline void* SYS_GetArena2Hi(void) { return (void*)(uintptr_t)mock_arena2_hi; }
