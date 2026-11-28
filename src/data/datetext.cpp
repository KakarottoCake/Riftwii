// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/datetext.hpp"

#include <cstdio>

namespace riftwii {

std::string date_text(const std::tm& when, DateParts parts) {
    char text[64];
    const int year = when.tm_year + 1900;
    const int month = when.tm_mon + 1;
    if (parts == DateParts::Day)
        std::snprintf(text, sizeof(text), "%04d-%02d-%02d", year, month, when.tm_mday);
    else if (parts == DateParts::Minutes)
        std::snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d", year, month, when.tm_mday, when.tm_hour,
                      when.tm_min);
    else
        std::snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d:%02d", year, month, when.tm_mday, when.tm_hour,
                      when.tm_min, when.tm_sec);
    return text;
}

}  // namespace riftwii
