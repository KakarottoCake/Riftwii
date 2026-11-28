// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "playtimemark.hpp"

#include <sys/stat.h>

#include <cstdio>
#include <ctime>

#include "loadersettings.hpp"
#include "textfile.hpp"
#include "riftwii/playtime.hpp"

namespace riftwii::wii {
namespace {

void write_marker(const PlayMarker& marker) {
    mkdir("sd:/riftwii", 0777);
    if (FILE* f = std::fopen(kPlayMarkerPath, "wb")) {
        const std::string text = serialize_play_marker(marker);
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
}

}  // namespace

void PlayTimeLaunch(const std::string& key) {
    std::remove(kPlayMarkerPath);
    if (Settings().play_history == "off" || !valid_tile_key(key)) return;
    PlayMarker marker;
    marker.key = key;
    marker.launch = static_cast<std::int64_t>(std::time(nullptr));
    write_marker(marker);
}

void PlayTimeLaunchNand(const std::string& folder) {
    std::string text;
    PlayMarker marker;
    if (!ReadTextFile(kPlayMarkerPath, text, 512) || !parse_play_marker(text, marker)) return;
    marker.emu_nand = folder;
    write_marker(marker);
}

}  // namespace riftwii::wii
