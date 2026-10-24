// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "d2xsd.hpp"

#include <gccore.h>
#include <ogc/cache.h>
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
// The handle and the driver choice belong to the IOS that was running
// when they were made, and d2x_sd_forget drops both before every IOS
// reload (reload_ios). A handle kept across a reload is a dead
// descriptor: the next open would have returned it without opening or
// starting the new IOS's device, and every read through it failed.
s32 g_fd = -1;
bool g_active = false;
bool g_held_by_fat = false;  // hold_sd_for_d2x_fat
// Why the last d2x_sd_open failed, for the launch's error message: libfat
// mounts through io_startup, which has nowhere to put it.
std::string g_last_error;
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

// What RiftWii's code gets while d2x's FAT module holds the card: nothing.
bool refused_startup() { return false; }
bool refused_inserted() { return false; }
bool refused_read(sec_t, sec_t, void*) { return false; }
bool refused_write(sec_t, sec_t, const void*) { return false; }
bool refused_clear() { return true; }
bool refused_shutdown() { return true; }
const DISC_INTERFACE g_refused = {
    DEVICE_TYPE_WII_SD, FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE | FEATURE_WII_SD,
    refused_startup,    refused_inserted,
    refused_read,       refused_write,
    refused_clear,      refused_shutdown,
};

}  // namespace

bool d2x_sd_open(std::string& error) {
    if (g_fd >= 0) return true;
    const s32 fd = IOS_Open(g_path, 1);
    if (fd < 0) {
        error = "cannot open d2x's SD device " + std::string(g_path) + " (" + std::to_string(fd) + ")";
        g_last_error = error;
        return false;
    }
    const s32 ret = IOS_Ioctlv(fd, kInit, 0, 0, nullptr);
    if (ret != 0) {
        IOS_Close(fd);
        error = "d2x's SD device could not start the card (" + std::to_string(ret) + ")";
        g_last_error = error;
        return false;
    }
    g_fd = fd;
    g_last_error.clear();
    return true;
}

void d2x_sd_forget() {
    // Under the card's lock, so a transfer from another thread (the
    // screenshot writer) is never halfway through the handle as it goes.
    Locked lock;
    // reload_ios calls this while the old IOS still runs, so the handle is
    // still its own and is closed properly rather than leaked: if the
    // reload then stops before the new IOS starts, the next open in the
    // same IOS gets a fresh handle and starts the card again.
    if (g_fd >= 0) IOS_Close(g_fd);
    g_fd = -1;
    // The next IOS may have no d2x at all (IOS 58 for the menu): libogc's
    // driver again until a launch picks d2x's for that IOS.
    g_active = false;
    // The reload ends d2x's FAT module and its NAND emulation too.
    g_held_by_fat = false;
}

const std::string& d2x_sd_last_error() { return g_last_error; }

std::int32_t d2x_sd_fd() { return g_fd; }

bool d2x_sd_read(std::uint32_t sector, std::uint32_t count, void* buffer) {
    if (g_held_by_fat) return false;
    return chunked(kRead, sector, count, static_cast<u8*>(buffer));
}

bool d2x_sd_write(std::uint32_t sector, std::uint32_t count, const void* buffer) {
    if (g_held_by_fat) return false;
    return chunked(kWrite, sector, count, static_cast<u8*>(const_cast<void*>(buffer)));
}

void use_d2x_sd(bool on) { g_active = on; }
bool using_d2x_sd() { return g_active; }
const DISC_INTERFACE* sd_interface() {
    if (g_held_by_fat) return &g_refused;
    return g_active ? &Serial<&g_io>::io : &Serial<&__io_wiisd>::io;
}

void hold_sd_for_d2x_fat(bool held) { g_held_by_fat = held; }
bool sd_held_by_d2x_fat() { return g_held_by_fat; }

}  // namespace riftwii::wii
