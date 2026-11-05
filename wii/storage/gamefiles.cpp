// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamefiles.hpp"

#include <algorithm>
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
bool RemoveOwnPath(const std::string& path, std::vector<std::string>& remaining, unsigned depth = 0) {
    const auto failed = [&] {
        try_push_back(remaining, path);
        logf("Could not delete %s. Some of the game's files may already be gone.\n", path.c_str());
        return false;
    };
    struct stat status;
    if (stat(path.c_str(), &status) != 0) {
        if (errno == ENOENT) return true;
        return failed();
    }
    if (!S_ISDIR(status.st_mode)) {
        if (std::remove(path.c_str()) == 0 || errno == ENOENT) return true;
        return failed();
    }
    if (depth >= 16) return failed();
    DIR* directory = opendir(path.c_str());
    if (!directory) return failed();
    bool ok = true;
    unsigned count = 0;
    while (dirent* entry = readdir(directory)) {
        const std::string name = entry->d_name;
        if (++count > 4096 || name.find_first_of("/\\:") != std::string::npos || path.size() + name.size() > 1022) {
            failed();
            ok = false;
            break;
        }
        if (name == "." || name == "..") continue;
        if (!RemoveOwnPath(path + "/" + name, remaining, depth + 1)) {
            ok = false;
            break;
        }
    }
    closedir(directory);
    if (!ok) return false;
    if (rmdir(path.c_str()) == 0 || errno == ENOENT) return true;
    return failed();
}

}  // namespace

bool HasOtherGameCopy(const ImageGame& game, const GameDeletion& plan, const ImageCatalog& catalog) {
    return std::any_of(catalog.games.begin(), catalog.games.end(), [&](const ImageGame& candidate) {
        return candidate.id == game.id &&
            std::find(plan.images.begin(), plan.images.end(), candidate.path) == plan.images.end();
    });
}

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
        unsigned entries_seen = 0;
        while (dirent* entry = readdir(directory)) {
            if (++entries_seen > 4096) {
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
    if (game_deletion_files(game.path, game.gamecube, siblings, plan)) return true;
    error = "The game's image paths could not be checked.";
    return false;
}

bool DeleteGameFiles(const ImageGame& game, const GameDeletion& plan, bool saves,
                     std::vector<std::string>& remaining, std::string& error) {
    remaining.clear();
    error.clear();
    if (is_usb_path(game.path)) {
        if (!delete_usb_image_files(plan.images, plan.empty_folder, error, &remaining)) return false;
    } else {
        for (const std::string& path : plan.images) {
            if (std::remove(path.c_str()) != 0 && errno != ENOENT) {
                try_push_back(remaining, path);
                error = "Could not delete " + path;
                return false;
            }
        }
        if (!plan.empty_folder.empty()) rmdir(plan.empty_folder.c_str());
        forget_sd_layout();
    }
    for (const std::string& path : game_deletion_extras(game.id, saves)) {
        if (!RemoveOwnPath(path, remaining)) {
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
        if (!covers.empty()) {
            std::string usb_error;
            if (!delete_usb_image_files(covers, "", usb_error, &remaining)) {
                error = usb_error;
                logf("Could not finish deleting the game's USB covers and cheats: %s\n", usb_error.c_str());
            }
        }
    }
    if (!remaining.empty() || !error.empty()) return false;
    logf("Deleted the image files for %s. The player chose to %s RiftWii's separate saves.\n", game.id.c_str(), saves ? "delete" : "keep");
    return true;
}

}  // namespace riftwii::wii
