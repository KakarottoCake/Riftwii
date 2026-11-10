// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "riftwii/plugin_ctgpr.hpp"

namespace riftwii::plugin {

// Builds the native CTGP-R launch's plan into request.plan, reading CTGP-R's
// files through request.sd and the loader's mount. False, with
// request.error (technical) and request.failure (which words the player
// gets), when a file is missing or wrong, or the CTGP-R version is not one
// the launch is made for.
bool ctgpr_prepare(CtgprLaunchRequest& request);

}  // namespace riftwii::plugin
