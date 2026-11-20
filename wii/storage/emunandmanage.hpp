// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace riftwii::wii {

struct EmuNandTitle {
    std::uint64_t id = 0;
    std::string name;
    bool emulated = false;
    bool real = false;
};

enum class EmuNandAction {
    DeleteTitle, DeleteSave, SaveToEmulated, SaveToWii,
    MiisToEmulated, MiisToWii, RemoveSettings
};

// Configured save/channel folders, per-game folders and existing /nands
// children. USB folders are opened through the writable partition mount.
std::vector<std::string> emunand_manager_folders();
bool emunand_manager_list(const std::string& folder, std::vector<EmuNandTitle>& titles,
                          bool& sysconf, bool& settings, std::string& error);
// Called only after confirmation, with the drive scanner stopped and a
// CardWriteHold alive. All policy checks are repeated here, not just in UI.
bool emunand_manager_action(const std::string& folder, EmuNandAction action,
                            std::uint64_t title, std::string& error);

}  // namespace riftwii::wii
