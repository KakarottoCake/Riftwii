// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "vsdimage.hpp"

#include <fat.h>
#include <gccore.h>
#include <sys/stat.h>

#include <cstdint>
#include <cstdio>

#include "log.hpp"
#include "usbcatalog.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kSdImage = "sd:/riftwii/sd.raw";
constexpr const char* kUsbImage = "usb:/riftwii/sd.raw";
constexpr std::uint32_t kSectorBytes = 512;

// The image being read: through stdio on the SD card, or through the
// menu's USB view.
std::string g_location;
FILE* g_file = nullptr;
std::uint64_t g_size = 0;
int g_mounts = 0;

bool io_startup() { return true; }
bool io_inserted() { return !g_location.empty(); }
bool io_read(sec_t sector, sec_t count, void* buffer) {
    const std::uint64_t offset = static_cast<std::uint64_t>(sector) * kSectorBytes;
    const std::size_t bytes = static_cast<std::size_t>(count) * kSectorBytes;
    if (offset > g_size || bytes > g_size - offset) return false;
    if (g_file) {
        return fseeko(g_file, static_cast<off_t>(offset), SEEK_SET) == 0 &&
               std::fread(buffer, 1, bytes, g_file) == bytes;
    }
    std::uint64_t size = 0;
    return read_usb_range(g_location, offset, static_cast<std::uint8_t*>(buffer), bytes, size);
}
bool io_write(sec_t, sec_t, const void*) { return false; }  // read only
bool io_clear() { return true; }
bool io_shutdown() { return true; }

const DISC_INTERFACE g_io = {
    0x56534400,  // "VSD"
    FEATURE_MEDIUM_CANREAD,
    io_startup,
    io_inserted,
    io_read,
    io_write,
    io_clear,
    io_shutdown,
};

void close_image() {
    if (g_file) std::fclose(g_file);
    g_file = nullptr;
    g_location.clear();
    g_size = 0;
}

}  // namespace

std::string VsdImageLocation() {
    struct stat st;
    if (stat(kSdImage, &st) == 0 && !S_ISDIR(st.st_mode)) return kSdImage;
    std::uint64_t size = 0;
    std::uint8_t ignored;
    if (usb_volume_ready() && read_usb_range(kUsbImage, 0, &ignored, 0, size)) return kUsbImage;
    return "";
}

VsdMount::VsdMount() {
    if (g_mounts > 0) {
        ++g_mounts;
        ok_ = true;
        return;
    }
    const std::string where = VsdImageLocation();
    if (where.empty()) return;
    g_location = where;
    if (where == kSdImage) {
        g_file = std::fopen(kSdImage, "rb");
        struct stat st;
        if (!g_file || stat(kSdImage, &st) != 0) {
            close_image();
            return;
        }
        g_size = static_cast<std::uint64_t>(st.st_size);
    } else {
        std::uint8_t ignored;
        if (!read_usb_range(where, 0, &ignored, 0, g_size)) {
            close_image();
            return;
        }
    }
    if (!fatMount("vsd", &g_io, 0, 8, 16)) {
        logf("Virtual SD card: %s has no FAT volume RiftWii can read\n", where.c_str());
        close_image();
        return;
    }
    ++g_mounts;
    ok_ = true;
}

VsdMount::~VsdMount() {
    if (!ok_ || --g_mounts > 0) return;
    fatUnmount("vsd:");
    close_image();
}

}  // namespace riftwii::wii
