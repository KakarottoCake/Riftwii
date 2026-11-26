// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <ctime>
#include <string>

namespace riftwii {

// A date as RiftWii writes it in logs, reports and messages: "2026-11-25",
// with the time "2026-11-25 18:42" or "2026-11-25 18:42:07". Formatted
// with snprintf rather than strftime, which brought newlib's second printf
// engine into the Wii build for these five formats.
enum class DateParts { Day, Minutes, Seconds };
std::string date_text(const std::tm& when, DateParts parts);

}  // namespace riftwii
