// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "vsdimage.hpp"

#include <dirent.h>
#include <fat.h>
#include <gccore.h>
#include <strings.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include "log.hpp"
#include "usbcatalog.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kSdFolder = "sd:/riftwii/";
constexpr const char* kUsbFolder = "usb:/riftwii/";
constexpr std::uint32_t kSectorBytes = 512;
constexpr std::size_t kMaxImages = 32;

// The image being read: through stdio on the SD card, or through the
// menu's USB view.
std::string g_name;
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
    g_name.clear();
    g_location.clear();
    g_size = 0;
}

// "pm.raw": a plain file name ending in .raw, not hidden (a Mac's ._pm.raw).
bool image_name(const std::string& name) {
    return name.size() > 4 && name.size() <= 64 && name[0] != '.' && name.find_first_of("/\\:") == std::string::npos &&
           strcasecmp(name.c_str() + name.size() - 4, ".raw") == 0;
}

bool sd_file(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode);
}

bool usb_file(const std::string& path) {
    std::uint64_t size = 0;
    std::uint8_t ignored;
    return usb_volume_ready() && read_usb_range(path, 0, &ignored, 0, size);
}

}  // namespace

std::vector<VsdImageFile> ListVsdImages() {
    std::vector<VsdImageFile> out;
    const auto has = [&](const std::string& name) {
        for (const VsdImageFile& f : out)
            if (strcasecmp(f.name.c_str(), name.c_str()) == 0) return true;
        return false;
    };
    if (DIR* d = opendir("sd:/riftwii")) {
        while (dirent* e = readdir(d)) {
            const std::string name = e->d_name;
            if (out.size() < kMaxImages && image_name(name) && sd_file(kSdFolder + name)) out.push_back({name, kSdFolder + name});
        }
        closedir(d);
    }
    if (usb_volume_ready()) {
        for (const std::string& name : usb_file_names("/riftwii", ".raw")) {
            if (out.size() < kMaxImages && image_name(name) && !has(name)) out.push_back({name, kUsbFolder + name});
        }
    }
    std::sort(out.begin(), out.end(), [](const VsdImageFile& a, const VsdImageFile& b) {
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return out;
}

std::string VsdImageLocation(const std::string& name) {
    if (!image_name(name)) return "";
    if (sd_file(kSdFolder + name)) return kSdFolder + name;
    if (usb_file(kUsbFolder + name)) return kUsbFolder + name;
    return "";
}

std::string VsdImageOfKey(const std::string& key) {
    const std::size_t slash = key.find('/');
    if (slash == std::string::npos) return "";
    const std::string top = key.substr(0, slash);
    return image_name(top) ? top : "";
}

VsdMount::VsdMount(const std::string& name) {
    if (g_mounts > 0) {
        if (strcasecmp(g_name.c_str(), name.c_str()) != 0) return;  // another image is mounted
        ++g_mounts;
        ok_ = true;
        return;
    }
    const std::string where = VsdImageLocation(name);
    if (where.empty()) return;
    g_name = name;
    g_location = where;
    if (where.compare(0, 4, "sd:/") == 0) {
        g_file = std::fopen(where.c_str(), "rb");
        struct stat st;
        if (!g_file || stat(where.c_str(), &st) != 0) {
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
