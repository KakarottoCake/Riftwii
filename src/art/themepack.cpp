// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/themepack.hpp"

#include "riftwii/textlines.hpp"
#include "riftwii/checkedalloc.hpp"

#include <algorithm>

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

// The apps part also carries RiftWii's plugins, one folder deeper
// ("riftwii/plugins/<file>"): a folder, then a path as above.
bool apps_path_ok(const std::string& path) {
    if (path_ok(path)) return true;
    const std::size_t slash = path.find('/');
    return slash != std::string::npos && theme_pack_name_ok(path.substr(0, slash), false) &&
           path_ok(path.substr(slash + 1));
}

}  // namespace

bool parse_theme_pack(const std::uint8_t* data, std::size_t size, ThemePack& out, std::string& error,
                      const char* header, std::size_t* end) {
    const ThemePackReader read = [data, size](std::size_t at, std::uint8_t* to, std::size_t count) {
        if (at > size || count > size - at) return false;
        for (std::size_t i = 0; i < count; ++i) to[i] = data[at + i];
        return true;
    };
    return parse_theme_pack(read, size, 0, out, error, header, end);
}

bool parse_theme_pack(const ThemePackReader& read, std::size_t size, std::size_t start, ThemePack& out,
                      std::string& error, const char* header, std::size_t* end) {
    out = ThemePack{};
    std::size_t at = start;
    // One text line from `at`, without its newline; false at the end. A
    // line is at most 1024 bytes, so one read of 1025 finds its end.
    constexpr std::size_t kMaxLine = 1024;
    std::uint8_t chunk[kMaxLine + 1];
    const auto line = [&](std::string& text) {
        if (at >= size) return false;
        const std::size_t count = std::min(sizeof(chunk), size - at);
        if (!read(at, chunk, count)) return false;
        std::size_t length = 0;
        while (length < count && chunk[length] != '\n') ++length;
        if (length >= count) return false;
        text.assign(reinterpret_cast<const char*>(chunk), length);
        at += length + 1;
        return true;
    };
    // Whether the byte at `where` is the newline a file's bytes end with.
    const auto newline_at = [&](std::size_t where) {
        std::uint8_t byte = 0;
        return where < size && read(where, &byte, 1) && byte == '\n';
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
            WordReader in(text.substr(7));
            RetiredTheme r;
            // As a stream: once the name is missing, the replacement is too.
            in.next(r.name);
            in.next(r.replacement);
            if (!theme_pack_name_ok(r.name, false) ||
                (r.replacement != "-" && !theme_pack_name_ok(r.replacement, false))) {
                error = "a retired theme with a bad name: " + text;
                return false;
            }
            if (r.replacement == "-") r.replacement.clear();
            for (std::string f; in.next(f);) {
                if (!theme_pack_name_ok(f, true)) {
                    error = "a retired theme's file with a bad name: " + f;
                    return false;
                }
                if (!try_push_back(r.files, std::move(f))) {
                    error = "not enough memory for a retired theme's file list";
                    return false;
                }
            }
            if (!try_push_back(out.retired, std::move(r))) {
                error = "not enough memory for the pack's retired themes";
                return false;
            }
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
            const bool apps = std::string(header) == kAppsPackHeader;
            if (!(apps ? apps_path_ok(f.path) : path_ok(f.path))) {
                error = "a file outside one theme's folder: " + f.path;
                return false;
            }
            f.size = static_cast<std::size_t>(std::stoul(count));
            f.offset = at;
            if (f.size > size - at || size - at - f.size < 1 || !newline_at(at + f.size)) {
                error = f.path + " is cut short";
                return false;
            }
            at += f.size + 1;
            if (!try_push_back(out.files, std::move(f))) {
                error = "not enough memory for the pack's file list";
                return false;
            }
            continue;
        }
        error = "a line the pack should not have: " + text.substr(0, 60);
        return false;
    }
    error = "the pack is cut short (no end line)";
    return false;
}

}  // namespace riftwii
