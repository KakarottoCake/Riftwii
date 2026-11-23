// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "textmarquee.hpp"

#include <algorithm>
#include <cmath>

#include "transition.hpp"
#include "video.h"

namespace riftwii::wii {

void TextMarquee::Reset() {
    offset = 0.0f;
    pauseMs = 1400.0f;
    returning = false;
}

void TextMarquee::Draw(GuiText& text, int x, int y, int width, int height, const Painter& paint) {
    if (width <= 0 || height <= 0) return;
    const int textWidth = text.GetTextWidth();
    if (textWidth != previousTextWidth || width != previousWidth) {
        Reset();
        previousTextWidth = textWidth;
        previousWidth = width;
    }
    const float overflow = static_cast<float>(std::max(0, textWidth - width));
    if (overflow == 0.0f) {
        paint(x, y, 255);
        return;
    }

    const float frameMs = transition::FrameMilliseconds();
    if (pauseMs > 0.0f) {
        pauseMs = std::max(0.0f, pauseMs - frameMs);
    } else {
        // Menu units per second, independent of PAL/NTSC refresh rate.
        const float step = frameMs * 0.022f;
        offset = std::clamp(offset + (returning ? -step : step), 0.0f, overflow);
        if (offset == 0.0f || offset == overflow) {
            returning = !returning;
            pauseMs = 1400.0f;
        }
    }

    const auto drawSlice = [&](int left, int sliceWidth, int alpha) {
        if (sliceWidth <= 0) return;
        Menu_Scissor(left, y - 4, sliceWidth, height + 8);
        paint(x - static_cast<int>(std::round(offset)), y, alpha);
    };

    // Fade the glyphs themselves in a few non-overlapping slices. This
    // works over box art as well as a solid background, without a cover-up.
    // Each slice draws the whole line again (GX clips it afterwards, and an
    // outlined line is several draws), so a fade is two slices a side: at
    // most five draws a frame instead of one per few pixels.
    constexpr int kFadeSlices = 2;
    const int fadeWidth = std::min(12, width / 2);
    const int leftFade = std::min(fadeWidth, static_cast<int>(std::ceil(offset)));
    const int rightFade = std::min(fadeWidth, static_cast<int>(std::ceil(overflow - offset)));
    drawSlice(x + leftFade, width - leftFade - rightFade, 255);
    for (int s = 0; s < kFadeSlices; ++s) {
        // Slice s from the edge: a third, then two thirds, of full strength.
        const int alpha = 255 * (s + 1) / (kFadeSlices + 1);
        const int leftFrom = leftFade * s / kFadeSlices;
        const int leftTo = leftFade * (s + 1) / kFadeSlices;
        drawSlice(x + leftFrom, leftTo - leftFrom, alpha);
        const int rightFrom = rightFade * s / kFadeSlices;
        const int rightTo = rightFade * (s + 1) / kFadeSlices;
        drawSlice(x + width - rightTo, rightTo - rightFrom, alpha);
    }
    GX_SetScissor(0, 0, Menu_XfbWidth(), Menu_EfbHeight());
}

}  // namespace riftwii::wii
