// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gametdb.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "pugixml.hpp"

namespace riftwii {
namespace {

// Limits on what one game may bring (network data).
constexpr std::size_t kMaxText = 200;       // a name, a genre, a rating
constexpr std::size_t kMaxSynopsis = 3000;  // bytes of UTF-8
constexpr std::size_t kMaxListItems = 24;   // genres, controls, features
constexpr int kMaxPlayers = 99;

bool is_game_id(const std::string& id) {
    if (id.size() != 4 && id.size() != 6) return false;
    for (char c : id) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!ok) return false;
    }
    return true;
}

// Text as data: control characters dropped (a newline kept where
// `newlines`), spaces trimmed, cut at `limit` bytes without splitting a
// UTF-8 character.
std::string clean(const std::string& in, std::size_t limit, bool newlines = false) {
    std::string out;
    out.reserve(std::min(in.size(), limit));
    for (char c : in) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 32 && !(newlines && c == '\n')) {
            if (u == '\t' || u == '\r') out += ' ';
            continue;
        }
        out += c;
    }
    const std::size_t first = out.find_first_not_of(" \n");
    if (first == std::string::npos) return "";
    out = out.substr(first, out.find_last_not_of(" \n") - first + 1);
    if (out.size() > limit) {
        std::size_t cut = limit;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out.resize(cut);
    }
    return out;
}

int bounded(long value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return static_cast<int>(value);
}

std::vector<std::string> split_list(const std::string& text) {
    std::vector<std::string> out;
    std::string item;
    std::istringstream in(text);
    while (std::getline(in, item, ',') && out.size() < kMaxListItems) {
        item = clean(item, kMaxText);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

std::string join(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& s : items) out += (out.empty() ? "" : ",") + s;
    return out;
}

// One line of the cache: backslashes and line breaks written as \\ and \n.
std::string escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

std::string unescape(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            out += s[i];
            continue;
        }
        out += s[i + 1] == 'n' ? '\n' : s[i + 1];
        ++i;
    }
    return out;
}

// The text between <id> and </id> in a raw block, without parsing it.
std::string raw_id(const std::string& block) {
    const std::size_t open = block.find("<id>");
    if (open == std::string::npos) return "";
    const std::size_t close = block.find("</id>", open);
    if (close == std::string::npos || close - open > 16) return "";
    return block.substr(open + 4, close - open - 4);
}

}  // namespace

WiitdbScanner::WiitdbScanner(std::set<std::string> wanted, std::string lang)
    : wanted_(std::move(wanted)), lang_(std::move(lang)) {}

void WiitdbScanner::feed(const char* data, std::size_t size) {
    buffer_.append(data, size);
    constexpr const char* kOpen = "<game ";
    constexpr const char* kClose = "</game>";
    for (;;) {
        if (!in_game_) {
            const std::size_t at = buffer_.find(kOpen);
            if (at == std::string::npos) {
                // Keep a tail that may be the start of the next "<game ".
                if (buffer_.size() > 8) buffer_.erase(0, buffer_.size() - 8);
                return;
            }
            buffer_.erase(0, at);
            in_game_ = true;
        }
        const std::size_t end = buffer_.find(kClose);
        if (end == std::string::npos) {
            if (buffer_.size() > kMaxBlock) {
                // Too long to be a game: dropped, then its end looked for.
                ++skipped_;
                ++blocks_;
                buffer_.erase(0, buffer_.size() - 8);
                in_game_ = false;
                // The rest of that block holds no "<game ", so the scan
                // simply resumes at the next one.
            }
            return;
        }
        const std::size_t length = end + std::strlen(kClose);
        ++blocks_;
        if (length > kMaxBlock) ++skipped_;
        else take_block(buffer_.substr(0, length));
        buffer_.erase(0, length);
        in_game_ = false;
    }
}

void WiitdbScanner::take_block(const std::string& block) {
    const std::string id = raw_id(block);
    if (!wanted_.count(id) || found_.count(id)) return;
    GameInfo info;
    if (parse_wiitdb_game(block, lang_, info)) found_[info.id] = std::move(info);
    else ++skipped_;
}

