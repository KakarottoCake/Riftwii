// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace riftwii::wii {

// Before ES_Identify or opening a disc partition: d2x refuses this request
// once a title runs. Repeated preparation on the same IOS keeps an accepted
// target, including across a disc probe and the final partition open.
void prepare_d2x_return_to(std::uint64_t title);

// An IOS reload discards d2x's setting, even when it reloads the same slot.
void forget_d2x_return_to();

}  // namespace riftwii::wii
