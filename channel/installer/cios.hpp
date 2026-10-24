// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "riftwii/d2xversion.hpp"

namespace installer {

// Read the same first-content information as RiftWii's cIOS search.
// `read` says whether the slot's first content was read at all, so a
// slot that is not d2x can be told from one that could not be checked.
// `restored` checks both the permission word and protection register.
// False forbids any IOS reload, even the ticket fallback or the return to RiftWii.
riftwii::D2xInfo ReadCios(int slot, bool& read, bool& restored);

}  // namespace installer
