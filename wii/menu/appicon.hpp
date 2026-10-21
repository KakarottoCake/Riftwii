// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "skin.hpp"

namespace riftwii::wii {

// Cache lookup only; safe for the GUI draw callback.
bool AppIcon(const std::string& folder, skin::Tex& out);

// Load from the menu loop, with the GUI halted and the drive scan finished.
bool AppIconWanted(const std::string& folder);
void LoadAppIcon(const std::string& folder);

void ForgetAppIcons();

}  // namespace riftwii::wii
