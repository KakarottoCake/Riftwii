// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The stacks libogc's crt0 sets up, in place of its default_stacks.o
// (whose two pointers are weak, and which is then not linked at all).
//
// The main thread's stack is mem::kMainStack in MEM2, above an IOS reload's
// reach like the heap's MEM2 part: crt0 writes to MEM2 already before it
// switches to it (the start-up stash's copy, wii/core/stash.hpp), and other
// threads have run on MEM2 stacks all along (Home's scan, the cover
// loader). libogc's default kept 128 KiB of MEM1 for it.
//
// The exception stack stays in MEM1, the same 16 KiB as libogc's: crt0
// runs on it before .bss is cleared, and an exception begins without
// address translation.
#include "memlimits.hpp"

namespace {

alignas(8) unsigned char g_exception_stack[0x4000];

}  // namespace

// crt0 loads both with small-data addressing (@sda21).
extern "C" {
__attribute__((section(".sdata"))) void* __ppc_excpt_sp = g_exception_stack + sizeof(g_exception_stack);
__attribute__((section(".sdata"))) void* __ppc_main_sp =
    reinterpret_cast<void*>(riftwii::wii::mem::kMainStack + riftwii::wii::mem::kMainStackBytes);
}
