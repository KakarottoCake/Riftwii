// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/modcover.hpp"

#include <cctype>

#include "pugixml.hpp"

namespace riftwii {

namespace {

bool is_custom_id(const std::string& id) {
    if (id.size() != 6) return false;
    for (const char c : id)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    return true;
}

// A title as the file keeps it: tabs, line breaks and other control
// characters made spaces, the ends trimmed, cut on a whole UTF-8 character.
std::string clean_title(const std::string& in) {
    std::string out;
    for (const char c : in) out += static_cast<unsigned char>(c) < 32 ? ' ' : c;
    const std::size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    out = out.substr(first, out.find_last_not_of(' ') - first + 1);
    if (out.size() > kMaxCustomTitleBytes) {
        std::size_t cut = kMaxCustomTitleBytes;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out.resize(cut);
    }
    return out;
}

}  // namespace

std::string custom_prefix(const std::string& stock_id) {
    return is_custom_id(stock_id) ? stock_id.substr(0, 4) : std::string();
}

std::set<std::string> custom_prefixes(const std::set<std::string>& ids) {
    std::set<std::string> out;
    for (const std::string& id : ids) {
        const std::string prefix = custom_prefix(id);
        if (!prefix.empty()) out.insert(prefix);
    }
    return out;
}

bool parse_wiitdb_custom(const std::string& block, CustomGame& out) {
    pugi::xml_document doc;
    if (!doc.load_buffer(block.data(), block.size())) return false;
    const pugi::xml_node game = doc.child("game");
    if (!game) return false;
    std::string type = clean_title(game.child_value("type"));
    for (char& c : type) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (type != "CUSTOM") return false;
    CustomGame g;
    g.id = clean_title(game.child_value("id"));
    if (!is_custom_id(g.id)) return false;
    for (const pugi::xml_node locale : game.children("locale"))
        if (g.title.empty() && std::string(locale.attribute("lang").as_string()) == "EN")
            g.title = clean_title(locale.child_value("title"));
    if (g.title.empty()) g.title = clean_title(game.attribute("name").as_string());
    if (g.title.empty()) return false;
    out = std::move(g);
    return true;
}

std::string serialize_custom_games(const std::vector<CustomGame>& games, std::size_t* written_out) {
    std::string out = "# GameTDB's custom games (mods) for the player's games, made by RiftWii from wiitdb.xml\n";
    std::size_t written = 0;
    for (const CustomGame& g : games) {
        if (written >= kMaxCustomGames) break;
        const std::string title = clean_title(g.title);
        if (!is_custom_id(g.id) || title.empty()) continue;
        const std::string line = g.id + "\t" + title + "\n";
        if (out.size() + line.size() > kMaxCustomGamesBytes) break;
        out += line;
        ++written;
    }
    if (written_out) *written_out = written;
    return out;
}

std::vector<CustomGame> parse_custom_games(const std::string& text) {
    std::vector<CustomGame> out;
    std::size_t at = 0;
    while (at < text.size() && out.size() < kMaxCustomGames) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        CustomGame g;
        g.id = line.substr(0, tab);
        g.title = clean_title(line.substr(tab + 1));
        if (is_custom_id(g.id) && !g.title.empty()) out.push_back(std::move(g));
    }
    return out;
}

std::string mod_title_words(const std::string& text) {
    std::string out;
    bool gap = false;
    for (const unsigned char c : text) {
        const bool kept = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c >= 0x80;
        if (c >= 'A' && c <= 'Z') {
            if (gap && !out.empty()) out += ' ';
            gap = false;
            out += static_cast<char>(c - 'A' + 'a');
        } else if (kept) {
            if (gap && !out.empty()) out += ' ';
            gap = false;
            out += static_cast<char>(c);
        } else {
            gap = true;
        }
    }
    return out;
}

bool title_has_mod_name(const std::string& title, const std::string& name) {
    const std::string words = " " + mod_title_words(title) + " ";
    const std::string phrase = mod_title_words(name);
    if (phrase.empty()) return false;
    return words.find(" " + phrase + " ") != std::string::npos;
}

std::string mod_cover_id(const std::vector<CustomGame>& customs, const std::string& stock_id,
                         const std::string& name) {
    if (stock_id.size() < 4) return {};
    const std::string prefix = stock_id.substr(0, 4);
    std::string found;
    bool stock_matches = false;
    for (const CustomGame& g : customs) {
        if (g.id.size() != 6 || g.id.compare(0, 4, prefix) != 0) continue;
        if (!title_has_mod_name(g.title, name)) continue;
        // The tile's own game (itself a mod, installed as a game of its own)
        // is no cover to take, but it is a match: with another, two match.
        if (g.id == stock_id) {
            stock_matches = true;
            continue;
        }
        if (!found.empty() && found != g.id) return {};  // more than one: never a guess
        found = g.id;
    }
    return stock_matches ? std::string() : found;
}

}  // namespace riftwii
