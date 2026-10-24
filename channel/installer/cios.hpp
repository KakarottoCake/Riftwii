// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "riftwii/d2xversion.hpp"

namespace installer {

// Read the same first-content information as RiftWii's cIOS search.
// Failure to read a slot never grants permission to reload into it.
riftwii::D2xInfo ReadCios(int slot);

}  // namespace installer
