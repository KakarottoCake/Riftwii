// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace riftwii::wii {

// Before ES_Identify or opening a disc partition: d2x refuses this request
// once a title runs. Repeated preparation on the same IOS keeps an accepted
// target, including across a disc probe and the final partition open.
// True when d2x has accepted this nonzero title. Zero clears the target.
bool prepare_d2x_return_to(std::uint64_t title);

// With NAND emulation on, keep the game's Wii Menu exit: even partial
// emulation redirects the channel's ticket lookup away from the real NAND.
bool should_patch_game_return_to(std::uint64_t title, bool nand_emulated, bool d2x_accepted);

// An IOS reload discards d2x's setting, even when it reloads the same slot.
void forget_d2x_return_to();

}  // namespace riftwii::wii
