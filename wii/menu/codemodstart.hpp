// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "riftwii/codemods.hpp"

// How each code mod starts, remembered in sd:/riftwii/codemods.txt
// (riftwii/codemods.hpp, #368): read once, written whole beside the old
// file and then put in its place at each change.
namespace riftwii::wii {

CodeModStart CodeModAnswer(const std::string& game_id, const std::string& key);
CodeModStart SharedCodeModAnswer(const std::string& game_id, const std::vector<std::string>& keys);
// Each changes the answers and saves them; false when the card could not
// be written (the answer then lasts until RiftWii closes).
bool SetCodeModAnswer(const std::string& game_id, const std::string& key, CodeModStart start);
bool RememberCodeModAnswer(const std::string& game_id, const std::vector<std::string>& keys, CodeModStart picked);
// The SD card was taken out and put back (perhaps another one): its file
// is read again before the next answer, never the old card's written onto it.
void ForgetCodeModAnswers();

}  // namespace riftwii::wii
