// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui_motion.hpp"

#include <algorithm>
#include <cmath>

#include "menutheme.hpp"
#include "skin.hpp"
#include "transition.hpp"
#include "video.h"

namespace riftwii::wii {

float HoverSwell::Advance(bool selected) {
    if (selected != selected_) {
        selected_ = selected;
        from_ = amount_;
        elapsed_ = 0;
    }
    const float duration = selected_ ? 240.0f : 180.0f;
    elapsed_ = std::min(duration, elapsed_ + transition::FrameMilliseconds());
    const float progress = elapsed_ / duration;
    const float eased = selected_ ? transition::EaseBack(progress) : transition::EaseOut(progress);
    const float target = selected_ ? 1.0f : 0.0f;
    amount_ = from_ + (target - from_) * eased;
    return amount_;
}

float ButtonSwellScale(float width, float amount) {
    // At most eight pixels added at either end, even for a full-width Play.
    return 1 + std::min(0.06f, 16.0f / std::max(1.0f, width)) * amount;
}

void DrawButtonGlint(float x, float y, float w, float h, float radius, int alpha) {
    const float phase = transition::Cycle(3500.0f);
    constexpr float kSweep = 600.0f / 3500.0f;
    if (phase >= kSweep || alpha <= 0) return;

    radius = std::min(h / 2, radius * MenuTheme().corners);
    const float progress = phase / kSweep;
    const float band = std::min(42.0f, w * 0.18f);
    const float centre = x - band - h * 0.3f + (w + 2 * band + h * 0.6f) * progress;
    const int brightness = (skin::kCard.r + skin::kCard.g + skin::kCard.b) / 3;
    const float strength = (20 + brightness * 24 / 255) * alpha / 255.0f;

    // Each horizontal slice stops inside the rounded edge. No scissor is
    // changed: this also works inside a popup's scaled entrance camera.
    for (float row = 2; row < h - 2; row += 2) {
        const float cornerY = std::max(0.0f, std::max(radius - row, row + 2 - (h - radius)));
        const float inset = radius - std::sqrt(std::max(0.0f, radius * radius - cornerY * cornerY)) + 2;
        const float diagonal = centre + (h / 2 - row) * 0.6f;
        for (int strip = -4; strip < 4; ++strip) {
            const float left = std::max(x + inset, diagonal + strip * band / 4);
            const float right = std::min(x + w - inset, diagonal + (strip + 1) * band / 4);
            if (right <= left) continue;
            const float fade = 1 - std::fabs((strip + 0.5f) / 4);
            const auto light = static_cast<u8>(strength * fade * fade);
            Menu_DrawRectangle(left, y + row, right - left, std::min(2.0f, h - 2 - row),
                               (GXColor){255, 255, 255, light}, 1);
        }
    }
}

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
