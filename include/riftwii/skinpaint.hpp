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

}  // namespace riftwii
