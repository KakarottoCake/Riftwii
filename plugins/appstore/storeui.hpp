// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "riftwii/plugin_appstore.h"

namespace riftwii::appstore {

// The store's pages, from opening the screen until the player leaves:
// RWP_LAUNCH_DONE, or RWP_LAUNCH_FAILED with the reason in the request.
std::int32_t RunStore(rwp_appstore_request& request);

}  // namespace riftwii::appstore
