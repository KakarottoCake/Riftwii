// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

// How each code mod starts (#368): with RiftWii's extras or without them,
// asked the first time and remembered in sd:/riftwii/codemods.txt, one
// line a game and mod, tab-separated:
//
//   RSBE01	Project+	with
//   RSBE01	rex_	without
//
// A mod is named by its code build's key (the one the Mods page and the
// choices files use: the build's folder, or "sd:/codes"), so the answer
// follows the mod to the game's page and to every tile it is on. Only code
// builds are here: Riivolution packs never ask.
namespace riftwii {

// Unset: no answer yet, so the next Start asks and remembers the answer.
// AskEachTime: the Mods page's "Ask each time": every Start asks, and
// nothing is remembered for the mod.
enum class CodeModStart { Unset, With, Without, AskEachTime };

// At most this many answers are kept (a card holds a handful of code mods),
// each key at most kCodeModsMaxKey bytes, and at most this much of the file
// is read: enough for every answer the file may hold, so none written is
// ever cut off when it is read back.
constexpr std::size_t kCodeModsMaxAnswers = 512;
constexpr std::size_t kCodeModsMaxKey = 255;
// The comment line, then a game ID, a key, "without" (the longest of
// with, without and ask) and two tabs and a
// line break a line.
constexpr std::size_t kCodeModsMaxBytes = 128 + kCodeModsMaxAnswers * (6 + kCodeModsMaxKey + 7 + 3);
static_assert(kCodeModsMaxBytes <= 160u << 10);

// Game ID and code build key to the answer; never CodeModStart::Unset.
using CodeModAnswers = std::map<std::pair<std::string, std::string>, CodeModStart>;

// Lenient: blank lines, comments ('#'), lines that are no answer and a mod
// named twice (its first answer kept) are skipped. Reading stops at
// kCodeModsMaxAnswers.
CodeModAnswers parse_code_mods(const std::string& text);
// The first kCodeModsMaxBytes of a larger file, cut back to its last whole
// line; text read whole is kept as it is.
std::string code_mods_whole_lines(const std::string& head, bool cut);
// The file, with a comment line first.
std::string serialize_code_mods(const CodeModAnswers& answers);

CodeModStart code_mod_answer(const CodeModAnswers& answers, const std::string& game_id, const std::string& key);
// Unset forgets the mod's answer. False: a game ID or key the file can't hold.
bool set_code_mod_answer(CodeModAnswers& answers, const std::string& game_id, const std::string& key,
                         CodeModStart start);

// Several code mods on together (only from one card): their shared answer,
// or Unset when one has none yet or they differ (either way RiftWii asks).
CodeModStart shared_code_mod_answer(const CodeModAnswers& answers, const std::string& game_id,
                                    const std::vector<std::string>& keys);
// After asking: the answer, kept for each of `keys` that had none. One that
// had another answer, or is set to ask each time, keeps it, so an answer
// always belongs to one mod. False: one the file couldn't take (it is full).
bool remember_code_mod_answer(CodeModAnswers& answers, const std::string& game_id,
                              const std::vector<std::string>& keys, CodeModStart picked);

}  // namespace riftwii
