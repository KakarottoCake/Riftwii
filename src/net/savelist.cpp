// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/savelist.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>

#include "riftwii/checkedalloc.hpp"
#include "riftwii/emunand.hpp"
#include "riftwii/jsonreader.hpp"

namespace riftwii {
namespace {

bool upper_digit(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

template <std::size_t N>
bool read_text(JsonReader& reader, char (&field)[N], bool& valid) {
    if (reader.peek() != '"') {
        valid = false;
        return reader.skip_value(2);
    }
    std::string text;
    if (!reader.string(text)) return false;
    if (reader.last_overlong() || !json_clean_text(text, N)) {
        valid = false;
    } else {
        std::memcpy(field, text.c_str(), text.size() + 1);
    }
    return true;
}

bool read_size(JsonReader& reader, std::uint32_t& field, bool& valid) {
    const char next = reader.peek();
    if (next < '0' || next > '9') {
        valid = false;
        return reader.skip_value(2);
    }
    std::uint64_t number = 0;
    bool whole = false;
    if (!reader.number(number, whole)) return false;
    if (!whole) valid = false;
    else field = static_cast<std::uint32_t>(number);
    return true;
}

bool read_save(JsonReader& reader, SaveListing& save, bool& valid) {
    if (!reader.take('{')) return false;
    std::set<std::string> keys;
    char platform[16] = {}, format[16] = {};
    if (!reader.take('}')) {
        for (;;) {
            std::string key;
            if (!reader.string(key) || reader.last_overlong() || !reader.take(':')) return false;
            // Bound even unknown fields. There is no use for a record with
            // hundreds of extensions, or two competing values of one field.
            if (keys.size() >= 32 || !keys.insert(key).second) return false;
            bool good;
            if (key == "id") good = read_text(reader, save.id, valid);
            else if (key == "game_id") good = read_text(reader, save.game_id, valid);
            else if (key == "title") good = read_text(reader, save.title, valid);
            else if (key == "description") good = read_text(reader, save.description, valid);
            else if (key == "author") good = read_text(reader, save.author, valid);
            else if (key == "page") good = read_text(reader, save.page, valid);
            else if (key == "url") good = read_text(reader, save.url, valid);
            else if (key == "sha256") good = read_text(reader, save.sha256, valid);
            else if (key == "platform") good = read_text(reader, platform, valid);
            else if (key == "format") good = read_text(reader, format, valid);
            else if (key == "size") good = read_size(reader, save.size, valid);
            else if (key == "files") good = read_size(reader, save.files, valid);
            else if (key == "unpacked_size") good = read_size(reader, save.unpacked_size, valid);
            else good = reader.skip_value(2);
            if (!good) return false;
            if (reader.take(',')) continue;
            if (!reader.take('}')) return false;
            break;
        }
    }
    if (std::strcmp(platform, "wii") == 0) save.platform = SavePlatform::Wii;
    else if (std::strcmp(platform, "gamecube") == 0) save.platform = SavePlatform::GameCube;
    else valid = false;
    if (std::strcmp(format, "gci") == 0) save.format = SaveFormat::Gci;
    else if (std::strcmp(format, "wii-zip") == 0) save.format = SaveFormat::WiiZip;
    else if (std::strcmp(format, "data.bin") == 0) save.format = SaveFormat::DataBin;
    else valid = false;
    return true;
}

std::string lower_path(std::string path) {
    for (char& c : path) {
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }
    return path;
}

}  // namespace

bool save_game_id_ok(const std::string& id) {
    return id.size() == 6 && std::all_of(id.begin(), id.end(), upper_digit);
}

bool save_id_ok(const std::string& id) {
    if (id.empty() || id.size() >= sizeof(SaveListing::id)) return false;
    for (char c : id) {
        if (!(upper_digit(c) || (c >= 'a' && c <= 'z') || c == '-' || c == '_')) return false;
    }
    return true;
}

bool save_url_ok(const std::string& url) {
    if (url.size() >= sizeof(SaveListing::url) || url.compare(0, 8, "https://") != 0) return false;
    const std::size_t end = url.find('/', 8);
    if (end == std::string::npos || end == 8) return false;
    const std::string host = url.substr(8, end - 8);
    if (host.find('.') == std::string::npos || host.front() == '.' || host.back() == '.') return false;
    std::size_t start = 0;
    while (start < host.size()) {
        const std::size_t dot = host.find('.', start);
        const std::string label = host.substr(start, dot - start);
        if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-') return false;
        for (char c : label) {
            const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            if (!letter && (dot == std::string::npos || !((c >= '0' && c <= '9') || c == '-'))) return false;
        }
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    for (std::size_t i = end; i < url.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(url[i]);
        if (c <= 32 || c >= 127 || c == '\\' || c == '#' || c == '"' || c == '<' || c == '>') return false;
    }
    return true;
}

bool save_listing_ok(const SaveListing& save) {
    if (!save_id_ok(save.id) || !save_game_id_ok(save.game_id) || !save_url_ok(save.url) ||
        !save_url_ok(save.page) || !sha256_hex_ok(save.sha256) || save.size == 0 ||
        save.size > kSaveDownloadMaxBytes || save.title[0] == '\0' || save.author[0] == '\0' ||
        save.description[0] == '\0') return false;
    if (save.platform == SavePlatform::GameCube) {
        return save.format == SaveFormat::Gci && save.size >= 64 + 8192 &&
               save.size <= 64 + 2043u * 8192 && (save.size - 64) % 8192 == 0 &&
               save.files == 0 && save.unpacked_size == 0;
    }
    if (save.platform != SavePlatform::Wii) return false;
    if (save.format == SaveFormat::DataBin) return save.files == 0 && save.unpacked_size == 0;
    return save.format == SaveFormat::WiiZip && save.files > 0 && save.files <= kSaveMaxFiles &&
           save.unpacked_size > 0 && save.unpacked_size <= kSaveMaxUnpackedBytes;
}

bool parse_save_list(const char* text, std::size_t bytes, SaveListing* out, std::size_t capacity,
                     std::size_t& count, std::size_t& skipped, std::string& error) {
    count = 0;
    skipped = 0;
    error.clear();
    const auto bad = [&] {
        count = 0;  // A partial list is never shown after a later parse error.
        error = "the save list is not in a form this RiftWii reads";
        return false;
    };
    if (!text || bytes > kSaveListMaxBytes || (!out && capacity != 0)) return bad();
    capacity = std::min(capacity, kSaveListMaxEntries);
    JsonReader reader(text, bytes);
    if (!reader.take('{')) return bad();
    bool schema = false, saves = false;
    std::set<std::string> keys;
    if (!reader.take('}')) {
        for (;;) {
            std::string key;
            if (!reader.string(key) || reader.last_overlong() || !reader.take(':') ||
                keys.size() >= 32 || !keys.insert(key).second) return bad();
            if (key == "schema") {
                std::uint64_t value;
                bool whole;
                if (!reader.number(value, whole) || !whole || value != 1) return bad();
                schema = true;
            } else if (key == "saves") {
                if (!reader.take('[')) return bad();
                saves = true;
                std::size_t seen = 0;
                if (!reader.take(']')) {
                    for (;;) {
                        if (++seen > kSaveListMaxEntries) return bad();
                        SaveListing save;
                        bool valid = true;
                        if (!read_save(reader, save, valid)) return bad();
                        bool duplicate = false;
                        for (std::size_t i = 0; i < count; ++i) {
                            if (lower_path(out[i].id) == lower_path(save.id)) duplicate = true;
                        }
                        if (!valid || !save_listing_ok(save) || duplicate || count == capacity) ++skipped;
                        else out[count++] = save;
                        if (reader.take(',')) continue;
                        if (!reader.take(']')) return bad();
                        break;
                    }
                }
            } else if (!reader.skip_value(1)) return bad();
            if (reader.take(',')) continue;
            if (!reader.take('}')) return bad();
            break;
        }
    }
    if (!schema || !saves || !reader.at_end()) return bad();
    return true;
}

bool save_matches(const SaveListing& save, const std::string& game_id, SavePlatform platform) {
    return save.platform == platform && save_game_id_ok(game_id) && game_id == save.game_id;
}

std::string save_wii_title_path(const std::string& game_id) {
    if (!save_game_id_ok(game_id)) return {};
    char path[40];
    std::snprintf(path, sizeof(path), "/title/00010000/%02x%02x%02x%02x",
                  static_cast<unsigned char>(game_id[0]), static_cast<unsigned char>(game_id[1]),
                  static_cast<unsigned char>(game_id[2]), static_cast<unsigned char>(game_id[3]));
    return path;
}

bool save_wii_zip_path(const std::string& name, std::string& relative) {
    relative.clear();
    if (name.compare(0, 5, "data/") != 0 || name.size() <= 5) return false;
    const std::string candidate = name.substr(5);
    // Use the existing NAND rules as well as FAT rules. Only ASCII names
    // are accepted so Unicode case folding cannot hide an alias on FAT.
    for (unsigned char c : candidate) {
        if (c < 32 || c >= 127 || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') return false;
    }
    std::size_t from = 0;
    unsigned depth = 0;
    while (from < candidate.size()) {
        const std::size_t slash = candidate.find('/', from);
        const std::string part = candidate.substr(from, slash - from);
        if (part.empty() || part.front() == '.' || part.back() == '.' || part.back() == ' ' ||
            !emunand_file_name(part) || ++depth > 8) return false;
        if (slash == std::string::npos) break;
        from = slash + 1;
        if (from == candidate.size()) return false;
    }
    // All disc title paths have the same width. Check the NAND path limit
    // without inventing a game ID or treating title zero as an actual game.
    constexpr std::size_t prefix_bytes = sizeof("/title/00010000/00000000/data/") - 1;
    if (prefix_bytes + candidate.size() >= 64) return false;
    relative = candidate;
    return true;
}

bool save_plan_wii_zip(const SaveListing& save, const std::vector<ZipEntry>& entries,
                       std::vector<std::size_t>& files, std::string& error) {
    files.clear();
    const auto bad = [&] {
        files.clear();
        error = "the save ZIP has unsafe paths, conflicting files or different sizes than its listing";
        return false;
    };
    if (save.format != SaveFormat::WiiZip || !save_listing_ok(save) || entries.size() > kSaveMaxFiles * 2) return bad();
    if (!try_reserve(files, save.files)) {
        error = "there is not enough memory to check the save ZIP";
        return false;
    }
    std::set<std::string> names, file_names;
    std::uint64_t total = 0;
    bool banner = false;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const ZipEntry& entry = entries[i];
        std::string name = entry.name;
        if (entry.directory) {
            if (name == "data/") {
                if (!names.insert("data").second) return bad();
                continue;
            }
            if (name.empty() || name.back() != '/' || entry.unpacked_size != 0) return bad();
            name.pop_back();
        }
        std::string relative;
        if (!save_wii_zip_path(name, relative)) return bad();
        const std::string folded = lower_path(name);
        if (!names.insert(folded).second) return bad();
        if (!entry.directory) {
            if (entry.method != 0 && entry.method != 8) return bad();
            file_names.insert(folded);
            total += entry.unpacked_size;
            if (total > save.unpacked_size || files.size() >= save.files) return bad();
            files.push_back(i);
            if (relative == "banner.bin") banner = entry.unpacked_size >= 4;
        }
    }
    // Refuse a file also used as an implied or explicit parent directory.
    for (const std::string& name : names) {
        for (std::size_t slash = name.find('/'); slash != std::string::npos; slash = name.find('/', slash + 1)) {
            if (file_names.count(name.substr(0, slash))) return bad();
        }
    }
    if (!banner || total != save.unpacked_size || files.size() != save.files) return bad();
    error.clear();
    return true;
}

}  // namespace riftwii
