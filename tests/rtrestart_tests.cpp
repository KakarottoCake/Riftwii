// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The Reset's request as the pad blob writes it (runtime/rtrestart.h, #458):
// the one sector of the SD card the resident runtime writes too, through
// /dev/sdio/slot0 or d2x's /dev/sdio/sdhc.
#include <cstdint>
#include <cstring>
#include <iostream>

extern "C" {
#include "resident/rt_hook.h"
#include "rtrestart.h"
}

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

// The pad blob and the resident runtime talk to the same card the same way:
// what one writes the other's request must look like.
static_assert(RTRESTART_SECTOR == RT_SECTOR_BYTES, "one sector");
static_assert(RTRESTART_SD_D2X == RT_SD_D2X, "d2x's mode");
static_assert(sizeof(rtrestart_request) == sizeof(rt_sdio_request), "the SENDCMD request's words");
static_assert(offsetof(rtrestart_request, arg) == offsetof(rt_sdio_request, arg), "its argument");
static_assert(offsetof(rtrestart_request, dma_addr) == offsetof(rt_sdio_request, dma_addr), "its buffer");
static_assert(sizeof(rtrestart_vector) == sizeof(rt_ioctlv), "an IPC vector");
static_assert(sizeof(rtrestart_block) % 32 == 0 && alignof(rtrestart_block) == 32, "IOS flushes by line");
static_assert(offsetof(rtrestart_block, armed) % 32 == 0 && offsetof(rtrestart_block, idle) % 32 == 0 &&
                  offsetof(rtrestart_block, request) % 32 == 0 && offsetof(rtrestart_block, response) % 32 == 0 &&
                  offsetof(rtrestart_block, vec) % 32 == 0,
              "each buffer IOS touches starts on a line");

namespace {

void TestSdsc() {
    alignas(32) static rtrestart_block blk;
    std::memset(&blk, 0, sizeof(blk));
    rtrestart_call call{};
    rtrestart_prepare_write(&blk, RTRESTART_SD_BYTES, 4000, blk.armed, &call);
    EXPECT_EQ(call.ioctl, 7u);  // SENDCMD
    EXPECT_EQ(call.in_count, 2u);
    EXPECT_EQ(call.out_count, 1u);
    EXPECT_EQ(blk.request.cmd, 0x19u);  // CMD25
    EXPECT_EQ(blk.request.cmd_type, 3u);
    EXPECT_EQ(blk.request.rsp_type, 1u);
    EXPECT_EQ(blk.request.arg, 4000u * 512u);  // a byte offset
    EXPECT_EQ(blk.request.blk_cnt, 1u);
    EXPECT_EQ(blk.request.blk_size, 512u);
    EXPECT_EQ(blk.request.isdma, 1u);
    EXPECT_EQ(blk.request.dma_addr, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(blk.armed)));
    EXPECT_EQ(blk.vec[0].len, static_cast<std::uint32_t>(sizeof(rtrestart_request)));
    EXPECT_EQ(blk.vec[1].len, 512u);
    EXPECT_EQ(blk.vec[1].data, blk.request.dma_addr);
    EXPECT_EQ(blk.vec[2].len, 16u);  // the response
    EXPECT_EQ(blk.vec[0].data, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&blk.request)));
    EXPECT_EQ(blk.vec[2].data, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(blk.response)));
}

void TestSdhc() {
    alignas(32) static rtrestart_block blk;
    std::memset(&blk, 0, sizeof(blk));
    std::memset(blk.response, 0xFF, sizeof(blk.response));
    rtrestart_call call{};
    rtrestart_prepare_write(&blk, RTRESTART_SD_SDHC, 4000, blk.idle, &call);
    EXPECT_EQ(call.ioctl, 7u);
    EXPECT_EQ(blk.request.arg, 4000u);  // a sector number
    EXPECT_EQ(blk.request.dma_addr, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(blk.idle)));
    EXPECT_EQ(blk.vec[1].data, blk.request.dma_addr);
    // The card's answer starts clear.
    for (unsigned i = 0; i < 4; ++i) EXPECT_EQ(blk.response[i], 0u);
}

void TestD2x() {
    alignas(32) static rtrestart_block blk;
    std::memset(&blk, 0, sizeof(blk));
    rtrestart_call call{};
    rtrestart_prepare_write(&blk, RTRESTART_SD_D2X, 123456, blk.armed, &call);
    EXPECT_EQ(call.ioctl, 3u);  // RT_SDHC_WRITE
    EXPECT_EQ(call.ioctl, static_cast<std::uint32_t>(RT_SDHC_WRITE));
    EXPECT_EQ(call.in_count, 3u);
    EXPECT_EQ(call.out_count, 0u);
    EXPECT_EQ(blk.request.cmd, 123456u);  // the sector
    EXPECT_EQ(blk.request.cmd_type, 1u);  // one of them
    EXPECT_EQ(blk.vec[0].len, 4u);
    EXPECT_EQ(blk.vec[1].len, 4u);
    EXPECT_EQ(blk.vec[0].data, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&blk.request.cmd)));
    EXPECT_EQ(blk.vec[1].data, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&blk.request.cmd_type)));
    EXPECT_EQ(blk.vec[2].len, 512u);
    EXPECT_EQ(blk.vec[2].data, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(blk.armed)));
}

}  // namespace

int main() {
    TestSdsc();
    TestSdhc();
    TestD2x();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "rtrestart: all passed" << std::endl;
    return 0;
}
