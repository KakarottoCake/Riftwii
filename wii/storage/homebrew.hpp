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
};

const std::vector<HomebrewApp>& HomebrewApps();
void RescanHomebrewApps();
bool HomebrewFoldersRead();

void SetHomebrewToStart(const HomebrewApp& app);
const HomebrewApp& HomebrewToStart();

bool StartHomebrew(const HomebrewApp& app, std::string& error);

}
