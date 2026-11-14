// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/homelist.hpp"

#include "riftwii/checkedalloc.hpp"
#include "riftwii/imagevolume.hpp"

namespace riftwii {
namespace {

// Room kept for the last line (\"end \" and a count of up to 20 digits).
constexpr std::size_t kFooterRoom = 32;

constexpr const char* kFirstLine = "homelist 1";
constexpr const char* kLastLineStart = "end ";
constexpr std::size_t kFieldsInLine = 7;

bool has_control_character(const std::string& text) {
    for (const char c : text) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7F) return true;
    }
    return false;
}

bool valid_id(const std::string& id) {
    if (id.empty() || id.size() > 6) return false;
    for (const char c : id) {
        const unsigned char byte = static_cast<unsigned char>(c);
        // Printable ASCII, and nothing that separates folders: the ID names
        // files elsewhere (a cover, a game's choices).
        if (byte <= 0x20 || byte >= 0x7F || c == '/' || c == '\\') return false;
    }
    return true;
}

bool valid_path(const std::string& path) {
    return !path.empty() && path.size() <= kHomeListMaxPath && !has_control_character(path);
}

bool path_on_sd(const std::string& path) { return path.compare(0, 4, "sd:/") == 0; }

// The decimal number `text` is, when it is one of at most `max`.
bool parse_number(const std::string& text, std::size_t max, std::size_t& out) {
    if (text.empty() || text.size() > 9) return false;
    std::size_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<std::size_t>(c - '0');
    }
    if (value > max) return false;
    // "007" is not how a number is written here.
    if (text.size() > 1 && text[0] == '0') return false;
    out = value;
    return true;
}

bool parse_marks(const std::string& marks, RememberedGame& game) {
    if (marks == "-") return true;
    if (marks.empty()) return false;
    for (const char mark : marks) {
        if (mark == 'g' && !game.gamecube) game.gamecube = true;
        else if (mark == 'm' && !game.has_mods) game.has_mods = true;
        else return false;  // an unknown mark, or one given twice
    }
    return true;
}

bool parse_game_line(const std::string& line, RememberedGame& game) {
    std::string fields[kFieldsInLine];
    std::size_t field = 0;
    std::size_t start = 0;
    for (;;) {
        const std::size_t tab = line.find('\t', start);
        if (field == kFieldsInLine) return false;  // more fields than a game has
        fields[field++] = line.substr(start, tab == std::string::npos ? std::string::npos : tab - start);
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    if (field != kFieldsInLine) return false;

    game = RememberedGame{};
    if (fields[0] == "usb") game.usb = true;
    else if (fields[0] != "sd") return false;
    game.id = fields[1];
    std::size_t disc_number = 0;
    if (!parse_number(fields[2], 255, disc_number)) return false;
    game.disc_number = static_cast<std::uint8_t>(disc_number);
    if (!parse_marks(fields[3], game)) return false;
    game.name = fields[4];
    game.path = fields[5];
    game.patch_source = fields[6];
    return remembered_game_valid(game);
}

}  // namespace

bool remembered_game_valid(const RememberedGame& game) {
    if (!valid_id(game.id)) return false;
    if (game.name.empty() || game.name.size() > kHomeListMaxName || has_control_character(game.name)) return false;
    if (!valid_path(game.path)) return false;
    if (game.usb ? !is_usb_path(game.path) : !path_on_sd(game.path)) return false;
    // The original a patch is made from may be on either drive.
    if (!game.patch_source.empty() &&
        (!valid_path(game.patch_source) || (!is_usb_path(game.patch_source) && !path_on_sd(game.patch_source)))) {
        return false;
    }
    return true;
}

std::string serialize_home_list(const std::vector<RememberedGame>& games) {
    std::string text =
        "# RiftWii: the games Home found the last time it read the drives. Home shows them\n"
        "# at once at the next start, while it reads the drives again. Safe to delete.\n";
    text += kFirstLine;
    text += '\n';
    std::size_t written = 0;
    for (const RememberedGame& game : games) {
        if (written == kHomeListMaxGames) break;
        if (!remembered_game_valid(game)) continue;
        std::string line;
        std::string marks;
        if (game.gamecube) marks += 'g';
        if (game.has_mods) marks += 'm';
        if (marks.empty()) marks = "-";
        line += game.usb ? "usb" : "sd";
        line += '\t';
        line += game.id;
        line += '\t';
        line += std::to_string(static_cast<unsigned>(game.disc_number));
        line += '\t';
        line += marks;
        line += '\t';
        line += game.name;
        line += '\t';
        line += game.path;
        line += '\t';
        line += game.patch_source;
        line += '\n';
        // The file must stay readable: a record that would take it past
        // the byte limit, footer included, is not written, nor any after it.
        if (text.size() + line.size() + kFooterRoom > kHomeListMaxBytes) break;
        text += line;
        ++written;
    }
    text += kLastLineStart;
    text += std::to_string(written);
    text += '\n';
    return text;
}

bool parse_home_list(const std::string& text, std::vector<RememberedGame>& out) {
    out.clear();
    if (text.size() > kHomeListMaxBytes) return false;
    std::vector<RememberedGame> games;
    bool first_line_seen = false;
    bool last_line_seen = false;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (last_line_seen) return false;  // something after the count
        if (!first_line_seen) {
            if (line != kFirstLine) return false;
            first_line_seen = true;
            continue;
        }
        if (line.compare(0, 4, kLastLineStart) == 0 && line.find('\t') == std::string::npos) {
            std::size_t counted = 0;
            if (!parse_number(line.substr(4), kHomeListMaxGames, counted) || counted != games.size()) return false;
            last_line_seen = true;
            continue;
        }
        RememberedGame game;
        if (games.size() == kHomeListMaxGames || !parse_game_line(line, game)) return false;
        if (!try_push_back(games, std::move(game))) return false;
    }
    if (!last_line_seen) return false;
    out.swap(games);
    return true;
}

}  // namespace riftwii
