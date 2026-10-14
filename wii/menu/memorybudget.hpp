// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "frontend.hpp"
#include "riftwii/extrasbudget.hpp"

namespace riftwii::wii {

// What RiftWii's extras will take of this game's memory when it starts
// with the game page's choices and Settings as they are now, from the
// blobs' own sizes (riftwii/extrasbudget.hpp). An estimate: a pack's
// redirect table is counted at a typical size, an RVZ's buffers at a
// compressed one's, the GameCube adapter in Automatic only when it is
// plugged in now. Names are translated.
ExtrasBudget EstimateExtras(const FrontendState& state);

}  // namespace riftwii::wii
