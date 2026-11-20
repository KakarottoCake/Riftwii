// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nintendontimport.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>

#include "riftwii/checkedalloc.hpp"
#include "riftwii/hidimport.hpp"
#include "usbcatalog.hpp"

namespace riftwii::wii {
namespace {

constexpr unsigned kDirectoryVisitLimit = 512;

bool IsUsb(const std::string& path) {
    return path.compare(0, 5, "usb:/") == 0;
}

bool IsFile(const std::string& path) {
    if (IsUsb(path)) {
        std::uint64_t size = 0;
        return read_usb_range(path, 0, nullptr, 0, size);
    }
    struct stat info{};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

bool MakeDirectory(const char* path) {
    if (mkdir(path, 0777) == 0) return true;
    if (errno != EEXIST) return false;
    struct stat info{};
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

}  // namespace

bool FindNintendontLayouts(std::vector<std::string>& paths, bool& limited) {
    paths.clear();
    limited = false;
    if (!try_reserve(paths, kHidImportMaxFiles)) return false;
    for (const char* path : kHidImportFiles) {
        if (IsFile(path)) paths.emplace_back(path);
    }
    const auto add = [&](const char* folder, const std::string& name) {
        std::uint32_t id = 0;
        if (!hid_filename_id(name, id)) return;
        if (paths.size() == kHidImportMaxFiles) {
            limited = true;
            return;
        }
        const std::string path = std::string(folder) + "/" + name;
        if (IsFile(path)) paths.push_back(path);
    };
    for (const char* folder : kHidImportFolders) {
        if (IsUsb(folder)) {
            // The menu's reader also handles NTFS, which is not mounted
            // as a stdio drive. Its format reader enforces directory limits.
            std::vector<VolumeEntry> entries;
            if (!list_usb_folder(folder, entries)) continue;
            const std::size_t count = std::min<std::size_t>(entries.size(), kDirectoryVisitLimit);
            if (entries.size() > count) limited = true;
            for (std::size_t i = 0; i < count; ++i) {
                if (!entries[i].is_directory) add(folder, entries[i].name);
            }
        } else {
            DIR* directory = opendir(folder);
            if (!directory) continue;
            unsigned visited = 0;
            while (const dirent* entry = readdir(directory)) {
                if (visited++ == kDirectoryVisitLimit) {
                    limited = true;
                    break;
                }
                add(folder, entry->d_name);
            }
            closedir(directory);
        }
    }
    std::sort(paths.begin(), paths.end());
    return true;
}

NintendontRead ReadNintendontLayout(const std::string& path, NintendontLayout& layout, std::string& detail) {
    layout.bytes = 0;
    detail.clear();
    if (IsUsb(path)) {
        std::uint64_t size = 0;
        if (!read_usb_range(path, 0, nullptr, 0, size)) return NintendontRead::Unreadable;
        if (size > kHidConfigMaxBytes) return NintendontRead::TooLarge;
        const std::size_t bytes = static_cast<std::size_t>(size);
        if (!read_usb_range(path, 0, reinterpret_cast<std::uint8_t*>(layout.text.data()), bytes, size) ||
            size != bytes) return NintendontRead::Unreadable;
        layout.bytes = bytes;
    } else {
        FILE* file = std::fopen(path.c_str(), "rb");
        if (!file) return NintendontRead::Unreadable;
        layout.bytes = std::fread(layout.text.data(), 1, layout.text.size(), file);
        const bool read_error = std::ferror(file) != 0;
        std::fclose(file);
        if (read_error) return NintendontRead::Unreadable;
        if (layout.bytes > kHidConfigMaxBytes) return NintendontRead::TooLarge;
    }
    const std::string_view text(layout.text.data(), layout.bytes);
    rthid_mapping mapping{};
    if (!parse_hid_config(text, mapping, detail)) return NintendontRead::Invalid;
    const std::string filename = path.substr(path.find_last_of('/') + 1);
    std::uint32_t id = 0;
    if (!validate_hid_import(text, filename, id, detail)) return NintendontRead::WrongDevice;
    return NintendontRead::Ready;
}

bool PrepareControllerFolder() {
    return MakeDirectory("sd:/riftwii") && MakeDirectory("sd:/riftwii/controllers");
}

}  // namespace riftwii::wii
