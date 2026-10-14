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
#include "riftwii/vsdparts.hpp"
#include "usbcatalog.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kSdFolder = "sd:/riftwii/";
constexpr const char* kUsbFolder = "usb:/riftwii/";
constexpr std::uint32_t kSectorBytes = 512;
constexpr std::size_t kMaxImages = 32;

// One file of the image being read (the image itself, or one of its
// parts): through stdio on the SD card, or through the menu's USB view.
struct Part {
    std::string path;
    std::uint64_t size = 0;
    FILE* file = nullptr;
};
std::string g_name;
std::vector<Part> g_parts;
std::uint64_t g_size = 0;
int g_mounts = 0;

bool io_startup() { return true; }
bool io_inserted() { return !g_parts.empty(); }
bool io_read(sec_t sector, sec_t count, void* buffer) {
    std::uint64_t offset = static_cast<std::uint64_t>(sector) * kSectorBytes;
    std::size_t bytes = static_cast<std::size_t>(count) * kSectorBytes;
    if (offset > g_size || bytes > g_size - offset) return false;
    auto* out = static_cast<std::uint8_t*>(buffer);
    // A read may run from one part into the next.
    for (const Part& part : g_parts) {
        if (bytes == 0) break;
        if (offset >= part.size) {
            offset -= part.size;
            continue;
        }
        const std::size_t here = static_cast<std::size_t>(std::min<std::uint64_t>(bytes, part.size - offset));
        std::uint64_t size = 0;
        const bool ok = part.file ? fseeko(part.file, static_cast<off_t>(offset), SEEK_SET) == 0 &&
                                        std::fread(out, 1, here, part.file) == here
                                  : read_usb_range(part.path, offset, out, here, size);
        if (!ok) return false;
        out += here;
        bytes -= here;
        offset = 0;
    }
    return bytes == 0;
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
    for (Part& part : g_parts)
        if (part.file) std::fclose(part.file);
    g_parts.clear();
    g_name.clear();
    g_size = 0;
}

// "pm.raw": a plain file name ending in .raw, not hidden (a Mac's ._pm.raw).
bool image_name(const std::string& name) {
    return name.size() > 4 && name.size() <= 64 && name[0] != '.' && name.find_first_of("/\\:") == std::string::npos &&
           strcasecmp(name.c_str() + name.size() - 4, ".raw") == 0;
}

bool sd_file(const std::string& path, std::uint64_t& size) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) return false;
    size = static_cast<std::uint64_t>(st.st_size);
    return true;
}
bool sd_file(const std::string& path) {
    std::uint64_t size;
    return sd_file(path, size);
}

bool usb_file(const std::string& path, std::uint64_t& size) {
    std::uint8_t ignored;
    return usb_volume_ready() && read_usb_range(path, 0, &ignored, 0, size);
}

// The files image `name` is in `folder` ("sd:/riftwii/"): the file itself,
// else its parts .001, .002... up to the first one missing. Empty when
// there is neither.
std::vector<Part> parts_in(const std::string& folder, const std::string& name) {
    const bool usb = folder.compare(0, 4, "usb:") == 0;
    const auto found = [&](const std::string& path, std::uint64_t& size) {
        return usb ? usb_file(path, size) : sd_file(path, size);
    };
    std::vector<Part> parts;
    std::uint64_t size = 0;
    if (found(folder + name, size)) {
        parts.push_back({folder + name, size, nullptr});
        return parts;
    }
    for (unsigned n = 1; n <= kMaxVsdParts; ++n) {
        const std::string path = folder + vsd_part_name(name, n);
        if (!found(path, size)) break;
        parts.push_back({path, size, nullptr});
    }
    return parts;
}

}  // namespace

std::vector<VsdImageFile> ListVsdImages() {
    std::vector<VsdImageFile> out;
    const auto has = [&](const std::string& name) {
        for (const VsdImageFile& f : out)
            if (strcasecmp(f.name.c_str(), name.c_str()) == 0) return true;
        return false;
    };
    // "rex.raw", or "rex.raw" for its first part "rex.raw.001".
    const auto add = [&](const std::string& file, const char* folder) {
        const std::string split = vsd_split_image(file);
        const std::string name = split.empty() ? file : split;
        if (out.size() < kMaxImages && image_name(name) && !has(name)) out.push_back({name, folder + name});
    };
    if (DIR* d = opendir("sd:/riftwii")) {
        while (dirent* e = readdir(d)) {
            const std::string file = e->d_name;
            if (sd_file(kSdFolder + file)) add(file, kSdFolder);
        }
        closedir(d);
    }
    if (usb_volume_ready()) {
        for (const char* ext : {".raw", ".001"})
            for (const std::string& file : usb_file_names("/riftwii", ext)) add(file, kUsbFolder);
    }
    std::sort(out.begin(), out.end(), [](const VsdImageFile& a, const VsdImageFile& b) {
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return out;
}

std::string VsdImageLocation(const std::string& name) {
    if (!image_name(name)) return "";
    if (!parts_in(kSdFolder, name).empty()) return kSdFolder + name;
    if (usb_volume_ready() && !parts_in(kUsbFolder, name).empty()) return kUsbFolder + name;
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
    g_parts = parts_in(where.compare(0, 4, "sd:/") == 0 ? kSdFolder : kUsbFolder, name);
    for (Part& part : g_parts) {
        if (part.path.compare(0, 4, "sd:/") == 0 && !(part.file = std::fopen(part.path.c_str(), "rb"))) {
            close_image();
            return;
        }
        g_size += part.size;
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
