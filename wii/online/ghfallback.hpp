// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

// Cheats from GameHacking.org (riftwii/ghcheats.hpp) for a Wii game that
// codes.rc24.xyz has none for: a zip of every game's cheat file, kept on the
// SD card in sd:/riftwii/cheats/gamehacking/ with an index of its games.
// The site is asked only when a game's cheats are wanted and there is no
// copy yet or it is over about 30 days old, and at most once per session:
// one request for the listing and one for the newest zip. Every game after
// that is answered from the card. Runs on the caller's thread (the cheats
// page waits for it as it does for rc24).
namespace riftwii::wii {

constexpr const char* kGameHackingDir = "sd:/riftwii/cheats/gamehacking";

enum class GameHackingResult {
    Found,        // `text` is the game's cheat file
    NoCheats,     // the archive was read (or its copy) and has nothing for this game
    Unavailable,  // no copy and none could be fetched; `error` says why
};

// The cheat text for `game_id` (the ID rc24 was asked for: six characters,
// or a channel's four). May fetch (see above); memory it needs comes from MEM2.
// `zip` (when given) gets the name of the zip the text was read from.
// `allow_refresh` false (the network is known to be down) answers from the
// copy on the card only, however old, and makes no request.
GameHackingResult GameHackingCheats(const std::string& game_id, std::string& text, std::string& error,
                                    std::string* zip = nullptr, bool allow_refresh = true);

}  // namespace riftwii::wii
