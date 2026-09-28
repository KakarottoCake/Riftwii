// SPDX-License-Identifier: GPL-3.0-or-later
#include "umsdev.hpp"

#include <gccore.h>
#include <ogc/ipc.h>

#include <algorithm>
#include <cstring>

#include "log.hpp"
#include "skin.hpp"
#include "usbcatalog.hpp"

namespace riftwii::wii::ums {
namespace {

// d2x's EHCI module ioctls ('U' 'M' 'S' base).
constexpr s32 kUmsBase = ('U' << 24) | ('M' << 16) | ('S' << 8);
constexpr s32 kInit = kUmsBase + 0x1;
constexpr s32 kGetCapacity = kUmsBase + 0x2;
constexpr s32 kReadSectors = kUmsBase + 0x3;
constexpr std::uint32_t kFatSectorBytes = 512;  // packs and RVZ games
constexpr std::uint32_t kBounceBytes = 32 * 1024;  // per request, as d2x reads

s32 g_fd = -1;
std::uint32_t g_sector_bytes = kFatSectorBytes;
// A failed open is not retried under the same IOS; its reason is kept
// for the later callers. An IOS reload clears both (Forget).
bool g_failed = false;
std::string g_failure;
std::uint8_t* g_bounce = nullptr;
std::uint32_t g_args[8] ATTRIBUTE_ALIGN(32);
ioctlv g_vec[3] ATTRIBUTE_ALIGN(32);
std::unique_ptr<ImageVolume> g_volume;

bool ReadBlocks(std::uint64_t lba, std::uint32_t count, std::uint8_t* out) { return Read(lba, count, out); }

}  // namespace

bool Open(std::string& error) {
    if (g_fd >= 0) return true;
    if (g_failed) {
        error = g_failure;
        return false;
    }
    static char path[] ATTRIBUTE_ALIGN(32) = "/dev/usb2";
    g_fd = IOS_Open(path, 0);
    if (g_fd < 0) {
        // libogc's driver is still up: a failure here leaves the menu's
        // USB drive mounted.
        error = "this IOS (" + std::to_string(IOS_GetVersion()) + ") has no d2x USB device (" +
                std::to_string(g_fd) + "); packs and RVZ games on the USB drive need a d2x cIOS";
        g_fd = -1;
        g_failed = true;
        g_failure = error;
        return false;
    }
    // One driver for the drive: libogc's lets go before d2x's starts.
    release_usb_driver();
    const s32 init = IOS_Ioctlv(g_fd, kInit, 0, 0, nullptr);
    g_vec[0].data = &g_args[0];
    g_vec[0].len = 4;
    DCFlushRange(g_args, sizeof(g_args));
    const s32 sectors = IOS_Ioctlv(g_fd, kGetCapacity, 0, 1, g_vec);
    DCInvalidateRange(g_args, sizeof(g_args));
    const std::uint32_t sector_bytes = g_args[0];
    logf("USB (d2x): fd %d, init %d, %d sectors of %u bytes\n", static_cast<int>(g_fd), static_cast<int>(init),
         static_cast<int>(sectors), static_cast<unsigned>(sector_bytes));
    // 512 for a hard drive or stick, 2048 for a DVD drive.
    const bool size_ok = sector_bytes >= kFatSectorBytes && sector_bytes <= kBounceBytes &&
                         (sector_bytes & (sector_bytes - 1)) == 0;
    if (init < 0 || sectors <= 0 || !size_ok) {
        error = init < 0 || sectors <= 0 ? "the USB drive did not start through d2x (" + std::to_string(init) + ")"
                                         : "the USB drive has " + std::to_string(sector_bytes) + "-byte sectors";
        IOS_Close(g_fd);
        g_fd = -1;
        g_failed = true;
        g_failure = error;
        return false;
    }
    g_sector_bytes = sector_bytes;
    if (!g_bounce) g_bounce = skin::Mem2Alloc(kBounceBytes);
    if (!g_bounce) {
        error = "no MEM2 left for the USB buffer";
        IOS_Close(g_fd);
        g_fd = -1;
        return false;
    }
    return true;
}

void Forget() {
    if (g_fd >= 0) IOS_Close(g_fd);
    g_fd = -1;
    g_failed = false;
    g_failure.clear();
    g_volume.reset();
}

int Fd() { return g_fd; }

std::uint32_t SectorBytes() { return g_sector_bytes; }

bool Read(std::uint64_t sector, std::uint32_t count, std::uint8_t* out) {
    if (g_fd < 0 || !g_bounce || sector > 0xFFFFFFFFull) return false;
    while (count > 0) {
        const std::uint32_t n = std::min(count, kBounceBytes / g_sector_bytes);
        g_args[0] = static_cast<std::uint32_t>(sector);
        g_args[1] = n;
        g_vec[0].data = &g_args[0];
        g_vec[0].len = 4;
        g_vec[1].data = &g_args[1];
        g_vec[1].len = 4;
        g_vec[2].data = g_bounce;
        g_vec[2].len = n * g_sector_bytes;
        DCFlushRange(g_args, sizeof(g_args));
        DCInvalidateRange(g_bounce, n * g_sector_bytes);
        if (IOS_Ioctlv(g_fd, kReadSectors, 2, 1, g_vec) < 0) return false;
        DCInvalidateRange(g_bounce, n * g_sector_bytes);
        std::memcpy(out, g_bounce, n * g_sector_bytes);
        out += n * g_sector_bytes;
        sector += n;
        count -= n;
    }
    return true;
}

bool Volume(const ImageVolume*& out, std::string& error) {
    if (!Open(error)) return false;
    if (g_sector_bytes != kFatSectorBytes) {
        error = "the USB drive has " + std::to_string(g_sector_bytes) + "-byte sectors; packs on it need 512";
        return false;
    }
    if (!g_volume && !mount_image_volume(&ReadBlocks, g_volume, error)) {
        error = "the USB drive has no FAT32 or NTFS volume: " + error;
        g_volume.reset();
        return false;
    }
    out = g_volume.get();
    return true;
}

bool ReadText(const std::string& usb_path, std::string& out, std::string& error) {
    if (usb_path.compare(0, 5, "usb:/") != 0) {
        error = "'" + usb_path + "' is not a usb:/ path";
        return false;
    }
    const ImageVolume* volume = nullptr;
    if (!Volume(volume, error)) return false;
    VolumeFile file;
    if (!volume->lookup(usb_path.substr(4), file, error)) return false;
    if (file.entry.is_directory || file.entry.size > (1u << 20)) {
        error = "'" + usb_path + "' is not an XML file of 1 MiB or less";
        return false;
    }
    out.assign(file.entry.size, '\0');
    if (!volume->read(file, 0, reinterpret_cast<std::uint8_t*>(&out[0]), out.size())) {
        error = "cannot read '" + usb_path + "' from the USB drive";
        return false;
    }
    return true;
}

}  // namespace riftwii::wii::ums
