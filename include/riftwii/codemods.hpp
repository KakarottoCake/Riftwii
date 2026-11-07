// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <map>
#include <set>
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
//
// A mod whose crash logs are switched on (#496: off unless the Mods page
// switches them on) has a line of its own with "logs" in the third field:
//
//   RSBE01	Project+	logs
//
// Builds from before #496 skip those lines (an answer they don't know)
// and keep the rest.
namespace riftwii {

// Unset: no answer yet. Since #496 it starts like AskEachTime: every
// Start asks, and the Mods page's Extras row (On, Off) keeps an answer.
// AskEachTime: the Mods page's "Ask every launch".
enum class CodeModStart { Unset, With, Without, AskEachTime };

// At most this many answers are kept (a card holds a handful of code mods),
// each key at most kCodeModsMaxKey bytes, and at most this much of the file
// is read: enough for every answer the file may hold, so none written is
// ever cut off when it is read back.
constexpr std::size_t kCodeModsMaxAnswers = 512;
// Mods with crash logs on: a handful, like the mods on a card.
constexpr std::size_t kCodeModsMaxCrashLogs = 64;
constexpr std::size_t kCodeModsMaxKey = 255;
// The comment line, then a game ID, a key, "without" (the longest of
// with, without and ask) and two tabs and a line break an answer's line,
// then the crash logs' lines ("logs").
constexpr std::size_t kCodeModsMaxBytes = 128 + kCodeModsMaxAnswers * (6 + kCodeModsMaxKey + 7 + 3) +
                                          kCodeModsMaxCrashLogs * (6 + kCodeModsMaxKey + 4 + 3);
static_assert(kCodeModsMaxBytes <= 160u << 10);

// Game ID and code build key to the answer; never CodeModStart::Unset.
using CodeModAnswers = std::map<std::pair<std::string, std::string>, CodeModStart>;
// Game ID and code build key of each mod whose crash logs are on.
using CodeModCrashLogs = std::set<std::pair<std::string, std::string>>;

// Lenient: blank lines, comments ('#'), lines that are no answer and a mod
// named twice (its first answer kept) are skipped. Reading stops at
// kCodeModsMaxAnswers.
CodeModAnswers parse_code_mods(const std::string& text);
// The first kCodeModsMaxBytes of a larger file, cut back to its last whole
// line; text read whole is kept as it is.
std::string code_mods_whole_lines(const std::string& head, bool cut);
// The crash logs' lines of the same text (lenient in the same way; reading
// stops at kCodeModsMaxCrashLogs).
CodeModCrashLogs parse_code_mod_crash_logs(const std::string& text);
// The file, with a comment line first: the answers, then the crash logs.
std::string serialize_code_mods(const CodeModAnswers& answers, const CodeModCrashLogs& crash_logs = {});

CodeModStart code_mod_answer(const CodeModAnswers& answers, const std::string& game_id, const std::string& key);
// Unset forgets the mod's answer. False: a game ID or key the file can't hold.
bool set_code_mod_answer(CodeModAnswers& answers, const std::string& game_id, const std::string& key,
                         CodeModStart start);

// Several code mods on together (only from one card): their shared answer,
// or Unset when one has none yet or they differ (either way RiftWii asks).
CodeModStart shared_code_mod_answer(const CodeModAnswers& answers, const std::string& game_id,
                                    const std::vector<std::string>& keys);

// Crash logs for a mod (#496): off unless switched on.
bool code_mod_crash_logs(const CodeModCrashLogs& crash_logs, const std::string& game_id, const std::string& key);
// False: a game ID or key the file can't hold, or the file holds no more
// mods with crash logs on (switching off always works).
bool set_code_mod_crash_logs(CodeModCrashLogs& crash_logs, const std::string& game_id, const std::string& key,
                             bool on);
// Several code mods on together: on only when each of them has them on.
bool shared_code_mod_crash_logs(const CodeModCrashLogs& crash_logs, const std::string& game_id,
                                const std::vector<std::string>& keys);

}  // namespace riftwii
