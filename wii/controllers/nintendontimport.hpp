// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string>
#include <vector>

#include "riftwii/hidconfig.hpp"

namespace riftwii::wii {

constexpr unsigned kHidImportMaxFiles = 64;

// Run on the menu thread. Every directory walk and file read is bounded.
bool FindNintendontLayouts(std::vector<std::string>& paths, bool& limited);

struct NintendontLayout {
    std::array<char, kHidConfigMaxBytes + 1> text{};
    std::size_t bytes = 0;
};

enum class NintendontRead { Ready, Unreadable, TooLarge, Invalid, WrongDevice };
NintendontRead ReadNintendontLayout(const std::string& path, NintendontLayout& layout, std::string& detail);
bool PrepareControllerFolder();

}  // namespace riftwii::wii
