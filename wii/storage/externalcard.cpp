// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "externalcard.hpp"

#include "riftwii/imagevolume.hpp"
#include "riftwii/gccard.hpp"
#include "usbcatalog.hpp"

#include <sys/stat.h>

namespace riftwii::wii {

bool ListImportCards(const std::string& folder, std::vector<GcFolderEntry>& entries) {
    if (!is_usb_path(folder)) return gc_posix_lister(folder, entries);
    entries.clear();
    std::vector<VolumeEntry> listed;
    if (!list_usb_folder(folder, listed)) return false;
    for (const VolumeEntry& entry : listed) {
        if (entries.size() >= kGcDolphinMaxEntries) break;
        entries.push_back({entry.name, entry.is_directory, entry.size});
    }
    return true;
}

std::string ExternalCardPath(const std::string& game_id, const std::string& source) {
    if (source == "nintendont") {
        // Nintendont's /saves and Better-Nintendont's /apps/gc_devo, on the SD
        // card and every USB partition. This game's own card comes before a
        // shared one, wherever either is.
        std::vector<std::string> roots{"sd:/"};
        for (const std::string& root : usb_volume_roots()) roots.push_back(root);
        const std::vector<std::string> candidates = gc_nintendont_card_candidates(game_id, roots);
        for (const std::string& path : candidates) {
            std::uint64_t bytes = 0;
            struct stat info {};
            const bool exists = is_usb_path(path)
                ? read_usb_range(path, 0, nullptr, 0, bytes)
                : stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) &&
                  (bytes = static_cast<std::uint64_t>(info.st_size)) != 0;
            if (exists && bytes >= kGcNintendontMinBytes && bytes <= kGcDolphinMaxRawBytes &&
                bytes % kGcCardBlockBytes == 0) return path;
        }
        return {};
    }
    if (source == "dolphin") {
        const auto found = gc_dolphin_scan("sd:/riftwii/gc/dolphin", gc_dolphin_regions(game_id, false));
        return gc_dolphin_card_for_game(found);
    }
    return {};
}

}  // namespace riftwii::wii
