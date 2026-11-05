// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "riftwii/settingsfile.hpp"

namespace riftwii {

bool game_page_id(const std::string& id);
std::string game_page_name(const LoaderSettings& settings, const std::string& id, const std::string& original);
void game_page_rename(LoaderSettings& settings, const std::string& id, const std::string& name);
bool game_page_hidden(const LoaderSettings& settings, const std::string& id);
void game_page_hide(LoaderSettings& settings, const std::string& id, bool hidden);
bool game_page_show_hidden(const LoaderSettings& settings);

struct GameDeletion {
    std::vector<std::string> images;
    std::string empty_folder;  // only rmdir, never recursive deletion
};

// Entries are leaf names from the selected image's own directory. Invalid
// names never become paths. An incomplete split image can still be removed.
bool game_deletion_files(const std::string& primary, bool gamecube, const std::vector<std::string>& siblings,
                         GameDeletion& out);
// Only RiftWii's own per-game files. Shared cards and other loaders stay.
std::vector<std::string> game_deletion_extras(const std::string& id, bool saves);

}  // namespace riftwii
