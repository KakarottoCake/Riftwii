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

// One GET (http:// or https://), following up to three redirects. `body` holds at most
// `max_bytes`.
bool HttpGet(const std::string& url, std::vector<std::uint8_t>& body, std::string& error,
             std::size_t max_bytes = 8u << 20, int timeout_ms = 15000);

// sd:/riftwii/titles-<lang>.txt, where the game names are kept.
std::string TitlesPath(const std::string& lang);
// Fetches the names when the file is missing or older than a week
// (`force`: always). False with `error` when it could not.
bool UpdateTitles(const std::string& lang, bool force, std::string& error);

// The newest RiftWii release on GitHub (a tag such as "v2.0.1-beta"),
// asked at most once a day unless `force`: sd:/riftwii/update.txt keeps
// the last answer. `newer` says whether it is newer than this build.
bool CheckForUpdate(bool force, std::string& latest, bool& newer, std::string& error);
constexpr const char* kReleasesPage = "github.com/KakarottoCake/Riftwii/releases";

// Whether `latest` was already installed by InstallUpdate (it runs once
// RiftWii is started again).
bool UpdateInstalled(const std::string& latest);
// Downloads the riftwii.dol the last CheckForUpdate found for `latest`,
// checks it (size, SHA-256 when GitHub gives one, a valid DOL header) and
// puts it in place of the running boot.dol, keeping the old one as
// boot.dol.old; meta.xml's version follows. `where` is the file replaced.
bool InstallUpdate(const std::string& latest, std::string& where, std::string& error);

// sd:/riftwii/cheats/<ID>.txt.
std::string CheatPath(const std::string& game_id);
// Fetches the game's cheats, replacing the file. The archive answers
// with an empty page for a game it has no cheats for: `error` says so.
bool DownloadCheats(const std::string& game_id, std::string& error);

}  // namespace riftwii::wii
