// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <string>

#include "riftwii/launch.hpp"
#include "riftwii/videopatch.hpp"

namespace riftwii {

// RiftWii's own settings (sd:/riftwii/settings.txt): "key = value" lines,
// "#" comments. Unknown keys are kept, so an older RiftWii does not drop
// what a newer one wrote.
struct LoaderSettings {
    std::string language = "auto";        // auto (the Wii's), en, es, ja, pt, it
    std::string video_width = "game";     // riftwii/videopatch.hpp names
    std::string deflicker = "game";
    std::string borders = "keep";         // keep, remove
    bool online = true;                   // download game names and cheats when the Wii is online
    std::map<std::string, std::string> other;

    void parse(const std::string& text);
    std::string serialize() const;
};

// The video settings a launch uses: the game's own choices, where it has
// them, over the global defaults.
VideoSettings effective_video(const GameSettings& game, const LoaderSettings& global);

}  // namespace riftwii
