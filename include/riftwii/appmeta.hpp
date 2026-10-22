// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

namespace riftwii {

struct AppMeta {
    std::string name;
    std::string coder;
    std::string version;
    std::string release_date;
    std::string description;
    std::vector<std::string> arguments;
    bool valid = false;
};

bool parse_app_meta(const std::string& text, AppMeta& out);

std::string app_display_name(const AppMeta& meta, const std::string& folder);

std::string app_release_date(const std::string& raw);

// Empty on an invalid argv[0]; otherwise NUL-separated arguments and a final double NUL.
std::string app_command_line(const std::string& path, const std::vector<std::string>& arguments);

constexpr std::size_t kAppCommandLineMax = 1024;

}