bool parse_wiitdb_game(const std::string& block, const std::string& lang, GameInfo& out) {
    pugi::xml_document doc;
    if (!doc.load_buffer(block.data(), block.size())) return false;
    const pugi::xml_node game = doc.child("game");
    if (!game) return false;
    GameInfo info;
    info.id = clean(game.child_value("id"), 8);
    if (!is_game_id(info.id)) return false;
    info.developer = clean(game.child_value("developer"), kMaxText);
    info.publisher = clean(game.child_value("publisher"), kMaxText);
    info.genres = split_list(game.child_value("genre"));
    const pugi::xml_node date = game.child("date");
    info.year = bounded(date.attribute("year").as_int(), 0, 2100);
    info.month = bounded(date.attribute("month").as_int(), 0, 12);
    info.day = bounded(date.attribute("day").as_int(), 0, 31);
    const pugi::xml_node rating = game.child("rating");
    info.rating_system = clean(rating.attribute("type").as_string(), 16);
    info.rating = clean(rating.attribute("value").as_string(), 16);
    const pugi::xml_node input = game.child("input");
    info.players = bounded(input.attribute("players").as_int(), 0, kMaxPlayers);
    for (const pugi::xml_node control : input.children("control")) {
        if (info.controls.size() >= kMaxListItems) break;
        GameControl c;
        c.type = clean(control.attribute("type").as_string(), 32);
        c.required = control.attribute("required").as_bool();
        if (!c.type.empty()) info.controls.push_back(c);
    }
    const pugi::xml_node wifi = game.child("wi-fi");
    info.online_players = bounded(wifi.attribute("players").as_int(), 0, kMaxPlayers);
    for (const pugi::xml_node feature : wifi.children("feature")) {
        if (info.online_features.size() >= kMaxListItems) break;
        const std::string f = clean(feature.child_value(), 32);
        if (!f.empty()) info.online_features.push_back(f);
    }
    // The synopsis in the wanted language, else the English one, else
    // the first there is.
    std::string english, first;
    for (const pugi::xml_node locale : game.children("locale")) {
        const std::string code = locale.attribute("lang").as_string();
        const std::string text = clean(locale.child_value("synopsis"), kMaxSynopsis, true);
        if (code == lang && !text.empty()) info.synopsis = text;
        if (code == "EN") english = text;
        if (first.empty()) first = text;
    }
    if (info.synopsis.empty()) info.synopsis = english.empty() ? first : english;
    out = std::move(info);
    return true;
}

std::string serialize_game_infos(const std::map<std::string, GameInfo>& games) {
    std::string out = "# GameTDB game details, made by RiftWii from wiitdb.zip\n";
    for (const auto& [id, g] : games) {
        out += "[" + id + "]\n";
        if (!g.developer.empty()) out += "developer=" + escape(g.developer) + "\n";
        if (!g.publisher.empty()) out += "publisher=" + escape(g.publisher) + "\n";
        if (!g.genres.empty()) out += "genres=" + escape(join(g.genres)) + "\n";
        if (g.year) out += "date=" + std::to_string(g.year) + "-" + std::to_string(g.month) + "-" + std::to_string(g.day) + "\n";
        if (!g.rating.empty()) out += "rating=" + escape(g.rating_system) + " " + escape(g.rating) + "\n";
        if (g.players) out += "players=" + std::to_string(g.players) + "\n";
        if (!g.controls.empty()) {
            std::string list;
            for (const GameControl& c : g.controls) list += (list.empty() ? "" : ",") + c.type + (c.required ? ":1" : ":0");
            out += "controls=" + escape(list) + "\n";
        }
        if (g.online_players) out += "online=" + std::to_string(g.online_players) + "\n";
        if (!g.online_features.empty()) out += "features=" + escape(join(g.online_features)) + "\n";
        if (!g.synopsis.empty()) out += "synopsis=" + escape(g.synopsis) + "\n";
    }
    return out;
}

bool parse_game_infos(const std::string& text, std::map<std::string, GameInfo>& out) {
    out.clear();
    std::istringstream in(text);
    std::string line;
    GameInfo* current = nullptr;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line[0] == '[') {
            const std::string id = line.size() > 2 && line.back() == ']' ? line.substr(1, line.size() - 2) : "";
            current = is_game_id(id) ? &out[id] : nullptr;
            if (current) current->id = id;
            continue;
        }
        const std::size_t eq = line.find('=');
        if (!current || eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = unescape(line.substr(eq + 1));
        if (key == "developer") current->developer = clean(value, kMaxText);
        else if (key == "publisher") current->publisher = clean(value, kMaxText);
        else if (key == "genres") current->genres = split_list(value);
        else if (key == "date") {
            current->year = bounded(std::strtol(value.c_str(), nullptr, 10), 0, 2100);
            const std::size_t a = value.find('-'), b = value.find('-', a == std::string::npos ? a : a + 1);
            if (a != std::string::npos) current->month = bounded(std::strtol(value.c_str() + a + 1, nullptr, 10), 0, 12);
            if (b != std::string::npos) current->day = bounded(std::strtol(value.c_str() + b + 1, nullptr, 10), 0, 31);
        } else if (key == "rating") {
            const std::size_t space = value.find(' ');
            current->rating_system = clean(value.substr(0, space), 16);
            current->rating = space == std::string::npos ? "" : clean(value.substr(space + 1), 16);
        } else if (key == "players") current->players = bounded(std::strtol(value.c_str(), nullptr, 10), 0, kMaxPlayers);
        else if (key == "online") current->online_players = bounded(std::strtol(value.c_str(), nullptr, 10), 0, kMaxPlayers);
        else if (key == "features") current->online_features = split_list(value);
        else if (key == "synopsis") current->synopsis = clean(value, kMaxSynopsis, true);
        else if (key == "controls") {
            current->controls.clear();
            for (const std::string& item : split_list(value)) {
                const std::size_t colon = item.rfind(':');
                GameControl c;
                c.type = clean(item.substr(0, colon), 32);
                c.required = colon != std::string::npos && item.substr(colon + 1) == "1";
                if (!c.type.empty()) current->controls.push_back(c);
            }
        }
    }
    return true;
}

