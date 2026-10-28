// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "homebrew.hpp"
#include "riftwii/emulatorfile.hpp"

namespace riftwii::wii {

struct EmulatorAddon {
    EmulatorFile file;
    std::string folder;
};

struct EmulatorGame {
    std::size_t emulator = 0;
    std::string path, name, key;
};

struct EmulatorCatalog {
    std::vector<EmulatorAddon> addons;
    std::vector<EmulatorGame> games;
};

// Called by the drive scan once both volumes have been mounted. The menu
// receives a finished snapshot, never a list being changed on that thread.
void ScanEmulators(EmulatorCatalog& out);
void ForgetSdEmulators(EmulatorCatalog& catalog);
EmulatorValues LoadEmulatorValues(const EmulatorAddon& addon, const std::string& game = "");
bool SaveEmulatorValues(const EmulatorAddon& addon, const std::string& game, const EmulatorValues& values);
bool PrepareEmulatorApp(const EmulatorAddon& addon, const EmulatorGame& game,
                        const EmulatorValues& values, HomebrewApp& app, std::string& error);

}  // namespace riftwii::wii
