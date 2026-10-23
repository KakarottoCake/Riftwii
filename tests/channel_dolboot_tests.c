// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the channel's actual argument placement, including a copy that
// would otherwise fault at address zero on the host.
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifndef __has_feature
#define __has_feature(feature) 0
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#elif defined(__linux__)
#include <sys/mman.h>
#endif

static uintptr_t copied_to;
static void* checked_copy(void* dst, const void* src, size_t size) {
    copied_to = (uintptr_t)dst;
    if (copied_to < 0x81000000u || copied_to + size > 0x81001000u) return dst;
    return memcpy(dst, src, size);
}

// Include the implementation so its private argument writer is tested
// without shutting down the host or jumping into PowerPC instructions.
#define memcpy checked_copy
#include "../channel/common/dolboot.c"
#undef memcpy

u8 __app_start[1];
u8 __bss_end[1];

int main(void) {
#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
    puts("Channel argument test needs the Wii's MEM1 addresses, reserved by AddressSanitizer.");
    return 77;
#endif
    void* memory = NULL;
#if defined(_WIN32) || defined(__CYGWIN__)
    memory = VirtualAlloc((void*)0x81000000u, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#elif defined(__linux__)
    memory = mmap((void*)0x81000000u, 0x1000, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (memory == MAP_FAILED) memory = NULL;
#endif
    if (memory != (void*)0x81000000u) {
        puts("Channel argument test cannot map MEM1.");
        return 77;
    }

    *(u32*)((u8*)memory + 4) = ARGV_MAGIC;
    DolHeader header = {0};
    header.offset[0] = 0x100;
    header.address[0] = 0x81000000;
    header.size[0] = 0x100;
    header.entry = header.address[0];
    const u32 bss_addresses[] = {0, 0xffffffe0u, 0xffffffffu, 0x81000200u};
    int failures = 0;
    for (unsigned i = 0; i < sizeof(bss_addresses) / sizeof(bss_addresses[0]); ++i) {
        header.bss_address = bss_addresses[i];
        header.bss_size = i == 3 ? 0x80 : 0;
        const uintptr_t expected = i == 3 ? 0x81000280u : 0x81000100u;
        if (!dolboot_valid((const u8*)&header, 0x200)) {
            puts("FAILED: fixture should pass DOL validation.");
            ++failures;
            continue;
        }
        copied_to = UINTPTR_MAX;
        set_argv(&header, "boot.dol\0", 10);
        if (copied_to != expected) {
            printf("FAILED: BSS %08x+%x placed arguments at %llx, expected %llx.\n",
                   header.bss_address, header.bss_size,
                   (unsigned long long)copied_to, (unsigned long long)expected);
            ++failures;
        }
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    VirtualFree(memory, 0, MEM_RELEASE);
#elif defined(__linux__)
    munmap(memory, 0x1000);
#endif
    return failures ? 1 : 0;
}
