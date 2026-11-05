// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamefiles.hpp"

#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "log.hpp"
#include "sdfile.hpp"
#include "riftwii/checkedalloc.hpp"

namespace riftwii::wii {
namespace {

// FAT has no links. Refuse unexpectedly deep or huge save trees rather
// than letting a damaged directory make deletion unbounded.
bool RemoveOwnPath(const std::string& path, unsigned depth = 0) {
    struct stat status;
    if (stat(path.c_str(), &status) != 0) return errno == ENOENT;
    if (!S_ISDIR(status.st_mode)) return std::remove(path.c_str()) == 0;
    if (depth >= 16) return false;
    DIR* directory = opendir(path.c_str());
    if (!directory) return false;
    bool ok = true;
    unsigned count = 0;
    while (dirent* entry = readdir(directory)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        if (++count > 4096 || name.find_first_of("/\\:") != std::string::npos || path.size() + name.size() > 1022) {
            ok = false;
            break;
        }
        if (!RemoveOwnPath(path + "/" + name, depth + 1)) {
            ok = false;
            break;
        }
    }
    closedir(directory);
    return ok && rmdir(path.c_str()) == 0;
}

}  // namespace

bool PlanGameDeletion(const ImageGame& game, GameDeletion& plan, std::string& error) {
    if (game.wbfs_slot >= 0 || !game.gc_patch_source.empty()) {
        error = "This game is not stored as an image file.";
        return false;
    }
    const std::size_t slash = game.path.find_last_of('/');
    if (slash == std::string::npos) return false;
    std::vector<std::string> siblings;
    if (!try_reserve(siblings, 4096)) return false;
    if (is_usb_path(game.path)) {
        std::vector<VolumeEntry> entries;
        if (!list_usb_folder(game.path.substr(0, slash), entries)) return false;
        for (const VolumeEntry& entry : entries) {
            if (entries.size() > 4096) return false;
            if (!entry.is_directory) siblings.push_back(entry.name);
        }
    } else {
        DIR* directory = opendir(game.path.substr(0, slash).c_str());
        if (!directory) return false;
        while (dirent* entry = readdir(directory)) {
            if (siblings.size() >= 4096) {
                closedir(directory);
                return false;
            }
            const std::string name = entry->d_name;
            struct stat status;
            const std::string path = game.path.substr(0, slash + 1) + name;
            if (stat(path.c_str(), &status) == 0 && S_ISREG(status.st_mode)) siblings.push_back(name);
        }
        closedir(directory);
    }
    if (game_deletion_files(game.path, siblings, plan)) return true;
    error = "The game's image paths could not be checked.";
    return false;
}

bool DeleteGameFiles(const ImageGame& game, const GameDeletion& plan, bool saves, std::string& error) {
    if (is_usb_path(game.path)) {
        if (!delete_usb_image_files(plan.images, plan.empty_folder, error)) return false;
    } else {
        for (const std::string& path : plan.images) {
            if (std::remove(path.c_str()) != 0) {
                error = "Could not delete " + path;
                return false;
            }
        }
        if (!plan.empty_folder.empty()) rmdir(plan.empty_folder.c_str());
        forget_sd_layout();
    }
    for (const std::string& path : game_deletion_extras(game.id, saves)) {
        if (!RemoveOwnPath(path)) {
            error = "Could not delete " + path;
        }
    }
    for (const std::string& root : usb_volume_roots()) {
        std::vector<std::string> covers;
        for (const std::string& own : game_deletion_extras(game.id, false)) {
            const std::string path = root + own.substr(4);
            std::uint64_t bytes = 0;
            if (read_usb_range(path, 0, nullptr, 0, bytes)) covers.push_back(path);
        }
        if (!covers.empty()) delete_usb_image_files(covers, "", error);
    }
    logf("Deleted the image files for %s. The player chose to %s RiftWii's separate saves.\n", game.id.c_str(), saves ? "delete" : "keep");
    return true;  // image files are gone; an extras error is still reported
}

}  // namespace riftwii::wii
