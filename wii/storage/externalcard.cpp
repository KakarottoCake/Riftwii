// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "externalcard.hpp"

#include "riftwii/imagevolume.hpp"
#include "usbcatalog.hpp"

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
        std::vector<std::string> folders{"sd:/saves"};
        for (const std::string& root : usb_volume_roots()) folders.push_back(root + "saves");
        const GcNintendontFiles found = gc_nintendont_scan(folders, ListImportCards);
        return gc_nintendont_card_for_game(found.cards, game_id);
    }
    if (source == "dolphin") {
        const auto found = gc_dolphin_scan("sd:/riftwii/gc/dolphin", gc_dolphin_regions(game_id, false));
        return gc_dolphin_card_for_game(found);
    }
    return {};
}

}  // namespace riftwii::wii
