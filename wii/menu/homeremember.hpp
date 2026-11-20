// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "riftwii/launch.hpp"
#include "usbcatalog.hpp"

// Home's games from the last look at the drives, on the card as
// sd:/riftwii/homelist.txt (riftwii/homelist.hpp): shown at once at the
// next start, while the drives are read again behind them.
namespace riftwii::wii {

// The remembered games into `sd` and `usb`, and the ones that had mods
// into `packs` (for the Mods view and the tiles' badges). For the first
// look of a run only, before either drive was read; the scan's parts
// replace all three as they come in. False, and nothing changed, when
// there is no list, it holds no game, or it is not whole.
bool ShowRememberedGames(ImageCatalog& sd, ImageCatalog& usb, PackIndex& packs);

// After a look at both drives: the games Home lists now, written to the
// card when they differ from the list it holds. Menu thread, once the
// scan has let go of the drives.
void RememberGames(const ImageCatalog& sd, const ImageCatalog& usb, const PackIndex& packs);

}  // namespace riftwii::wii
