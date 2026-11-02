// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "covers.hpp"
#include "emulators.hpp"

namespace riftwii::wii {

// Publishes a bounded copy for the cover loader and download threads.
void ConfigureEmulatorCovers(const EmulatorCatalog& catalog);
bool EmulatorCoverWanted(const std::string& id);
void ForgetEmulatorCoverMiss(const std::string& id);
bool SteamGridKeyFound();
bool EmulatorDownloadedCoversAllowed();

// Local PNGs are checked on the loader thread before the downloaded cache,
// even with all downloads disabled. The result is the normal 80x112 RWC file.
bool ReadEmulatorLocalCover(const std::string& id, std::vector<std::uint8_t>& file);
// channel-<cover ID> requests share catalog, settings and the network worker,
// with independent banner caches and missing-source notes.
bool ReadEmulatorBanner(const std::string& key, std::vector<std::uint8_t>& file);
CoverFetch FetchEmulatorCover(const std::string& id, std::string& error);

// The player's own cover of a Wii or GameCube game (#384), read as the
// emulator games' own covers are: `name` is a game ID, or a mod tile's
// "<game ID>@<slug>", and the file riftwii/covers/<name>.png on the SD
// card, else on the USB drive once the drives are read. The result is the
// normal 80x112 RWC file.
bool ReadOwnCover(const std::string& name, std::vector<std::uint8_t>& file);
// Whether such a file is there (without reading it all).
bool OwnCoverThere(const std::string& name);
// Whether it starts as a PNG the cover reader would take (its first bytes
// only: cheap enough for the menu thread; the reader still decodes it).
bool OwnCoverLooksReadable(const std::string& name);
// Forgets whether usb:/riftwii/covers was there: the USB scan is done (a
// drive found, swapped or gone since).
void ForgetUsbCoversFolder();

}  // namespace riftwii::wii
