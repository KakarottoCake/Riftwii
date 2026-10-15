// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "riftwii/canvas.hpp"
#include "riftwii/theme.hpp"

// Controller and button pictures from Kenney's Input Prompts (CC0,
// vendor-kenney/input-prompts), drawn in a theme's colour at any size: the
// About page's controllers, the button prompts' badges, the Controllers
// tile.
namespace riftwii {

// The picture for a controller GameTDB names (wiimote, nunchuk,
// classiccontroller, gamecube), or "" when there is none for it.
std::string input_icon_for_control(const std::string& type);
// The picture for a button a prompt names (A, B, +, -, 1, 2, HOME, C, Z),
// or "" when there is none.
std::string input_icon_for_button(const std::string& button);

// `name` (a picture's file name without ".png") as a `size` x `size`
// canvas in `color`, each pixel the average of the area it covers. False
// (and `out` untouched) when there is no such picture.
bool paint_input_icon(const std::string& name, int size, Rgba color, Canvas& out);

}  // namespace riftwii
