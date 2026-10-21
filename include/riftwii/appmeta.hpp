// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

namespace riftwii {

struct AppMeta {
    std::string name;          // <name>
    std::string coder;         // <coder>
    std::string version;       // <version>
    std::string release_date;  // <release_date>, as written
    std::string description;   // <short_description>, else <long_description>
    std::vector<std::string> arguments;
    bool valid = false;  // the text was XML (a damaged file is not)
};

bool parse_app_meta(const std::string& text, AppMeta& out);

std::string app_display_name(const AppMeta& meta, const std::string& folder);

std::string app_release_date(const std::string& raw);

std::string app_command_line(const std::string& path, const std::vector<std::string>& arguments);

constexpr std::size_t kAppCommandLineMax = 1024;

}  // namespace riftwii
