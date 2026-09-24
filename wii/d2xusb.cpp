// SPDX-License-Identifier: GPL-3.0-or-later
#include "d2xusb.hpp"

#include <gccore.h>
#include <ogc/ipc.h>

#include <algorithm>
#include <cstring>

namespace riftwii::wii {
namespace {

// d2x-cios ehci-module/module.h: ('U' << 24) | ('M' << 16) | ('S' << 8) + n,
// all IOCTLVs. Reads and writes return 0 on success.
constexpr u32 kUmsInit = 0x554D5301;
constexpr u32 kUmsCapacity = 0x554D5302;  // out: the sector size; returns the sector count
constexpr u32 kUmsRead = 0x554D5303;      // in: sector, count; out: the data
constexpr u32 kSectorBytes = 512;
constexpr u32 kBounceSectors = 64;

char g_path[] ATTRIBUTE_ALIGN(32) = "/dev/usb2";
s32 g_fd = -1;
u32 g_args[8] ATTRIBUTE_ALIGN(32);
ioctlv g_vec[3] ATTRIBUTE_ALIGN(32);
u8 g_bounce[kBounceSectors * kSectorBytes] ATTRIBUTE_ALIGN(32);

bool transfer(u32 sector, u32 count, void* buffer) {
    if (g_fd < 0 || count == 0) return false;
    g_args[0] = sector;
    g_args[1] = count;
    g_vec[0].data = &g_args[0];
    g_vec[0].len = 4;
    g_vec[1].data = &g_args[1];
    g_vec[1].len = 4;
    g_vec[2].data = buffer;
    g_vec[2].len = count * kSectorBytes;
    return IOS_Ioctlv(g_fd, kUmsRead, 2, 1, g_vec) == 0;
}

}  // namespace

bool d2x_usb_open(std::string& error) {
    if (g_fd >= 0) return true;
    const s32 fd = IOS_Open(g_path, 0);
    if (fd < 0) {
        error = "cannot open d2x's USB device " + std::string(g_path) + " (" + std::to_string(fd) + ")";
        return false;
    }
    const s32 init = IOS_Ioctlv(fd, kUmsInit, 0, 0, nullptr);
    if (init < 0) {
        IOS_Close(fd);
        error = "d2x's USB device could not start the drive (" + std::to_string(init) + ")";
        return false;
    }
    g_args[2] = 0;
    g_vec[0].data = &g_args[2];
    g_vec[0].len = 4;
    const s32 sectors = IOS_Ioctlv(fd, kUmsCapacity, 0, 1, g_vec);
    DCInvalidateRange(g_args, sizeof(g_args));
    if (sectors <= 0 || g_args[2] != kSectorBytes) {
        IOS_Close(fd);
        error = "d2x's USB device reports " + std::to_string(g_args[2]) + "-byte sectors (" +
                std::to_string(sectors) + " of them); RVZ games need 512";
        return false;
    }
    g_fd = fd;
    return true;
}

std::int32_t d2x_usb_fd() { return g_fd; }

bool d2x_usb_read(std::uint32_t sector, std::uint32_t count, void* buffer) {
    u8* data = static_cast<u8*>(buffer);
    if ((reinterpret_cast<std::uintptr_t>(data) & 31) == 0) {
        const bool ok = transfer(sector, count, data);
        DCInvalidateRange(data, count * kSectorBytes);
        return ok;
    }
    while (count) {
        const u32 n = std::min<u32>(count, kBounceSectors);
        if (!transfer(sector, n, g_bounce)) return false;
        DCInvalidateRange(g_bounce, n * kSectorBytes);
        std::memcpy(data, g_bounce, n * kSectorBytes);
        sector += n;
        count -= n;
        data += n * kSectorBytes;
    }
    return true;
}

}  // namespace riftwii::wii
