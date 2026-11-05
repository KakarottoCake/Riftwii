// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/loaderfiles.hpp"

namespace riftwii {

bool loader_file_id(const std::string& id) {
    if (id.size() != 4 && id.size() != 6) return false;
    for (const char c : id) {
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

std::vector<std::string> loader_cover_paths(const std::string& id) {
    if (!loader_file_id(id)) return {};
    // Folder names only, from USB Loader GX's public configuration defaults:
    // https://github.com/wiidev/usbloadergx/blob/master/source/settings/CSettings.cpp
    // WiiFlow documents flat covers and full boxes here:
    // https://sites.google.com/view/wiiflow-wiki/plugins-setup
    // Flat/front images come before full boxes and disc pictures.
    std::vector<std::string> paths;
    for (const char* folder : {"/images/2D/", "/config/images/2D/", "/images/", "/config/images/",
                               "/images/3D/", "/config/images/3D/", "/wiiflow/covers/",
                               "/images/full/", "/config/images/full/", "/wiiflow/boxcovers/",
                               "/images/disc/", "/config/images/disc/"}) {
        paths.push_back(std::string(folder) + id + ".png");
    }
    return paths;
}

std::vector<std::string> loader_cheat_paths(const std::string& id) {
    if (!loader_file_id(id)) return {};
    return {"/codes/" + id + ".txt", "/txtcodes/" + id + ".txt"};
}

}  // namespace riftwii
