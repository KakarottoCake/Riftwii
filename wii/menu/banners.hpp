// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nandchannels.hpp"
#include "usbcatalog.hpp"

// The games' own banners (riftwii/bnr.hpp), kept on the card: each game's
// opening.bnr is read from its image once (wii/storage/usbcatalog.hpp's
// read_image_disc_file) and stored as sd:/riftwii/banners/<ID>.bnr.
//
// A GameCube game's disc banner is the old BNR1/BNR2 picture
// (riftwii/gcbanner.hpp), stored the same way: the player draws it in a
// frame of RiftWii's own. With Download names and cheats on (Settings >
// Online), the community's animated banner for the game is downloaded in
// the background and takes its place; a game the server has none for is
// stamped <ID>.none and not asked about again for a week.
namespace riftwii::wii {

// True when the game's banner is not stored yet and reading it has not
// failed this session.
bool BannerWanted(const std::string& game_id);

// Reads the banner from the game's image and stores it. Takes a moment
// (an ISO or WBFS game's is decrypted); a failure is remembered for the
// session so the game is not tried again. A GameCube game's is its disc's
// BNR1/BNR2 picture, a few kilobytes.
bool StoreBanner(const ImageGame& game, std::string& error);

// GameCube: true when the game has no animated banner stored, the server
// was not found without one in the last week, and no download of it
// missed this session. The menu asks only with Download names and cheats on.
bool GameCubeBannerWanted(const std::string& game_id);

enum class BannerFetch { Stored, NotFound, Failed };
// The game's animated banner downloaded on the network's background thread
// (NetRunInBackground) and stored, checked first (riftwii/gcbanner.hpp's
// check_gc_banner_download). One at a time: false while the network's
// thread is busy or the last one's answer was not taken yet.
// `regional_fallback`: the game is a regional copy of a game, not a mod
// (riftwii/gcbanner.hpp's gc_banner_regional_copy), so the server's banner
// for its first three letters may stand in for its own.
bool StartGameCubeBannerFetch(const std::string& game_id, bool regional_fallback);
// The game a started fetch is for, until its answer is taken ("" if none).
const std::string& GameCubeBannerFetchGame();
// The answer once the fetch has finished, once. NotFound: the server has
// no banner for it (or sent one that is not); Failed: the network or the
// card failed (`error`).
bool TakeGameCubeBannerFetch(std::string& game_id, BannerFetch& got, std::string& error);

// The same for a WiiWare or Virtual Console game installed on the Wii:
// its banner content read from the NAND, stored under its game ID.
bool StoreChannelBanner(const NandChannel& channel, std::string& error);

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
