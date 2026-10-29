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

}  // namespace riftwii::wii
