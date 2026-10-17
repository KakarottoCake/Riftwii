// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "frontend.hpp"
#include "riftwii/cheats.hpp"
#include "riftwii/launchargs.hpp"
#include "riftwii/playhistory.hpp"

// The per-game extras the game page offers beside the mod packs: cheats
// (sd:/riftwii/cheats/<ID>.txt) and the video settings, and handing them
// to the boot code (boot.hpp's LaunchExtras) before a launch.
namespace riftwii::wii {

// Reads the game's cheat file. With `download` and the online setting on,
// a missing file is fetched first. False with `status` saying why there
// are no cheats (no file, none in the archive, offline...).
bool LoadGameCheats(const std::string& game_id, bool download, CheatFile& out, std::string& status);

// What the last launch of the game found about its borders (boot.cpp
// writes sd:/riftwii/choices/<ID>.video): a sentence for the game page,
// empty when the game has not been started yet.
std::string BorderNote(const std::string& game_id);

// Builds the extras for the selected game from its choices and the
// global settings and passes them to the boot code. Needs the card
// mounted; call it just before leaving the menu.
// A headless launch (wii/launch/headless.hpp) passes what the other loader
// chose: its cheat file, custom server domain and return-to title.
void PrepareLaunchExtras(const FrontendState& state, const HeadlessLaunch* headless = nullptr);
// Whether the code builds turned on for the game (with its cheats) can
// run; `error` says why not, for the game page. Needs the card mounted.
bool CheckCodeBuilds(const FrontendState& state, std::string& error);

// Which games were started from RiftWii (sd:/riftwii/history.txt), read
// once.
const PlayHistory& History();
// Counts a start of the game (or of a profile's tile, by its key) and saves the history.
void RecordPlay(const std::string& game_id);
// The tile launched (the game ID, or a profile's key), kept in
// sd:/riftwii/last_tile.txt even with the history off: a problem report
// then shows the choices that ran (riftwii/profiles.hpp's launched_choices_key).
void RememberLaunchedTile(const std::string& key);
// Drops a tile from the history (a profile removed).
void ForgetPlays(const std::string& game_id);
// "Played 3 times, last on 9/21" for the game page; empty when never.
std::string PlayNote(const std::string& game_id);

// Home's own order (sd:/riftwii/order.txt, riftwii/homeorder.hpp), read
// once; and written back after the player moves a game.
const std::vector<std::string>& HomeOrder();
void SaveHomeOrder(const std::vector<std::string>& order);

}  // namespace riftwii::wii
