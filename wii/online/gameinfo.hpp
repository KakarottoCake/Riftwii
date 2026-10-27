// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <ctime>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "riftwii/gametdb.hpp"

// A game's details for its About page (riftwii/gametdb.hpp), from GameTDB's
// database of every Wii game: sd:/riftwii/wiitdb-<lang>.zip, which the
// About page downloads when the player asks (about 2 MB: the texts in the
// menu's language, English where there is no translation), else a
// wiitdb.xml another loader left on the card. Reading the whole database
// takes seconds on a Wii, so the details of the games on the player's
// drives are kept in sd:/riftwii/gameinfo-<lang>.txt and it is read again
// only for a game that is not in there yet, or once the database is newer.
namespace riftwii::wii {

// The database on the card: its path and when it was written. False when
// there is none.
bool GameInfoSource(std::string& path, std::time_t& written);

enum class GameInfoResult { Found, NotListed, NoDatabase, Failed };

// `id`'s details. When the cache does not have them, the database is read
// for `id` and every game in `wanted` (the rest of the player's games, so
// it is read once for all of them). `progress` gets the share done, 0 to
// 1, and stops the reading by returning false.
GameInfoResult FindGameInfo(const std::string& id, const std::set<std::string>& wanted, GameInfo& out,
                            std::string& error, const std::function<bool(double done)>& progress = nullptr);

// `id`'s details when the cache already has them: the game page's line of
// facts. Reads nothing but the cache file (once a session), never the
// database.
bool PeekGameInfo(const std::string& id, GameInfo& out);

// GameTDB's homebrew apps (riftwii/homebrewmatch.hpp), kept in
// sd:/riftwii/homebrew-titles.txt whenever the database is read, for the
// Homebrew view's covers. False when there is no such file or it can't be
// read; true with no apps for the empty list (a database without homebrew).
bool LoadHomebrewTitles(std::vector<HomebrewTitle>& out);
// How many times that file was written this session: the apps are matched
// again once it changes (a game's About page read the database).
unsigned HomebrewTitlesWritten();
// Whether the database on the card is newer than that file (or there is
// no file yet), and was not read for it this session.
bool HomebrewTitlesNeedReading();
// Forgets that the database was read for its homebrew apps this session:
// the SD card was put back and may be another one, with its own database.
void ForgetHomebrewTitlesRead();
// Reads the database for its homebrew apps alone and keeps them;
// `progress` as FindGameInfo's.
bool ReadHomebrewTitles(std::string& error, const std::function<bool(double done)>& progress = nullptr);

// Downloads the database in the menu's language (`received`: bytes so
// far). The cache is made again from it at the next FindGameInfo.
bool DownloadGameInfo(std::string& error, const std::function<void(std::size_t received)>& received = nullptr);

}  // namespace riftwii::wii
