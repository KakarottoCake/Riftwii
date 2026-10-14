// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "riftwii/cheats.hpp"

// What RiftWii fetches from the internet when the Wii is online and the
// "Download names and cheats" setting is on, over plain HTTP
// (riftwii/http.hpp):
//   - game names from GameTDB, in the menu's language, to
//     sd:/riftwii/titles-<lang>.txt (checked at most once a week);
//   - a game's cheats from the GeckoCodes archive (RiiConnect24), to
//     sd:/riftwii/cheats/<ID>.txt, a text file anyone can edit.
namespace riftwii::wii {

constexpr const char* kCheatDir = "sd:/riftwii/cheats";

// Bytes received so far (headers included), called as they come in.
using HttpProgress = std::function<void(std::size_t received)>;

// One GET (http:// or https://), following up to three redirects. `body` holds at most
// `max_bytes`.
bool HttpGet(const std::string& url, std::vector<std::uint8_t>& body, std::string& error,
             std::size_t max_bytes = 8u << 20, int timeout_ms = 15000, const HttpProgress& progress = nullptr);

// One POST of `body`, no redirects. True when the server answered at
// all: `status` and `answer` (its body, at most 64 KiB) are what it said.
bool HttpPost(const std::string& url, const std::string& content_type, const std::string& body, int& status,
              std::string& answer, std::string& error, int timeout_ms = 30000);

// sd:/riftwii/titles-<lang>.txt, where the game names are kept.
std::string TitlesPath(const std::string& lang);
// Fetches the names when the file is missing or older than a week
// (`force`: always). False with `error` when it could not.
bool UpdateTitles(const std::string& lang, bool force, std::string& error);

// The newest RiftWii release on GitHub (a tag such as "v2.0.1-beta"),
// asked every time. sd:/riftwii/update.txt keeps the last answer, which
// stands in when GitHub cannot be reached (not with `force`). `newer`
// says whether it is newer than this build.
bool CheckForUpdate(bool force, std::string& latest, bool& newer, std::string& error);
// The check at start, on a thread of its own (NetRunInBackground) so the
// menu answers while the network comes up. TakeUpdateCheck is true once,
// when it has finished, with what CheckForUpdate said.
void StartUpdateCheck();
bool TakeUpdateCheck(bool& ok, std::string& latest, bool& newer, std::string& error);
// The download of the update pack (themes, channel installer) that
// follows the first start after an in-app update: running (a launch then
// stops it, NetCancelBackground), and, on Home's thread, the player's theme
// moved off one the pack retired.
bool UpdatePacksBusy();
void TakeUpdatePacks();

// After an in-app update, until this version's riftwii-apps.pack has
// been written (the channel installer in sd:/apps/riftwii_channel): the
// installer there is still the old version's.
bool AppsPackPending();
constexpr const char* kReleasesPage = "github.com/KakarottoCake/Riftwii/releases";

// Whether `latest` was already installed by InstallUpdate (it runs once
// RiftWii is started again).
bool UpdateInstalled(const std::string& latest);
// Downloads the riftwii.dol the last CheckForUpdate found for `latest`,
// checks it (size, GitHub's SHA-256, a valid DOL header) and puts it in
// place of the running boot.dol, keeping the old one as boot.dol.old;
// meta.xml's version follows. A release without a SHA-256 is refused.
// `where` is the file replaced. `progress` gets the share of the download
// done, 0 to 1.
bool InstallUpdate(const std::string& latest, std::string& where, std::string& error,
                   const std::function<void(double done)>& progress = nullptr);

// Card writes RiftWii must not be stopped in the middle of (the update's
// swap): while one runs, the GUI thread holds back the power button and
// any exit until it has finished. Taken on the menu thread.
class CardWriteHold {
public:
    CardWriteHold();
    ~CardWriteHold();
    CardWriteHold(const CardWriteHold&) = delete;
    CardWriteHold& operator=(const CardWriteHold&) = delete;
};
bool CardWritesBusy();
// The player turned down `latest` at start (Not now, twice). Logged to the
// session now, and to boot.log by LogDeclinedUpdate at a launch, so a bug
// report shows the version that might already fix it was declined.
void NoteUpdateDeclined(const std::string& latest);
void LogDeclinedUpdate();

// sd:/riftwii/cheats/<ID>.txt.
std::string CheatPath(const std::string& game_id);

// Adds <ahb_access/> to the meta.xml next to this boot.dol when it lacks
// it (an update through the menu replaces only boot.dol): the Homebrew
// Channel starts RiftWii with hardware access from the next start on, for
// a pack's Homebrew Channel app to inherit. Best effort, logged.
void EnsureMetaAhbAccess();
// Fetches the game's cheats into the file, keeping the player's own (cheats
// added by hand, values filled in). The archive answers with an empty
// page for a game it has no cheats for: `error` says so.
bool DownloadCheats(const std::string& game_id, std::string& error);
// The placeholders (X, Y...) of cheat `name` in the game's cheat file,
// with their values when they are filled in. Empty when none.
std::vector<CheatField> CheatFields(const std::string& game_id, const std::string& name);
// Fills them in, in the file (riftwii/cheats.hpp fill_cheat_values).
bool FillCheatValues(const std::string& game_id, const std::string& name, const std::vector<CheatField>& fields,
                     std::string& error);

}  // namespace riftwii::wii
