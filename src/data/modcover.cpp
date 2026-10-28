// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/modcover.hpp"

namespace riftwii {

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
    for (const CustomGame& g : customs) {
        if (g.id.size() != 6 || g.id.compare(0, 4, prefix) != 0 || g.id == stock_id) continue;
        if (!title_has_mod_name(g.title, name)) continue;
        if (!found.empty() && found != g.id) return {};  // more than one: never a guess
        found = g.id;
    }
    return found;
}

}  // namespace riftwii
