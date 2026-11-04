// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "skin.hpp"

namespace riftwii::wii {

// Direction points out of the page. The invitation moves inward, keeping
// an edge arrow inside the safe area; its hit area stays still.
void DrawInvitingArrow(const skin::Tex& plain, const skin::Tex& over,
                      float x, float y, int dx, int dy, bool lit,
                      float& lift, int alpha = 255);

}  // namespace riftwii::wii
