// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "usbcatalog.hpp"

// The games' own banners (riftwii/bnr.hpp), kept on the card: each game's
// opening.bnr is read from its image once (wii/usbcatalog.hpp's
// read_image_disc_file) and stored as sd:/riftwii/banners/<ID>.bnr.
namespace riftwii::wii {

// True when the game's banner is not stored yet and reading it has not
// failed this session.
bool BannerWanted(const std::string& game_id);

// Reads the banner from the game's image and stores it. Takes a moment
// (an ISO or WBFS game's is decrypted); a failure is remembered for the
// session so the game is not tried again.
bool StoreBanner(const ImageGame& game, std::string& error);

// The stored banner's bytes.
bool LoadBanner(const std::string& game_id, std::vector<std::uint8_t>& out);

// The stored banner with only what its icon needs read from the card (its
// header, archive table and meta/icon.bin; the rest left zero): a channel
// icon in a fraction of the time. The whole file when it is laid out some
// other way.
bool LoadBannerIcon(const std::string& game_id, std::vector<std::uint8_t>& out);

// The Wii's language as banners name it ("ENG", "JPN", ...), and as the
// index of their IMET names (riftwii/bnr.hpp's BannerLanguage).
std::string BannerLanguageCode();
int BannerLanguageIndex();

}  // namespace riftwii::wii
