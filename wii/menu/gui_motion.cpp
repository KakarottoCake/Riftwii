// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui_motion.hpp"

#include "transition.hpp"
#include "video.h"

namespace riftwii::wii {

void DrawInvitingArrow(const skin::Tex& plain, const skin::Tex& over,
                      float x, float y, int dx, int dy, bool lit,
                      float& lift, int alpha) {
    lift = transition::Follow(lift, lit ? 1.0f : 0.0f, 47.0f);
    const float phase = transition::Cycle(1600.0f);
    const float triangle = phase < 0.5f ? phase * 2 : (1 - phase) * 2;
    const float inward = 3.0f * transition::Ease(triangle) + 3.0f * lift;
    const float scale = 1.0f + 0.08f * lift;
    Menu_PushCamera(scale, x + plain.w / 2.0f, y + plain.h / 2.0f,
                    -dx * inward, -dy * inward);
    skin::Draw(plain, x, y, alpha);
    if (lift > 0.001f) skin::Draw(over, x, y, static_cast<int>(alpha * lift));
    Menu_PopCamera();
}

}  // namespace riftwii::wii
