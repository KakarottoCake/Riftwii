// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "skin.hpp"

namespace riftwii::wii {

// A quiet sweep every 3.5 seconds, inside the button's rounded body.
// Draw before its label, under the same camera as the button.
void DrawButtonGlint(float x, float y, float w, float h, float radius, int alpha = 255);

// Direction points out of the page. The invitation moves inward, keeping
// an edge arrow inside the safe area; its hit area stays still.
void DrawInvitingArrow(const skin::Tex& plain, const skin::Tex& over,
                      float x, float y, int dx, int dy, bool lit,
                      float& lift, int alpha = 255);

}  // namespace riftwii::wii
