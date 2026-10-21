// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "skin.hpp"

namespace riftwii::wii {

bool AppIcon(const std::string& folder, skin::Tex& out);

void ForgetAppIcons();

}  // namespace riftwii::wii
