// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace riftwii::wii {

// Tells the player's RiiTag servers (riftwii/riitag.hpp) the game being
// started: every <Tag> in sd:/riftwii/Wiinnertag.xml and USB Loader GX's
// sd:/apps/usbloader_gx/Wiinnertag.xml, and riitag_key in settings.txt for
// riitag.t0g3pii.de. Nothing without any, or with Settings > Online off.
// `background`: on the network's thread (the menu, which waits for it
// before the game); else here and now (a headless launch).
void TagGame(const std::string& game_id, bool background);

}  // namespace riftwii::wii
