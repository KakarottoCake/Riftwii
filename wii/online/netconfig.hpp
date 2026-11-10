// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace riftwii::wii {

// The Wii's saved network settings (/shared2/sys/net/02/config.dat, see
// riftwii/dnsquery.hpp), read from the NAND. False, with `why`, when the
// NAND or the file cannot be read or is not a whole settings file.
bool ReadSavedNetConfig(std::vector<std::uint8_t>& config, std::string& why);

}  // namespace riftwii::wii
