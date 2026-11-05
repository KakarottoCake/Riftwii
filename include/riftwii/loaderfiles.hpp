// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

namespace riftwii {

// Only a disc ID may become a file name in another loader's folders.
bool loader_file_id(const std::string& id);
std::vector<std::string> loader_cover_paths(const std::string& id);
std::vector<std::string> loader_cheat_paths(const std::string& id);

}  // namespace riftwii
