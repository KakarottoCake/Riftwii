// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/hidimport.hpp"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "riftwii/hidconfig.hpp"

namespace riftwii {
namespace {

// An inaccessible path counts as occupied: never truncate something just
// because the existence check could not read it.
bool occupied(const std::string& path) {
    struct stat info{};
    return stat(path.c_str(), &info) == 0 || errno != ENOENT;
}

}  // namespace

bool hid_filename_id(std::string_view name, std::uint32_t& id) {
    id = 0;
    if (name.size() != 13 || name[4] != '_' || name[9] != '.' ||
        (name[10] != 'i' && name[10] != 'I') ||
        (name[11] != 'n' && name[11] != 'N') ||
        (name[12] != 'i' && name[12] != 'I')) return false;

    std::uint32_t parsed = 0;
    for (unsigned i = 0; i < 9; ++i) {
        if (i == 4) continue;
        const char c = name[i];
        unsigned digit = 16;
        if (c >= '0' && c <= '9') digit = c - '0';
        if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        if (digit == 16) return false;
        parsed = (parsed << 4) | digit;
    }
    id = parsed;
    return true;
}

std::string hid_layout_filename(std::uint32_t id) {
    char name[14];
    std::snprintf(name, sizeof(name), "%04X_%04X.ini",
                  static_cast<unsigned>(id >> 16), static_cast<unsigned>(id & 65535u));
    return name;
}

bool validate_hid_import(std::string_view text, std::string_view filename,
                         std::uint32_t& id, std::string& error) {
    id = 0;
    rthid_mapping mapping{};
    if (!parse_hid_config(text, mapping, error)) return false;
    const std::uint32_t parsed = (static_cast<std::uint32_t>(mapping.vid) << 16) | mapping.pid;
    if (filename != "controller.ini") {
        std::uint32_t named = 0;
        if (!hid_filename_id(filename, named) || named != parsed) {
            error = "The device IDs in the file do not match its filename.";
            return false;
        }
    }
    id = parsed;
    return true;
}

HidImportSave save_hid_import(const std::string& folder, std::string_view text, bool replace,
                             std::string& destination, std::string& error) {
    destination.clear();
    std::uint32_t id = 0;
    if (!validate_hid_import(text, "controller.ini", id, error)) return HidImportSave::Failed;
    destination = folder + "/" + hid_layout_filename(id);
    // LoadConfig also accepts an all-lowercase filename on development
    // cards. Keep that exact path when replacing one of those files.
    if (!occupied(destination)) {
        char lower[14];
        std::snprintf(lower, sizeof(lower), "%04x_%04x.ini",
                      static_cast<unsigned>(id >> 16), static_cast<unsigned>(id & 65535u));
        const std::string lowercase = folder + "/" + lower;
        if (occupied(lowercase)) destination = lowercase;
    }
    const bool existed = occupied(destination);
    if (existed && !replace) return HidImportSave::Exists;

    const std::string temporary = destination + ".import-new";
    const std::string backup = destination + ".import-old";
    error = "The layout could not be saved. Check the SD card's free space and lock switch.";
    // Leftovers are kept for recovery, never overwritten by a later import.
    if (occupied(backup)) return HidImportSave::Failed;
    const int descriptor = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (descriptor < 0) return HidImportSave::Failed;
    FILE* file = fdopen(descriptor, "wb");
    if (!file) {
        close(descriptor);
        std::remove(temporary.c_str());
        return HidImportSave::Failed;
    }
    const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed) {
        std::remove(temporary.c_str());
        return HidImportSave::Failed;
    }

    // Recheck after writing as well. All menu imports run serially.
    if (!replace && occupied(destination)) {
        std::remove(temporary.c_str());
        error.clear();
        return HidImportSave::Exists;
    }
    if (existed) {
        struct stat info{};
        if (stat(destination.c_str(), &info) != 0 || !S_ISREG(info.st_mode) ||
            std::rename(destination.c_str(), backup.c_str()) != 0) {
            std::remove(temporary.c_str());
            return HidImportSave::Failed;
        }
    }
    if (std::rename(temporary.c_str(), destination.c_str()) != 0) {
        std::remove(temporary.c_str());
        if (existed && std::rename(backup.c_str(), destination.c_str()) != 0) {
            error = "The layout could not be saved or restored. The old copy is at " + backup + ".";
        }
        return HidImportSave::Failed;
    }
    if (existed) std::remove(backup.c_str());
    error.clear();
    return HidImportSave::Saved;
}

}  // namespace riftwii
