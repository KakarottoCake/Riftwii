// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "playtimemark.hpp"

#include <sys/stat.h>

#include <cstdio>
#include <ctime>

#include "loadersettings.hpp"
#include "log.hpp"
#include "textfile.hpp"
#include "riftwii/playtime.hpp"

namespace riftwii::wii {
namespace {

void write_marker(const PlayMarker& marker) {
    mkdir("sd:/riftwii", 0777);
    const std::string text = serialize_play_marker(marker);
    if (!WriteWholeFile(kPlayMarkerPath, text.data(), text.size())) {
        std::remove(kPlayMarkerPath);  // never a half note
        logf("Play time: %s could not be written, so this launch's play time is not collected\n", kPlayMarkerPath);
    }
}

}  // namespace

void PlayTimeLaunch(const std::string& key, bool gamecube) {
    std::remove(kPlayMarkerPath);
    // Not for a GameCube or emulator game: no play log of theirs to read.
    if (Settings().play_history == "off" || gamecube || !valid_tile_key(key)) return;
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
