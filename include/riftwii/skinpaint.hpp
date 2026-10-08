// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "riftwii/canvas.hpp"
#include "riftwii/theme.hpp"

// The menu's own pictures, painted in a theme's colours and corners: what
// the Wii shows when a theme has no <name>.png, and the templates in the
// theme kit (tools/theme_kit.cpp). Free of libogc so both use one painter.
namespace riftwii {

// Paints theme_images()'s `name` at its listed size into `out`. False for
// a name theme_images() does not list.
bool paint_theme_image(const std::string& name, const Theme& theme, Canvas& out);

// The rounded box behind a hover name, `w` x `h` with a margin of
// kHintBoxMargin all round for its shadow. Sized to the text, so it is
// painted on the Wii as needed rather than listed for themes.
constexpr int kHintBoxMargin = 6;
void paint_hint_box(const Theme& theme, int w, int h, Canvas& out);
// The Mods page's picture popup: a card `w` x `h`, the same margin.
void paint_art_frame(const Theme& theme, int w, int h, Canvas& out);
// Home's clock figures: 0 to 9 then the colon, each 28 x 44, side by side.
void paint_clock_digits(const Theme& theme, Canvas& out);

}  // namespace riftwii
