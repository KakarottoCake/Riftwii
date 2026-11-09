// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcexecpacks.hpp"

namespace riftwii {
namespace {

bool one_line(const std::string& s) { return s.find_first_of("\r\n\t") == std::string::npos; }

// `line` starts with `key`: the rest of it in `value`.
bool take(const std::string& line, const char* key, std::string& value) {
    const std::string prefix(key);
    if (line.compare(0, prefix.size(), prefix) != 0) return false;
    value = line.substr(prefix.size());
    return true;
}

}  // namespace

bool gc_exec_packs_text(const std::string& game, const std::string& path, const std::vector<PackageChoices>& packages,
                        std::string& text) {
    text.clear();
    if (!one_line(game) || !one_line(path)) return false;
    text = "game=" + game + "\npath=" + path + "\n";
    for (const PackageChoices& pack : packages) {
        if (!one_line(pack.xml_sd_path)) return false;
        text += "xml=" + pack.xml_sd_path + "\n";
        for (const auto& choice : pack.choices) {
            if (!one_line(choice.first) || !one_line(choice.second)) return false;
            text += "choice=" + choice.first + "\t" + choice.second + "\n";
        }
    }
    return true;
}

bool gc_exec_packs_parse(const std::string& text, const std::string& game, const std::string& path,
                         std::vector<PackageChoices>& packages) {
    packages.clear();
    if (text.size() > kMaxGcExecPacksBytes) return false;
    std::string file_game;
    std::string file_path;
    std::size_t choice_count = 0;
    bool whole = true;
    std::size_t at = 0;
    while (whole && at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::string value;
        if (take(line, "game=", value)) {
            file_game = value;
        } else if (take(line, "path=", value)) {
            file_path = value;
        } else if (take(line, "xml=", value)) {
            whole = !value.empty() && packages.size() < kMaxGcExecPacks;
            if (whole) packages.push_back(PackageChoices{value, {}});
        } else if (take(line, "choice=", value)) {
            const std::size_t tab = value.find('\t');
            whole = !packages.empty() && tab != std::string::npos && ++choice_count <= kMaxGcExecChoices;
            if (whole) packages.back().choices.emplace_back(value.substr(0, tab), value.substr(tab + 1));
        } else {
            whole = false;
        }
    }
    if (!whole || file_game != game || file_path != path || game.empty()) {
        packages.clear();
        return false;
    }
    return true;
}

}  // namespace riftwii
