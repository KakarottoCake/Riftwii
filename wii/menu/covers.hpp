// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

#include <cstdint>
#include <string>
#include <vector>

// Box art (riftwii/coverart.hpp) on the Wii: fetched from GameTDB (or the
// emulator sources in emulatorcovers.hpp) into
// sd:/riftwii/covers/<ID>.rwc, and read back into a fixed pool of
// textures in MEM2 (room for every game's cover, up to 400) for drawing.
namespace riftwii::wii {

// True when the game has no stored cover and GameTDB was not found
// without one in the last week.
bool CoverWanted(const std::string& game_id);
// Forgets that GameTDB had no cover for the game, so the next look asks
// again instead of waiting out the week (Settings > Download missing covers).
void ForgetCoverMiss(const std::string& game_id);
// True when the game's cover is on the card.
bool CoverStored(const std::string& game_id);

enum class CoverFetch { Stored, NotFound, NoAnswer, Failed };
// Downloads, shrinks and stores the game's cover. NotFound: GameTDB has
// none (remembered for a week); NoAnswer: no answer about it (no network,
// TLS, a timeout, GameTDB in trouble; `error`), not remembered; Failed:
// the card could not be written (`error`). The shelf's boxes and the 3D
// covers (boxart.hpp, cover3d.hpp) answer the same way.
CoverFetch FetchCover(const std::string& game_id, std::string& error);

// FetchCover on the network's background thread (NetRunInBackground), so
// the menu keeps answering while the network comes up and GameTDB answers
// (seconds). One at a time: false while the network's thread is busy or
// the last one's answer was not taken yet.
bool StartCoverFetch(const std::string& game_id);
// The game a started fetch is for, until its answer is taken ("" if none).
const std::string& CoverFetchGame();
// Whether the fetch TakeCoverFetch last gave was of GameTDB's second set
// (CoverSecondSet when it started): a set changed meanwhile means another
// fetch.
bool TakenFetchWasSecondSet();
// The answer of a started fetch once it has finished, once.
bool TakeCoverFetch(std::string& game_id, CoverFetch& got, std::string& error);

// The game's cover as an RGB5A3 texture of kCoverWidth x kCoverHeight,
// or nullptr while there is none in memory. Never reads the card: a cover
// not in the pool is asked of a loader thread (a few frames), so draw
// without it and ask again next frame; asks not renewed for a moment are
// dropped. `rank` orders the asks, lowest first. GUI thread only.
const u8* CoverTexture(const std::string& game_id, int rank = 0);
// The player's own PNGs the cover loader found but could not use (cut
// short: CoverWanted reads only their first bytes), each once until the
// covers are forgotten; now wanted from GameTDB after all. Menu thread.
std::vector<std::string> TakeBrokenOwnCovers();
// Whether the cover in memory for `name` is the player's own PNG (#384):
// then it is shown instead of GameTDB's full box art too (the shelf, the
// close-up). Never reads the card; false while it isn't in memory.
bool CoverIsOwn(const std::string& name);
// Whether the cover in memory for `name` is shown instead of the game's
// full box art (the shelf, the ring, the close-up, the game page): the
// player's own, or GameTDB's second set when chosen, which the full box
// (always the first set's) is not.
bool CoverBeforeBox(const std::string& name);
// Which reading of `name`'s slot the cover in memory is: another once it
// is read again (another set, a USB drive's), at the same address. 0
// while it isn't in memory.
unsigned CoverFill(const std::string& name);
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
// Which of GameTDB's cover sets the game shows (#384): the first, or the
// second (coverB) that some games have. Kept per game beside its cover
// (sd:/riftwii/covers/<ID>.second), so every download follows it.
bool CoverSecondSet(const std::string& game_id);
// False: the card could not be written.
bool ChooseCoverSet(const std::string& game_id, bool second);

// Drops what the pool remembers about the game (after a new download).
void ForgetCover(const std::string& game_id);
// The USB drive is read now (or again): every game cover in memory is
// read again, in case the drive has the player's own PNG (#384), another
// one than before, or none any more.
void ForgetGameCoversBeforeUsb();
// Drops every cover the pool holds and every "not on the card" it
// remembers: the SD card was put back and may be another one.
void ForgetAllCovers();
// Stops the loader reading the card, waiting for a read in flight: before
// the card is unmounted, read raw or IOS reloaded. Lifted by
// CoverLoaderRelease (the menu's ResumeGui).
void CoverLoaderHold();
void CoverLoaderRelease();

// A PNG file's bytes to RGBA rows (a cover, a theme's picture).
bool DecodePngRgba(const std::vector<std::uint8_t>& png, std::vector<std::uint8_t>& rgba, int& w, int& h,
                   std::string& error);

}  // namespace riftwii::wii
