// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "riftwii/fat32.hpp"

// Where an SD file's bytes are on the card: the raw device sectors the
// resident runtime will read without a file system (riftwii/fat32.hpp's
// walker over libogc's SD driver, so the card must be mounted). Paths are
// the sd:/ paths the rest of the loader uses.
namespace riftwii::wii {

bool resolve_sd_file(const std::string& sd_path, Fat32File& out, std::string& error);

}  // namespace riftwii::wii
