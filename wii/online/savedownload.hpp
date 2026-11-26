// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include "riftwii/savelist.hpp"

namespace riftwii::wii::saves {

// Call on the menu thread, after drive scans and earlier network work ended.
// The catalogue is cached in MEM2 for this session. A failed fetch can retry.
bool HaveList();
const SaveListing* Listing(std::size_t& count);
bool StartList();
// Wii: destination is its configured emulated NAND root. GameCube: its
// RiftWii SD memory card path, as gc_card_file_of resolves it.
bool StartInstall(const SaveListing& save, const std::string& destination);
enum class Stage { Download, Check, Install, Finish };
struct Progress {
    Stage stage = Stage::Download;
    unsigned percent = 0;
};
Progress CurrentProgress();
void Cancel();
// False while working; the completed job's result is consumed once.
bool TakeResult(bool& success, std::string& backup, std::string& error);

}  // namespace riftwii::wii::saves
