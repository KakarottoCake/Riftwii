// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gameconfig.hpp"

#include <cctype>
#include <sstream>

namespace riftwii {
namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A 32-bit hex number, with or without 0x.
bool parse_hex(const std::string& text, std::uint32_t& out) {
    std::string s = trim(text);
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s = s.substr(2);
    if (s.empty() || s.size() > 8) return false;
    std::uint32_t v = 0;
    for (char c : s) {
        const int d = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                    : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                    : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (d < 0) return false;
        v = (v << 4) | static_cast<std::uint32_t>(d);
    }
    out = v;
    return true;
}

// "RSBE01:" (letters and digits, up to six, then a colon and nothing else).
bool section_id(const std::string& line, std::string& id) {
    if (line.size() < 2 || line.back() != ':') return false;
    const std::string s = trim(line.substr(0, line.size() - 1));
    if (s.empty() || s.size() > 6) return false;
    for (char c : s)
        if (!std::isalnum(static_cast<unsigned char>(c))) return false;
    id = s;
    return true;
}

// The comma-separated hex words inside "name(...)".
bool call_args(const std::string& line, const std::string& name, std::vector<std::uint32_t>& args) {
    if (lower(line.substr(0, name.size())) != name) return false;
    const std::string rest = trim(line.substr(name.size()));
    if (rest.size() < 2 || rest.front() != '(' || rest.back() != ')') return false;
    std::stringstream parts(rest.substr(1, rest.size() - 2));
    std::string part;
    args.clear();
    while (std::getline(parts, part, ',')) {
        std::uint32_t v = 0;
        if (!parse_hex(part, v)) return false;
        args.push_back(v);
    }
    return true;
}

}  // namespace

GameConfig parse_gameconfig(const std::string& text, const std::string& game_id) {
    GameConfig out;
    bool in_game = false;
    std::stringstream lines(text);
    std::string raw;
    while (std::getline(lines, raw)) {
        const std::size_t hash = raw.find('#');
        const std::string line = trim(hash == std::string::npos ? raw : raw.substr(0, hash));
        if (line.empty()) continue;
        std::string id;
        if (section_id(line, id)) {
            in_game = game_id.compare(0, id.size(), id) == 0 && id.size() <= game_id.size();
            out.found = out.found || in_game;
            continue;
        }
        if (!in_game) continue;
        std::vector<std::uint32_t> args;
        const std::size_t eq = line.find('=');
        if (call_args(line, "pokeifequal", args) && args.size() == 4) {
            GamePoke p;
            p.conditional = true;
            p.check_address = args[0];
            p.check_value = args[1];
            p.address = args[2];
            p.value = args[3];
            out.pokes.push_back(p);
        } else if (call_args(line, "poke", args) && args.size() == 2) {
            GamePoke p;
            p.address = args[0];
            p.value = args[1];
            out.pokes.push_back(p);
        } else if (eq != std::string::npos) {
            const std::string key = lower(trim(line.substr(0, eq)));
            const std::string value = trim(line.substr(eq + 1));
            std::uint32_t v = 0;
            if (key == "codeliststart" && parse_hex(value, v)) out.codelist_start = v;
            else if (key == "codelistend" && parse_hex(value, v)) out.codelist_end = v;
            else if (key == "hooktype" && parse_hex(value, v) && v <= 9) out.hooktype = static_cast<int>(v);
            else out.ignored.push_back(line);
        } else {
            out.ignored.push_back(line);
        }
    }
    return out;
}

}  // namespace riftwii
