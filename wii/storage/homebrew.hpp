// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

namespace riftwii::wii {

struct HomebrewApp {
    std::string name;     // Home's tile: the meta.xml's <name>, else the folder's
    std::string folder;   // "sd:/apps/wiimc"
    std::string program;  // "sd:/apps/wiimc/boot.dol" (or boot.elf)
    std::string path;     // the DOL to start, empty for a boot.elf app
    std::string device;   // "SD" or "USB": the tile's badge
    std::string coder;
    std::string version;
    std::string date;         // <release_date> as a date ("18/10/2026")
    std::string description;  // <short_description>
    std::vector<std::string> arguments;  // <arguments>/<arg>
    std::string problem;
};

const std::vector<HomebrewApp>& HomebrewApps();
void RescanHomebrewApps();
bool HomebrewFoldersRead();

void SetHomebrewToStart(const HomebrewApp& app);
const HomebrewApp& HomebrewToStart();

bool StartHomebrew(const HomebrewApp& app, std::string& error);

}  // namespace riftwii::wii
