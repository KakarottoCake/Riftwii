// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

// Which of the age rating boards' marks (wii/assets/rating_*.png) shows a
// game's rating on its About page, from GameTDB's <rating type value>.
// Each board's current mark: the ESRB's of 2013, PEGI's of 2009, CERO's
// of 2006, Korea's GRAC (GameTDB still says GRB, its old name; its 18 was
// raised to 19 in 2024) and Taiwan's GSRR (GameTDB's CSRR, the old name).
namespace riftwii {

// The mark's name, "esrb_e10" for rating_esrb_e10.png, or "" when there is
// no mark for it (an empty or unknown rating: the page then writes it out).
// Case and spaces around either part don't matter.
std::string rating_mark_name(const std::string& system, const std::string& value);

}  // namespace riftwii
