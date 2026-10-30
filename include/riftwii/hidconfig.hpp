// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: FIX94 and the Nintendont contributors, used with permission
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

#include "rthid.h"

namespace riftwii {

constexpr std::size_t kHidConfigMaxBytes = 8192;

// Strict, bounded reader for Nintendont's controller.ini format. No
// partially parsed mapping escapes on failure; error names the bad key.
bool parse_hid_config(std::string_view text, rthid_mapping& mapping, std::string& error);

}  // namespace riftwii