std::string gametdb_locale(const std::string& menu_language) {
    static const char* const kCodes[][2] = {{"ja", "JA"}, {"es", "ES"}, {"pt", "PT"}, {"it", "IT"},
                                            {"fr", "FR"}, {"ko", "KO"}, {"de", "DE"}, {"nl", "NL"}};
    for (const auto& pair : kCodes)
        if (menu_language == pair[0]) return pair[1];
    return "EN";
}

std::string plain_punctuation(const std::string& text) {
    static const char* const kSwaps[][2] = {
        {"\xE2\x80\x98", "'"},  {"\xE2\x80\x99", "'"},   {"\xE2\x80\x9C", "\""}, {"\xE2\x80\x9D", "\""},
        {"\xE2\x80\x93", "-"},  {"\xE2\x80\x94", " - "}, {"\xE2\x80\xA2", "-"},  {"\xE2\x80\xA6", "..."},
        {"\xC2\xA0", " "},      {"\xC2\xB7", "-"},
    };
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        bool swapped = false;
        for (const auto& swap : kSwaps) {
            const std::size_t length = std::strlen(swap[0]);
            if (text.compare(i, length, swap[0]) == 0) {
                out += swap[1];
                i += length;
                swapped = true;
                break;
            }
        }
        if (!swapped) out += text[i++];
    }
    return out;
}

namespace {

// The code point of the UTF-8 character at `at` and its length in bytes; a
// stray byte is taken as one character of its own.
std::uint32_t next_char(const std::string& s, std::size_t at, std::size_t& length) {
    const unsigned char lead = static_cast<unsigned char>(s[at]);
    length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 1;
    if (at + length > s.size()) length = 1;
    if (length == 1) return lead;
    std::uint32_t code = lead & (0x7F >> length);
    for (std::size_t k = 1; k < length; ++k) {
        const unsigned char follow = static_cast<unsigned char>(s[at + k]);
        if ((follow & 0xC0) != 0x80) {
            length = 1;
            return lead;
        }
        code = (code << 6) | (follow & 0x3F);
    }
    return code;
}

// Characters drawn about twice as wide as a Latin letter.
bool is_wide(std::uint32_t code) {
    return (code >= 0x1100 && code <= 0x115F) || (code >= 0x2E80 && code <= 0xA4CF) ||
           (code >= 0xAC00 && code <= 0xD7A3) || (code >= 0xF900 && code <= 0xFAFF) ||
           (code >= 0xFF00 && code <= 0xFF60) || (code >= 0xFFE0 && code <= 0xFFE6);
}

void wrap_paragraph(const std::string& text, std::size_t columns, std::vector<std::string>& out) {
    std::string line;
    std::size_t used = 0;
    std::size_t last_space = std::string::npos;  // in `line`
    std::size_t used_at_space = 0;
    for (std::size_t at = 0; at < text.size();) {
        std::size_t length = 1;
        const std::uint32_t code = next_char(text, at, length);
        const std::size_t width = is_wide(code) ? 2 : 1;
        if (code == ' ' && line.empty()) {  // no line starts with a space
            at += length;
            continue;
        }
        if (used + width > columns && !line.empty()) {
            if (code != ' ' && last_space != std::string::npos) {
                // The word so far moves down with this character.
                std::string rest = line.substr(last_space + 1);
                line.resize(last_space);
                out.push_back(line);
                line = rest;
                used -= used_at_space + 1;
            } else {
                while (!line.empty() && line.back() == ' ') line.pop_back();
                out.push_back(line);
                line.clear();
                used = 0;
            }
            last_space = std::string::npos;
            if (code == ' ') {
                at += length;
                continue;
            }
        }
        if (code == ' ') {
            last_space = line.size();
            used_at_space = used;
        }
        line.append(text, at, length);
        used += width;
        at += length;
    }
    while (!line.empty() && line.back() == ' ') line.pop_back();
    if (!line.empty()) out.push_back(line);
}

}  // namespace

std::vector<std::string> wrap_columns(const std::string& text, std::size_t columns) {
    std::vector<std::string> out;
    if (columns == 0) return out;
    std::size_t start = 0;
    bool blank_kept = true;  // none before the first line
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const std::string paragraph = text.substr(start, end - start);
        if (paragraph.find_first_not_of(' ') == std::string::npos) {
            if (!blank_kept) out.emplace_back();
            blank_kept = true;
        } else {
            wrap_paragraph(paragraph, columns, out);
            blank_kept = false;
        }
        start = end + 1;
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

}  // namespace riftwii
