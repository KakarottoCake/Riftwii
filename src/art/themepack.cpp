// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/themepack.hpp"

#include <sstream>

namespace riftwii {

bool theme_pack_name_ok(const std::string& name, bool file) {
    if (name.empty() || name.size() > 64 || name[0] == '.' || name[0] == ' ') return false;
    for (char c : name) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
                        c == '-' || c == ' ' || (file && c == '.');
        if (!ok) return false;
    }
    return true;
}

namespace {

bool path_ok(const std::string& path) {
    const std::size_t slash = path.find('/');
    return slash != std::string::npos && theme_pack_name_ok(path.substr(0, slash), false) &&
           theme_pack_name_ok(path.substr(slash + 1), true);
}

}  // namespace

bool parse_theme_pack(const std::uint8_t* data, std::size_t size, ThemePack& out, std::string& error,
                      const char* header, std::size_t* end) {
    out = ThemePack{};
    std::size_t at = 0;
    // One text line from `at`, without its newline; false at the end.
    const auto line = [&](std::string& text) {
        std::size_t end = at;
        while (end < size && data[end] != '\n') ++end;
        if (end >= size || end - at > 1024) return false;
        text.assign(reinterpret_cast<const char*>(data) + at, end - at);
        at = end + 1;
        return true;
    };
    std::string text;
    if (!line(text) || text != header) {
        error = std::string("not a RiftWii pack (") + header + ")";
        return false;
    }
    while (line(text)) {
        if (text == "end") {
            if (end) *end = at;
            return true;
        }
        if (text.compare(0, 7, "retire ") == 0) {
            std::istringstream in(text.substr(7));
            RetiredTheme r;
            in >> r.name >> r.replacement;
            if (!theme_pack_name_ok(r.name, false) ||
                (r.replacement != "-" && !theme_pack_name_ok(r.replacement, false))) {
                error = "a retired theme with a bad name: " + text;
                return false;
            }
            if (r.replacement == "-") r.replacement.clear();
            for (std::string f; in >> f;) {
                if (!theme_pack_name_ok(f, true)) {
                    error = "a retired theme's file with a bad name: " + f;
                    return false;
                }
                r.files.push_back(f);
            }
            out.retired.push_back(r);
            continue;
        }
        if (text.compare(0, 5, "file ") == 0) {
            // The path may hold spaces: the size is the last word.
            const std::size_t space = text.rfind(' ');
            ThemePackFile f;
            f.path = text.substr(5, space - 5);
            const std::string count = text.substr(space + 1);
            if (space <= 5 || count.empty() || count.size() > 9 || count.find_first_not_of("0123456789") != std::string::npos) {
                error = "a file line without its size: " + text;
                return false;
            }
            if (!path_ok(f.path)) {
                error = "a file outside one theme's folder: " + f.path;
                return false;
            }
            f.size = static_cast<std::size_t>(std::stoul(count));
            f.offset = at;
            if (f.size > size - at || size - at - f.size < 1 || data[at + f.size] != '\n') {
                error = f.path + " is cut short";
                return false;
            }
            at += f.size + 1;
            out.files.push_back(f);
            continue;
        }
        error = "a line the pack should not have: " + text.substr(0, 60);
        return false;
    }
    error = "the pack is cut short (no end line)";
    return false;
}

}  // namespace riftwii
