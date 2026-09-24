// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/cheats.hpp"

#include <cctype>
#include <sstream>

namespace riftwii {
namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// "XXXXXXXX XXXXXXXX": 1 with the two words, 2 when it has that shape
// with placeholders (letters beyond F) in it, 0 otherwise.
int code_line(const std::string& line, std::uint32_t& a, std::uint32_t& b) {
    if (line.size() != 17 || line[8] != ' ') return 0;
    bool placeholder = false;
    std::uint32_t words[2] = {0, 0};
    for (std::size_t i = 0; i < 17; ++i) {
        if (i == 8) continue;
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(line[i])));
        unsigned v = 0;
        if (c >= '0' && c <= '9') {
            v = static_cast<unsigned>(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            v = static_cast<unsigned>(c - 'A' + 10);
        } else if (c >= 'G' && c <= 'Z') {
            placeholder = true;
        } else {
            return 0;
        }
        std::uint32_t& w = words[i < 8 ? 0 : 1];
        w = (w << 4) | v;
    }
    a = words[0];
    b = words[1];
    return placeholder ? 2 : 1;
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

}  // namespace

bool parse_cheat_text(const std::string& text, CheatFile& out, std::string& error) {
    out = CheatFile{};
    std::istringstream in(text);
    std::string raw;
    std::vector<std::string> lines;
    while (std::getline(in, raw)) lines.push_back(trim(raw));
    // A UTF-8 byte order mark from a PC editor.
    if (!lines.empty() && lines[0].compare(0, 3, "\xEF\xBB\xBF") == 0) lines[0] = lines[0].substr(3);
    std::size_t i = 0;
    // The header: the ID line and the name line, when the file has them.
    if (i < lines.size() && (lines[i].size() == 6 || lines[i].size() == 4)) {
        std::uint32_t a, b;
        if (code_line(lines[i], a, b) == 0) out.game_id = lines[i++];
    }
    if (!out.game_id.empty() && i < lines.size() && !lines[i].empty()) out.title = lines[i++];
    Cheat current;
    bool open = false;
    const auto close = [&]() {
        if (open && (!current.words.empty() || current.needs_values)) out.cheats.push_back(current);
        current = Cheat{};
        open = false;
    };
    for (; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.empty()) {
            close();
            continue;
        }
        std::uint32_t a = 0, b = 0;
        const int kind = code_line(line, a, b);
        if (!open) {
            if (kind != 0) continue;  // code lines with no name: nothing to call them
            current.name = line;
            open = true;
            continue;
        }
        if (kind == 1) {
            current.words.push_back(a);
            current.words.push_back(b);
        } else if (kind == 2) {
            current.needs_values = true;
        } else {
            current.notes.push_back(line);
        }
    }
    close();
    // Names must tell cheats apart: they are how choices are saved.
    std::vector<std::string> original;
    for (const Cheat& c : out.cheats) original.push_back(c.name);
    for (std::size_t k = 0; k < out.cheats.size(); ++k) {
        int n = 1;
        for (std::size_t j = 0; j < k; ++j) {
            if (original[j] == original[k]) ++n;
        }
        if (n > 1) out.cheats[k].name += " (" + std::to_string(n) + ")";
    }
    if (out.cheats.empty()) {
        error = "no cheats in the file";
        return false;
    }
    return true;
}

std::vector<std::uint8_t> build_gct(const CheatFile& file, const std::set<std::string>& enabled, std::size_t& count) {
    std::vector<std::uint8_t> out;
    count = 0;
    put32(out, 0x00D0C0DE);
    put32(out, 0x00D0C0DE);
    for (const Cheat& c : file.cheats) {
        if (c.needs_values || c.words.empty() || enabled.count(c.name) == 0) continue;
        for (std::uint32_t w : c.words) put32(out, w);
        ++count;
    }
    put32(out, 0xF0000000);
    put32(out, 0x00000000);
    return out;
}

}  // namespace riftwii
