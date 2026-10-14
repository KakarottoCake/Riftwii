// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpconfig.hpp"

#include <vector>

namespace riftwii {
namespace {

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

}  // namespace

bool ini_add_default(std::string& text, const std::string& section, const std::string& key,
                     const std::string& value) {
    const std::string newline = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    // Lines as [begin, end) offsets, the end before the line break.
    std::vector<std::pair<std::size_t, std::size_t>> lines;
    for (std::size_t at = 0; at < text.size();) {
        std::size_t end = text.find('\n', at);
        const std::size_t next = end == std::string::npos ? text.size() : end + 1;
        if (end == std::string::npos) end = text.size();
        lines.emplace_back(at, end);
        at = next;
    }
    bool inside = false;
    std::size_t last_in_section = std::string::npos;  // offset after the section's last non-empty line
    for (const auto& line : lines) {
        const std::string t = trim(text.substr(line.first, line.second - line.first));
        if (!t.empty() && t[0] == '[') {
            const auto close = t.find(']');
            inside = close != std::string::npos && trim(t.substr(1, close - 1)) == section;
            if (inside) last_in_section = line.second;
            continue;
        }
        if (!inside || t.empty() || t[0] == '#' || t[0] == ';') {
            if (inside && !t.empty()) last_in_section = line.second;
            continue;
        }
        last_in_section = line.second;
        const auto eq = t.find('=');
        if (trim(t.substr(0, eq)) == key) return false;
    }
    const std::string entry = key + " = " + value;
    if (last_in_section != std::string::npos) {
        // After the section's last line, keeping its own line break.
        std::size_t at = last_in_section;
        if (at > 0 && text[at - 1] == '\r') --at;
        text.insert(at, newline + entry);
        return true;
    }
    if (!text.empty() && text.back() != '\n') text += newline;
    if (!text.empty()) text += newline;
    text += "[" + section + "]" + newline + entry + newline;
    return true;
}

bool ctgp_config_defaults(std::string& text) {
    return ini_add_default(text, "exploit", "disable_ios_exploit", "yes");
}

}  // namespace riftwii
