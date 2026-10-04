// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

#include <cstdint>
#include <string>
#include <vector>

// Box art (riftwii/coverart.hpp) on the Wii: fetched from GameTDB into
// sd:/riftwii/covers/<ID>.rwc, and read back into a fixed pool of
// textures in MEM2 (room for every game's cover, up to 400) for drawing.
namespace riftwii::wii {

// True when the game has no stored cover and GameTDB was not found
// without one in the last week.
bool CoverWanted(const std::string& game_id);
// True when the game's cover is on the card.
bool CoverStored(const std::string& game_id);

enum class CoverFetch { Stored, NotFound, Failed };
// Downloads, shrinks and stores the game's cover. NotFound: GameTDB has
// none (remembered for a week); Failed: no network, or the card could
// not be written (`error`).
CoverFetch FetchCover(const std::string& game_id, std::string& error);

// FetchCover on the network's background thread (NetRunInBackground), so
// the menu keeps answering while the network comes up and GameTDB answers
// (seconds). One at a time: false while the network's thread is busy or
// the last one's answer was not taken yet.
bool StartCoverFetch(const std::string& game_id);
// The game a started fetch is for, until its answer is taken ("" if none).
const std::string& CoverFetchGame();
// The answer of a started fetch once it has finished, once.
bool TakeCoverFetch(std::string& game_id, CoverFetch& got, std::string& error);

// The game's cover as an RGB5A3 texture of kCoverWidth x kCoverHeight,
// or nullptr while there is none in memory. Never reads the card: a cover
// not in the pool is asked of a loader thread (a few frames), so draw
// without it and ask again next frame; asks not renewed for a moment are
// dropped. `rank` orders the asks, lowest first. GUI thread only.
const u8* CoverTexture(const std::string& game_id, int rank = 0);
// Asks for a cover to be read ahead (a page next to the one shown), and
// keeps it in memory while asked for every frame.
void CoverPrefetch(const std::string& game_id, int rank);
// Room in MEM2 for this many covers (the games in the grid and the game
// page), taken once in one piece; never less. GUI thread, before the
// first CoverTexture.
void CoverReserve(int covers);
// Covers for the loader to read when nothing is asked for, in this order,
// while there is room: then no page has to wait for its covers.
void CoverReadAll(const std::vector<std::string>& game_ids);
// Drops what the pool remembers about the game (after a new download).
void ForgetCover(const std::string& game_id);
// Stops the loader reading the card, waiting for a read in flight: before
// the card is unmounted, read raw or IOS reloaded. Lifted by
// CoverLoaderRelease (the menu's ResumeGui).
void CoverLoaderHold();
void CoverLoaderRelease();

// A PNG file's bytes to RGBA rows (a cover, a theme's picture).
bool DecodePngRgba(const std::vector<std::uint8_t>& png, std::vector<std::uint8_t>& rgba, int& w, int& h,
                   std::string& error);

}  // namespace riftwii::wii
