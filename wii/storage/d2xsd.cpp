// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "d2xsd.hpp"

#include <gccore.h>
#include <ogc/ipc.h>
#include <ogc/mutex.h>
#include <sdcard/wiisd_io.h>

#include <algorithm>
#include <cstring>

namespace riftwii::wii {
namespace {

// d2x cIOS sdhc-module: IOCTLV commands; each returns 0 on success.
constexpr u32 kInit = 0x01;
constexpr u32 kRead = 0x02;
constexpr u32 kWrite = 0x03;
constexpr u32 kIsInserted = 0x04;
constexpr u32 kSectorBytes = 512;
constexpr u32 kBounceSectors = 64;

char g_path[] ATTRIBUTE_ALIGN(32) = "/dev/sdio/sdhc";
s32 g_fd = -1;
bool g_active = false;
u32 g_args[8] ATTRIBUTE_ALIGN(32);
ioctlv g_vec[3] ATTRIBUTE_ALIGN(32);
u8 g_bounce[kBounceSectors * kSectorBytes] ATTRIBUTE_ALIGN(32);

bool transfer(u32 command, u32 sector, u32 count, void* buffer) {
    if (g_fd < 0 || count == 0) return false;
    g_args[0] = sector;
    g_args[1] = count;
    g_vec[0].data = &g_args[0];
    g_vec[0].len = 4;
    g_vec[1].data = &g_args[1];
    g_vec[1].len = 4;
    g_vec[2].data = buffer;
    g_vec[2].len = count * kSectorBytes;
    const bool write = command == kWrite;
    return IOS_Ioctlv(g_fd, command, write ? 3 : 2, write ? 0 : 1, g_vec) == 0;
}

// Through the aligned bounce buffer unless the caller's already is.
bool chunked(u32 command, u32 sector, u32 count, u8* data) {
    if ((reinterpret_cast<std::uintptr_t>(data) & 31) == 0) {
        if (command == kWrite) DCFlushRange(data, count * kSectorBytes);
        const bool ok = transfer(command, sector, count, data);
        if (command == kRead) DCInvalidateRange(data, count * kSectorBytes);
        return ok;
    }
    while (count) {
        const u32 n = std::min(count, kBounceSectors);
        if (command == kWrite) {
            std::memcpy(g_bounce, data, n * kSectorBytes);
            DCFlushRange(g_bounce, n * kSectorBytes);
        }
        if (!transfer(command, sector, n, g_bounce)) return false;
        if (command == kRead) {
            DCInvalidateRange(g_bounce, n * kSectorBytes);
            std::memcpy(data, g_bounce, n * kSectorBytes);
        }
        sector += n;
        count -= n;
        data += n * kSectorBytes;
    }
    return true;
}

// libfat's view of the device. shutdown keeps the handle: closing it
// would shut the card down under d2x (the module's close handler does).
bool io_startup() {
    std::string ignored;
    return d2x_sd_open(ignored);
}
bool io_inserted() {
    return g_fd >= 0 && IOS_Ioctlv(g_fd, kIsInserted, 0, 0, nullptr) == 0;
}
bool io_read(sec_t sector, sec_t count, void* buffer) {
    return d2x_sd_read(static_cast<u32>(sector), static_cast<u32>(count), buffer);
}
bool io_write(sec_t sector, sec_t count, const void* buffer) {
    return d2x_sd_write(static_cast<u32>(sector), static_cast<u32>(count), buffer);
}
bool io_clear() { return true; }
bool io_shutdown() { return true; }

const DISC_INTERFACE g_io = {
    DEVICE_TYPE_WII_SD,
    FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE | FEATURE_WII_SD,
    io_startup,
    io_inserted,
    io_read,
    io_write,
    io_clear,
    io_shutdown,
};

// Every use of the card, libfat's and the raw readers' (the image scan,
// sd: files), one at a time: libogc's SD driver keeps its request in
// static memory. The menu's screenshot thread wrote a PNG while the menu
// thread scanned the card for images: a read failed until its retry and
// the PNG came out broken (a tester's Mario Kart Wii shots).
mutex_t g_lock = LWP_MUTEX_NULL;
struct Locked {
    Locked() {
        if (g_lock == LWP_MUTEX_NULL) LWP_MutexInit(&g_lock, true);
        LWP_MutexLock(g_lock);
    }
    ~Locked() { LWP_MutexUnlock(g_lock); }
};
template <const DISC_INTERFACE* D>
struct Serial {
    static bool startup() { Locked l; return D->startup(); }
    static bool inserted() { Locked l; return D->isInserted(); }
    static bool read(sec_t sector, sec_t count, void* buffer) { Locked l; return D->readSectors(sector, count, buffer); }
    static bool write(sec_t sector, sec_t count, const void* buffer) {
        Locked l;
        return D->writeSectors(sector, count, buffer);
    }
    static bool clear() { Locked l; return D->clearStatus(); }
    static bool shutdown() { Locked l; return D->shutdown(); }
    static const DISC_INTERFACE io;
};
template <const DISC_INTERFACE* D>
const DISC_INTERFACE Serial<D>::io = {
    DEVICE_TYPE_WII_SD,         FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE | FEATURE_WII_SD,
    Serial<D>::startup,         Serial<D>::inserted,
    Serial<D>::read,            Serial<D>::write,
    Serial<D>::clear,           Serial<D>::shutdown,
};

}  // namespace

bool d2x_sd_open(std::string& error) {
    if (g_fd >= 0) return true;
    const s32 fd = IOS_Open(g_path, 1);
    if (fd < 0) {
        error = "cannot open d2x's SD device " + std::string(g_path) + " (" + std::to_string(fd) + ")";
        return false;
    }
    const s32 ret = IOS_Ioctlv(fd, kInit, 0, 0, nullptr);
    if (ret != 0) {
        IOS_Close(fd);
        error = "d2x's SD device could not start the card (" + std::to_string(ret) + ")";
        return false;
    }
    g_fd = fd;
    return true;
}

std::int32_t d2x_sd_fd() { return g_fd; }

bool d2x_sd_read(std::uint32_t sector, std::uint32_t count, void* buffer) {
    return chunked(kRead, sector, count, static_cast<u8*>(buffer));
}

bool d2x_sd_write(std::uint32_t sector, std::uint32_t count, const void* buffer) {
    return chunked(kWrite, sector, count, static_cast<u8*>(const_cast<void*>(buffer)));
}

void use_d2x_sd(bool on) { g_active = on; }
bool using_d2x_sd() { return g_active; }
const DISC_INTERFACE* sd_interface() { return g_active ? &Serial<&g_io>::io : &Serial<&__io_wiisd>::io; }

}  // namespace riftwii::wii
