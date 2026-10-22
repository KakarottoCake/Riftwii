// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "riftwii/themelist.hpp"

// The theme browser's engine (Settings > Theme > Get more themes): the list
// from riftwii.github.io, each theme's preview as a small texture, and the
// download and safe install of a theme into sd:/riftwii/themes. The page
// that shows it is MenuThemeBrowser in rift_menu.cpp.
//
// The network work runs on the network's background thread
// (netsock.hpp's NetRunInBackground), one job at a time, so the menu keeps
// answering. The menu thread calls Pump() while the GUI is halted; it
// starts the next job, takes a finished one's answer and never waits.
//
// Memory, by Jack's rule MEM2 first: everything big lives in blocks taken
// from MEM2 when the page opens (Open) and given back when it closes
// (Close): the downloaded list, the thumbnails, the connection's state,
// the downloaded answer and the picture decoder's rows. What stays on the
// heap are the page's own rows of text and a few strings.
namespace riftwii::wii::themes {

// Takes the page's MEM2. False (with `error`) when there is none.
bool Open(std::string& error);
// Stops the job (waiting for it), gives the memory back.
void Close();

// Starts whatever job is due and takes a finished one's answer. Called
// by the menu thread, with the GUI thread halted, every turn of the page's
// loop.
void Pump();

enum class ListState { Loading, Ready, Failed };
ListState ListStatus(std::string& error);
// Asks for the list again after it failed.
void RetryList();
// The themes of the list, once Ready. Valid until Close.
const ThemeListing* Listing(std::size_t& count);

// The preview of theme `index` as a 160x120 RGB5A3 texture (32-byte aligned),
// or null until it has been fetched. GUI thread. It never fetches: Focus asks,
// and the neighbours follow.
constexpr int kThumbWidth = 160;
constexpr int kThumbHeight = 120;
const u8* Thumbnail(std::size_t index);
// Whether Thumbnail(index) has one (no drawing involved: the menu thread asks).
bool ThumbnailReady(std::size_t index);
// The theme the player is looking at: its preview is fetched first.
void Focus(std::size_t index);
// Previews that could not be fetched (not retried this visit).
bool PreviewFailed(std::size_t index);

// What the card has of a theme in the list.
enum class Installed {
    No,      // no such folder
    Current, // installed by the browser from this very zip
    Update,  // installed by the browser, and the list has another zip
    Other,   // a folder of that name that the browser did not install
};
// Reads sd:/riftwii/themes again (after an install or delete, and at Open).
void RefreshInstalled();
Installed InstalledState(const ThemeListing& theme);

// The install of theme `index`, as a job.
enum class Stage { Download, Check, Unpack, Verify, Finish };
struct InstallProgress {
    Stage stage = Stage::Download;
    std::uint32_t done = 0;   // bytes (download) or files (unpack) so far
    std::uint32_t total = 0;
};
// False when a job is running (the page waits and asks again).
bool StartInstall(std::size_t index);
bool InstallRunning();
InstallProgress CurrentInstallProgress();
// The player pressed Cancel.
void CancelInstall();
// Once the install has ended: true with the theme's folder, `error` empty
// when it worked, else `error` says why. False while it runs.
bool TakeInstall(std::string& folder, std::string& error);

// Removes a theme the browser installed (and its record). False with `error`
// when it was not installed by the browser or could not be removed.
bool DeleteTheme(const std::string& folder, std::string& error);

}  // namespace riftwii::wii::themes
