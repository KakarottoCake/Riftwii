// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "riftwii/canvas.hpp"

// The app store's icons (theme pictures store_*, docs/THEMES.md): its
// categories' at 40x40 and an app's facts' at 28x28, in `ink`. Painted by
// the store's plugin (white, which the menu tints with the theme's glyph
// colour) and by the theme kit; not by the menu, so the DOL stays without
// them. A theme's own PNG of the same name takes their place.
namespace riftwii {

bool paint_store_icon(const std::string& name, Rgba ink, Canvas& out);

}  // namespace riftwii
