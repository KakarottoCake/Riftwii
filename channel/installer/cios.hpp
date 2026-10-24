// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "riftwii/d2xversion.hpp"

namespace installer {

// Read the same first-content information as RiftWii's cIOS search.
// Failure to read a slot never grants permission to reload into it.
// `read` says whether the slot's first content was read at all, so a
// slot that is not d2x can be told from one that could not be checked.
riftwii::D2xInfo ReadCios(int slot, bool& read);

}  // namespace installer
