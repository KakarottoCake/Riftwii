// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "homebrew.hpp"

namespace riftwii::wii {

enum class HomebrewArt { Cover, Cover3d, Box };

// Menu thread, GUI halted. Rescans PNG names only when the apps are scanned;
// matching GameTDB again only updates the fallback IDs. Publishes a copy
// for the cover readers, which ResumeGui releases after publication.
void ConfigureHomebrewArt(const std::vector<HomebrewApp>& apps, bool rescan);
std::string HomebrewArtName(const std::string& key, HomebrewArt kind);
std::string HomebrewCoverId(const std::string& key);
std::vector<std::string> HomebrewCoverKeys(const std::string& gametdb_id);

}  // namespace riftwii::wii
