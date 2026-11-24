// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/themelist.hpp"

#include <cctype>
#include <cstring>

#include "riftwii/jsonreader.hpp"
#include "riftwii/themepack.hpp"

namespace riftwii {
namespace {

// The fields of one theme object, as read, before they are checked.
struct RawTheme {
    std::string folder, name, author, license, sha256, zip, preview;
    std::uint64_t size = 0, files = 0, unpacked_size = 0;
    bool have_size = false, have_files = false, have_unpacked = false, has_music = false;
    bool malformed = false;  // a field of the wrong kind or too long
};

// FAT does not tell "Indigo" from "indigo": they are one folder.
bool same_folder(const std::string& a, const char* b) {
    std::size_t i = 0;
    for (; i < a.size() && b[i] != '\0'; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return i == a.size() && b[i] == '\0';
}

bool read_theme(JsonReader& r, RawTheme& t, bool& fatal) {
    if (!r.take('{')) return false;
    if (r.take('}')) return true;
    for (;;) {
        std::string key;
        if (!r.string(key) || !r.take(':')) return false;
        const char next = r.peek();
        const auto text = [&](std::string& into) {
            if (next != '"') {
                t.malformed = true;
                return r.skip_value(1);
            }
            if (!r.string(into)) return false;
            if (r.last_overlong()) t.malformed = true;
            return true;
        };
        const auto whole = [&](std::uint64_t& into, bool& have) {
            std::uint64_t value;
            bool ok;
            if (next == '"' || next == '{' || next == '[' || next == 't' || next == 'f' || next == 'n') {
                t.malformed = true;
                return r.skip_value(1);
            }
            if (!r.number(value, ok)) return false;
            if (!ok) t.malformed = true;
            into = value;
            have = ok;
            return true;
        };
        bool good;
        if (key == "folder") good = text(t.folder);
        else if (key == "name") good = text(t.name);
        else if (key == "author") good = text(t.author);
        else if (key == "license") good = text(t.license);
        else if (key == "sha256") good = text(t.sha256);
        else if (key == "zip") good = text(t.zip);
        else if (key == "preview") good = text(t.preview);
        else if (key == "size") good = whole(t.size, t.have_size);
        else if (key == "files") good = whole(t.files, t.have_files);
        else if (key == "unpacked_size") good = whole(t.unpacked_size, t.have_unpacked);
        else if (key == "has_music") {
            if (r.word("true")) {
                t.has_music = true;
                good = true;
            } else if (r.word("false")) {
                good = true;
            } else {
                t.malformed = true;
                good = r.skip_value(1);
            }
        } else good = r.skip_value(1);
        if (!good) {
            fatal = true;  // a string too long or broken text: the reader cannot go on
            return false;
        }
        if (r.take(',')) continue;
        return r.take('}');
    }
}

void copy_field(char* to, std::size_t room, const std::string& from) { std::memcpy(to, from.c_str(), from.size() + 1 <= room ? from.size() + 1 : 0); }

}  // namespace

bool theme_folder_ok(const std::string& folder) { return folder.size() < 48 && theme_pack_name_ok(folder, false); }

bool theme_url_ok(const std::string& url) {
    const std::string prefix = kThemeSiteUrl;
    if (url.size() <= prefix.size() || url.size() >= 200 || url.compare(0, prefix.size(), prefix) != 0) return false;
    for (std::size_t i = prefix.size(); i < url.size(); ++i) {
        const char c = url[i];
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                           c == '_' || c == '.' || c == '~' || c == '/' || c == '%' || c == '+';
        if (!plain) return false;
    }
    const std::string path = url.substr(prefix.size());
    // No part of the path climbs: "..", whole or at either end.
    if (path == ".." || path.compare(0, 3, "../") == 0 || path.find("/../") != std::string::npos ||
        (path.size() >= 3 && path.compare(path.size() - 3, 3, "/..") == 0))
        return false;
    return true;
}

bool parse_theme_list(const char* text, std::size_t text_size, ThemeListing* out, std::size_t max,
                      std::size_t& count, std::size_t& skipped, std::string& error) {
    count = 0;
    skipped = 0;
    if (text_size > kThemeListMaxBytes) {
        error = "the theme list is bigger than RiftWii takes (256 KB)";
        return false;
    }
    if (max > kThemeListMaxThemes) max = kThemeListMaxThemes;
    const auto bad = [&] {
        error = "the theme list is not in a form RiftWii reads";
        return false;
    };
    JsonReader r(text, text_size);
    if (!r.take('{')) return bad();
    bool have_schema = false, have_themes = false;
    if (!r.take('}')) {
        for (;;) {
            std::string key;
            if (!r.string(key) || !r.take(':')) return bad();
            if (key == "schema") {
                std::uint64_t value;
                bool ok;
                if (!r.number(value, ok) || !ok || value != 1) {
                    error = "the theme list is from a newer RiftWii than this one (schema is not 1)";
                    return false;
                }
                have_schema = true;
            } else if (key == "themes") {
                if (!r.take('[')) return bad();
                have_themes = true;
                count = 0;
                skipped = 0;
                if (!r.take(']')) {
                    for (;;) {
                        RawTheme t;
                        bool fatal = false;
                        if (r.peek() != '{') return bad();
                        if (!read_theme(r, t, fatal)) return bad();
                        ThemeListing listing;
                        std::memset(&listing, 0, sizeof(listing));
                        const bool fits = json_clean_text(t.name, sizeof(listing.name)) &&
                                          json_clean_text(t.author, sizeof(listing.author)) &&
                                          json_clean_text(t.license, sizeof(listing.license));
                        const bool good = !t.malformed && fits && theme_folder_ok(t.folder) && sha256_hex_ok(t.sha256) &&
                                          theme_url_ok(t.zip) && theme_url_ok(t.preview) && t.have_size &&
                                          t.have_files && t.have_unpacked && t.size > 0 && t.size <= kThemeZipMaxBytes &&
                                          t.files > 0 && t.files <= kThemeMaxFiles && t.unpacked_size > 0 &&
                                          t.unpacked_size <= kThemeMaxUnpackedBytes;
                        if (!good) {
                            ++skipped;
                        } else if (count >= max) {
                            ++skipped;
                        } else {
                            copy_field(listing.folder, sizeof(listing.folder), t.folder);
                            copy_field(listing.name, sizeof(listing.name), t.name);
                            copy_field(listing.author, sizeof(listing.author), t.author);
                            copy_field(listing.license, sizeof(listing.license), t.license);
                            copy_field(listing.sha256, sizeof(listing.sha256), t.sha256);
                            copy_field(listing.zip, sizeof(listing.zip), t.zip);
                            copy_field(listing.preview, sizeof(listing.preview), t.preview);
                            listing.size = static_cast<std::uint32_t>(t.size);
                            listing.files = static_cast<std::uint32_t>(t.files);
                            listing.unpacked_size = static_cast<std::uint32_t>(t.unpacked_size);
                            listing.has_music = t.has_music;
                            // One folder twice: the first stands.
                            bool again = false;
                            for (std::size_t i = 0; i < count; ++i) {
                                if (same_folder(t.folder, out[i].folder)) again = true;
                            }
                            if (again) ++skipped;
                            else out[count++] = listing;
                        }
                        if (r.take(',')) continue;
                        if (!r.take(']')) return bad();
                        break;
                    }
                }
            } else if (!r.skip_value(1)) {
                return bad();
            }
            if (r.take(',')) continue;
            if (!r.take('}')) return bad();
            break;
        }
    }
    if (!r.at_end()) return bad();
    if (!have_schema) {
        error = "the theme list says nothing about its schema";
        return false;
    }
    if (!have_themes) return bad();
    return true;
}

namespace {

// One line of the records: "<folder> <sha256>".
bool record_line(const std::string& line, std::string& folder, std::string& sha256) {
    const std::size_t space = line.find(' ');
    if (space == std::string::npos) return false;
    folder = line.substr(0, space);
    sha256 = line.substr(space + 1);
    return theme_folder_ok(folder) && sha256_hex_ok(sha256);
}

}  // namespace

bool parse_theme_list(const std::string& text, ThemeListing* out, std::size_t max, std::size_t& count,
                      std::size_t& skipped, std::string& error) {
    return parse_theme_list(text.data(), text.size(), out, max, count, skipped, error);
}

ThemeInstall theme_install_state(bool folder_there, bool recorded, const std::string& recorded_sha,
                                const std::string& listed_sha) {
    if (!folder_there) return ThemeInstall::No;
    if (!recorded) return ThemeInstall::Other;
    return recorded_sha == listed_sha ? ThemeInstall::Current : ThemeInstall::Update;
}

std::string theme_record_without(const std::string& records, const std::string& folder) {
    std::string out;
    std::size_t at = 0;
    while (at < records.size()) {
        std::size_t end = records.find('\n', at);
        if (end == std::string::npos) end = records.size();
        std::string line = records.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string name, sha;
        if (!record_line(line, name, sha) || name == folder) continue;
        out += line + "\n";
    }
    return out;
}

std::string theme_record_text(const std::string& records, const std::string& folder, const std::string& sha256) {
    return theme_record_without(records, folder) + folder + " " + sha256 + "\n";
}

bool theme_record_find(const std::string& records, const std::string& folder, std::string& sha256) {
    std::size_t at = 0;
    while (at < records.size()) {
        std::size_t end = records.find('\n', at);
        if (end == std::string::npos) end = records.size();
        std::string line = records.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string name, sha;
        if (record_line(line, name, sha) && name == folder) {
            sha256 = sha;
            return true;
        }
    }
    return false;
}

}  // namespace riftwii
