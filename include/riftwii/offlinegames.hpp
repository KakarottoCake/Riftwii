// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace riftwii {

// Whether GameTDB says the game has no online play (no Nintendo Wi-Fi
// Connection features: tools/make_offline_games.py), by its ID's first
// four characters. False for games it does not know, hacks and mod
// distributions with IDs of their own included: an online server may
// matter to them.
bool game_has_no_online(const std::string& game_id);

}  // namespace riftwii
