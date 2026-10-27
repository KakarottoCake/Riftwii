// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "skin.hpp"

namespace riftwii::wii {

// The rating board's own mark for a game's age rating (riftwii/ratingmark.hpp),
// decoded from the PNG embedded from wii/assets the first time it is shown
// and kept in MEM2 for the menu phase. A Tex without data when the rating
// has no mark or its picture can't be read: the About page then writes the
// rating out instead.
const skin::Tex& RatingBadge(const std::string& system, const std::string& value);

}  // namespace riftwii::wii
