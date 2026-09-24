// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// What RiftWii fetches from the internet when the Wii is online and the
// "Download names and cheats" setting is on, over plain HTTP
// (riftwii/http.hpp):
//   - game names from GameTDB, in the menu's language, to
//     sd:/riftwii/titles-<lang>.txt (checked at most once a week);
//   - a game's cheats from the GeckoCodes archive (RiiConnect24), to
//     sd:/riftwii/cheats/<ID>.txt, a text file anyone can edit.
namespace riftwii::wii {

constexpr const char* kCheatDir = "sd:/riftwii/cheats";

// One GET, following up to three http:// redirects. `body` holds at most
// `max_bytes`.
bool HttpGet(const std::string& url, std::vector<std::uint8_t>& body, std::string& error,
             std::size_t max_bytes = 8u << 20, int timeout_ms = 15000);

// sd:/riftwii/titles-<lang>.txt, where the game names are kept.
std::string TitlesPath(const std::string& lang);
// Fetches the names when the file is missing or older than a week
// (`force`: always). False with `error` when it could not.
bool UpdateTitles(const std::string& lang, bool force, std::string& error);

// sd:/riftwii/cheats/<ID>.txt.
std::string CheatPath(const std::string& game_id);
// Fetches the game's cheats, replacing the file. The archive answers
// with an empty page for a game it has no cheats for: `error` says so.
bool DownloadCheats(const std::string& game_id, std::string& error);

}  // namespace riftwii::wii
