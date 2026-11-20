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

// Safe PNG stems, metadata name first and folder basename second. Invalid
// FAT characters are removed; empty names and dot-only names are omitted.
std::vector<std::string> app_cover_names(const std::string& meta_name, const std::string& folder_name);

// A stable tile key for an app in sd:/apps. Escapes separators used by
// settings.txt so commas, spaces and percent signs round-trip unchanged.
std::string app_tile_key(const std::string& folder_path);
bool valid_app_tile_key(const std::string& key);

// Empty on an invalid argv[0]; otherwise NUL-separated arguments and a final double NUL.
std::string app_command_line(const std::string& path, const std::vector<std::string>& arguments);

constexpr std::size_t kAppCommandLineMax = 1024;

}
