// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/codemods.hpp"

#include "riftwii/textlines.hpp"

namespace riftwii {
namespace {

bool plausible_id(const std::string& id) {
    if (id.size() < 4 || id.size() > 6) return false;
    for (char c : id)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    return true;
}

// A build's folder name, or "sd:/codes": no tab or line break, which the
// file uses between fields and lines, and no longer than a folder's name.
bool plausible_key(const std::string& key) {
    if (key.empty() || key.size() > kCodeModsMaxKey) return false;
    for (char c : key)
        if (c == '\t' || c == '\n' || c == '\r' || c == '\0') return false;
    return true;
}

}  // namespace

CodeModAnswers parse_code_mods(const std::string& text) {
    CodeModAnswers answers;
    LineReader in(text);
    std::string line;
    while (answers.size() < kCodeModsMaxAnswers && in.next(line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const std::size_t first = line.find('\t');
        const std::size_t second = first == std::string::npos ? first : line.find('\t', first + 1);
        if (second == std::string::npos || line.find('\t', second + 1) != std::string::npos) continue;
        const std::string game = line.substr(0, first);
        const std::string key = line.substr(first + 1, second - first - 1);
        const std::string start = line.substr(second + 1);
        if (!plausible_id(game) || !plausible_key(key)) continue;
        if (start != "with" && start != "without" && start != "ask") continue;
        answers.emplace(std::make_pair(game, key), start == "with"      ? CodeModStart::With
                                                   : start == "without" ? CodeModStart::Without
                                                                        : CodeModStart::AskEachTime);
    }
    return answers;
}

std::string code_mods_whole_lines(const std::string& head, bool cut) {
    if (!cut) return head;
    const std::size_t lastNewline = head.rfind('\n');
    return lastNewline == std::string::npos ? std::string() : head.substr(0, lastNewline + 1);
}

std::string serialize_code_mods(const CodeModAnswers& answers) {
    std::string out = "# RiftWii code mods: game ID, code build, with/without RiftWii's extras, or ask\n";
    std::size_t count = 0;
    for (const auto& [mod, start] : answers) {
        if (count++ == kCodeModsMaxAnswers) break;
        const char* word = start == CodeModStart::With ? "with" : start == CodeModStart::Without ? "without" : "ask";
        out += mod.first + "\t" + mod.second + "\t" + word + "\n";
    }
    return out;
}

CodeModStart code_mod_answer(const CodeModAnswers& answers, const std::string& game_id, const std::string& key) {
    const auto it = answers.find({game_id, key});
    return it == answers.end() ? CodeModStart::Unset : it->second;
}

bool set_code_mod_answer(CodeModAnswers& answers, const std::string& game_id, const std::string& key,
                         CodeModStart start) {
    if (!plausible_id(game_id) || !plausible_key(key)) return false;
    if (start == CodeModStart::Unset) {
        answers.erase({game_id, key});
        return true;
    }
    // A full file keeps the answers it has: a new mod is then asked each time.
    if (answers.size() >= kCodeModsMaxAnswers && answers.find({game_id, key}) == answers.end()) return false;
    answers[{game_id, key}] = start;
    return true;
}

CodeModStart shared_code_mod_answer(const CodeModAnswers& answers, const std::string& game_id,
                                    const std::vector<std::string>& keys) {
    if (keys.empty()) return CodeModStart::Unset;
    const CodeModStart first = code_mod_answer(answers, game_id, keys.front());
    for (const std::string& key : keys)
        if (code_mod_answer(answers, game_id, key) != first) return CodeModStart::Unset;
    return first;
}

bool remember_code_mod_answer(CodeModAnswers& answers, const std::string& game_id,
                              const std::vector<std::string>& keys, CodeModStart picked) {
    if (picked != CodeModStart::With && picked != CodeModStart::Without) return true;
    bool kept = true;
    for (const std::string& key : keys)
        if (code_mod_answer(answers, game_id, key) == CodeModStart::Unset)
            kept = set_code_mod_answer(answers, game_id, key, picked) && kept;
    return kept;
}

}  // namespace riftwii
