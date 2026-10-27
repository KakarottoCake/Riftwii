// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

namespace riftwii::wii {

struct HomebrewApp {
    std::string name;
    std::string folder;
    std::string program;
    std::string path;
    std::string device;
    std::string coder;
    std::string version;
    std::string date;
    std::string description;
    std::vector<std::string> arguments;
    std::string problem;
    std::string meta_name;   // meta.xml's <name>, empty without one
    // GameTDB's ID for the app (riftwii/homebrewmatch.hpp), found by its
    // name, for its cover; empty when GameTDB's list has no such app or
    // is not on the card.
    std::string gametdb_id;
};

const std::vector<HomebrewApp>& HomebrewApps();
void RescanHomebrewApps();
bool HomebrewFoldersRead();
// Finds each app's GameTDB entry again (sd:/riftwii/homebrew-titles.txt
// just made), without reading sd:/apps again.
void MatchHomebrewApps();
// Whether that file was written since the apps were last matched.
bool HomebrewMatchStale();

void SetHomebrewToStart(const HomebrewApp& app);
const HomebrewApp& HomebrewToStart();

bool StartHomebrew(const HomebrewApp& app, std::string& error);

}
