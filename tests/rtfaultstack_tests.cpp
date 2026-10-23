// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Guard pages make a crash recorder's out-of-RAM stack read fail on the host.
#include "rtfault.h"

#include <cstdint>
#include <iostream>

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#elif defined(__linux__)
#include <sys/mman.h>
#endif

namespace {

constexpr std::size_t kRegionBytes = 0x10000;
int failures = 0;

bool map_boundary(std::uint32_t end) {
    void* start = reinterpret_cast<void*>(static_cast<std::uintptr_t>(end) - kRegionBytes);
#if defined(_WIN32) || defined(__CYGWIN__)
    void* region = VirtualAlloc(start, 2 * kRegionBytes, MEM_RESERVE, PAGE_NOACCESS);
    if (region == nullptr) return false;
    if (VirtualAlloc(region, kRegionBytes, MEM_COMMIT, PAGE_READWRITE) == nullptr) {
        VirtualFree(region, 0, MEM_RELEASE);
        return false;
    }
    return true;
#elif defined(__linux__)
    void* region = mmap(start, 2 * kRegionBytes, PROT_NONE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (region == MAP_FAILED) return false;
    if (mprotect(region, kRegionBytes, PROT_READ | PROT_WRITE) != 0) {
        munmap(region, 2 * kRegionBytes);
        return false;
    }
    return true;
#else
    (void)start;
    return false;
#endif
}

void unmap_boundary(std::uint32_t end) {
    void* start = reinterpret_cast<void*>(static_cast<std::uintptr_t>(end) - kRegionBytes);
#if defined(_WIN32) || defined(__CYGWIN__)
    VirtualFree(start, 0, MEM_RELEASE);
#elif defined(__linux__)
    munmap(start, 2 * kRegionBytes);
#else
    (void)start;
#endif
}

void test_boundary(std::uint32_t end) {
    auto* stack = reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(end) - 16);
    std::uint32_t context[0x68]{};
    context[1] = end - 16;
    rtfault_record record{};

    // The back chain itself is readable; its saved LR would be outside RAM.
    stack[0] = end - 4;
    rtfault_fill(&record, RTFAULT_DSI, context, 0, 0, "RMCE01\0\0", 0, 0, 60750000, 1);
    if (record.frame_count != 0) {
        std::cerr << "Accepted a frame whose saved LR is outside RAM\n";
        ++failures;
    }

    // The last whole frame is valid: its saved LR is the final word in RAM.
    stack[0] = end - 8;
    stack[2] = 0;
    stack[3] = 0x80012340;
    rtfault_fill(&record, RTFAULT_DSI, context, 0, 0, "RMCE01\0\0", 0, 0, 60750000, 1);
    if (record.frame_count != 1 || record.frames[0] != 0x80012340) {
        std::cerr << "Refused the last whole frame in RAM\n";
        ++failures;
    }
}

}  // namespace

int main() {
    for (std::uint32_t end : {0x81800000u, 0x94000000u}) {
        if (!map_boundary(end)) {
            std::cerr << "Cannot map guarded Wii memory; skipping stack tests\n";
            return 77;
        }
        test_boundary(end);
        unmap_boundary(end);
    }
    return failures == 0 ? 0 : 1;
}
