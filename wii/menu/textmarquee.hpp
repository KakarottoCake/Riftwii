// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>

#include "libwiigui/gui.h"

namespace riftwii::wii {

// Draw an unwrapped GuiText with no max width through a moving window.
// The painter receives its top-left position and opacity, so shadows and
// outlines can share the clip and fade. Call once per GUI frame. Short text
// stays still; Reset restarts the pause after changing the text or its font.
// Like the menu's scrolling lists, restores the full-screen scissor on exit.
class TextMarquee {
public:
    using Painter = std::function<void(int x, int y, int alpha)>;

    void Draw(GuiText& text, int x, int y, int width, int height, const Painter& paint);
    void Reset();

private:
    int previousTextWidth = -1;
    int previousWidth = -1;
    float offset = 0.0f;
    float pauseMs = 1400.0f;
    bool returning = false;
};

}  // namespace riftwii::wii
