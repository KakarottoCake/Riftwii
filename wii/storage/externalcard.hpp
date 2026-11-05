// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "riftwii/gcimport.hpp"

namespace riftwii::wii {

bool ListImportCards(const std::string& folder, std::vector<GcFolderEntry>& entries);
std::string ExternalCardPath(const std::string& game_id, const std::string& source);

}  // namespace riftwii::wii
