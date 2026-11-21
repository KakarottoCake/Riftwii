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
    // Only a ticket is left of it in the emulated NAND (a delete that
    // stopped after the title's folder): Delete title finishes it.
    bool ticket_only = false;
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

// Moving a title between the drives, for a player whose emulated NAND is on
// the USB drive: RiftWii plays channels and WADs only from one on the SD
// card. Where a title in `folder` is copied to: the SD card's channel
// folder (the one Home lists channels from, else sd:/nands/01) for a folder
// on the USB drive or another folder on the card (WiiFlow's nands/default),
// usb:/nands/01 for the channel folder itself; "" when `folder` is neither.
std::string emunand_copy_destination(const std::string& folder);
// Copies the title (its program, save and ticket, the shared contents it
// uses and its entry in the ownership list) from `from` to `to`, at least
// one of them on the SD card. The original stays. Refused when the title is
// already in `to`; a copy cut short is taken out of `to` again. Same
// preconditions as emunand_manager_action.
bool emunand_manager_copy_title(const std::string& from, const std::string& to, std::uint64_t title,
                                std::string& error);

}  // namespace riftwii::wii
