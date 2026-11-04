// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui_busy.hpp"

#include <algorithm>

#include "skin.hpp"
#include "transition.hpp"
#include "video.h"

namespace riftwii::wii {

void DrawPill(float x, float y, float width, int height, GXColor colour) {
    if (width <= 0 || colour.a == 0) return;
    // Small rounded strips need no texture, allocation or libm calls.
    const float radius = std::min(width / 2.0f, height / 2.0f);
    for (int row = 0; row < height; ++row) {
        const float edge = std::min(row + 0.5f, height - row - 0.5f);
        const float t = std::max(0.0f, 1.0f - edge / radius);
        const float inset = radius * t * t;
        Menu_DrawRectangle(x + inset, y + row, width - 2 * inset, 1, colour, 1);
    }
}

void DrawBusyRing(float x, float y, float elapsedMs, int alpha) {
    // Fixed positions avoid trigonometry and new theme textures. Three
    // strips give each dot soft, rounded corners at this size.
    // A 14 radius: at 10 it read as a speck beside 16-point text on a TV.
    constexpr int offsets[8][2] = {{0, -14}, {10, -10}, {14, 0}, {10, 10},
                                  {0, 14}, {-10, 10}, {-14, 0}, {-10, -10}};
    const float head = elapsedMs / 125.0f;
    const int turns = static_cast<int>(head / 8.0f);
    const float position = head - turns * 8.0f;
    const float fade = transition::Ease(std::min(1.0f, elapsedMs / 150.0f));
    for (int dot = 0; dot < 8; ++dot) {
        float age = position - dot;
        if (age < 0) age += 8.0f;
        // The next dot rises gently as the tail approaches it.
        const float brightness = age > 7.0f ? 0.18f + 0.82f * (age - 7.0f) : 1.0f - 0.82f * age / 7.0f;
        const int opacity = static_cast<int>(alpha * fade * brightness);
        const GXColor colour = skin::WithAlpha(skin::kAccent, opacity);
        const float left = x + offsets[dot][0] - 3;
        const float top = y + offsets[dot][1] - 3;
        Menu_DrawRectangle(left + 1, top, 4, 1, colour, 1);
        Menu_DrawRectangle(left, top + 1, 6, 4, colour, 1);
        Menu_DrawRectangle(left + 1, top + 5, 4, 1, colour, 1);
    }
}

void BusyRing::Show(bool active) {
    if (active && !active_) elapsed_ = 0;
    active_ = active;
}

void BusyRing::Draw() {
    if (!active_ || !IsVisible()) return;
    elapsed_ += transition::FrameMilliseconds();
    // Keep sub-frame precision even if a network job stays up for hours.
    if (elapsed_ >= 1150.0f) elapsed_ -= 1000.0f;
    DrawBusyRing(x_, y_, elapsed_, GetAlpha());
}

}  // namespace riftwii::wii
