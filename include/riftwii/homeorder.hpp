// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Home's own order (Settings > Home order: Your order), which the player
// sets by moving the tiles: sd:/riftwii/order.txt, one game a line, the
// first tile first. A game is named by where it is and its ID ("usb:RMCE01",
// "sd:SMNE01", a WiiWare or Virtual Console channel "wii:WBME01"; a second
// disc "usb:R8PE01/2"), so the same game on the SD card and on a USB drive
// has a place of its own, as it has a tile of its own. Games the file does not list come after the listed ones, A to Z.
// The names of games that are gone stay in the file, so a drive taken out
// for a while does not lose its games' places.
namespace riftwii {

// At most this much of the file is read (a larger one is read up to
// here), and at most this many games are kept: a drive holds a few
// hundred, and 2048 names take about 30 KiB.
constexpr std::size_t kHomeOrderMaxBytes = 64u << 10;
constexpr std::size_t kHomeOrderMaxGames = 2048;

// `place`: "usb", "sd" or "wii" (a channel on the Wii, its own NAND or an
// emulated one). `game_id` may be a profile's tile key on a drive
// ("RMCE01@retro-rewind", riftwii/profiles.hpp): its tile has a place of
// its own. `disc_number` as the disc header has it (0 the first disc).
std::string home_order_key(const std::string& place, const std::string& game_id, unsigned disc_number);

// The games named, in the file's order. Lenient: blank lines, comments
// ('#'), lines that are no game's name and a game named twice (its first
// place kept) are skipped, and reading stops at kHomeOrderMaxGames.
std::vector<std::string> parse_home_order(const std::string& text);
// The first kHomeOrderMaxBytes of a larger file, as read: cut back to its
// last whole line, so a name cut in two ("usb:RMCE0") is not read as
// another game. Text that was read whole is kept as it is.
std::string home_order_whole_lines(const std::string& head, bool cut);
// The file, with a comment line first; at most kHomeOrderMaxGames games.
std::string serialize_home_order(const std::vector<std::string>& order);

// The tiles' order for `keys` (the games on the drives, A to Z): the
// indices into `keys`, the games `order` lists first in its order, the
// rest after them as they were.
std::vector<std::size_t> arrange_home_order(const std::vector<std::string>& order, const std::vector<std::string>& keys);

// `first` followed by the names in `rest` it does not have yet: the
// stored order completed with the games it does not list yet, or, when
// the player first moves a tile in another order, that order with the
// file's names of games that are gone after it.
std::vector<std::string> merge_home_order(const std::vector<std::string>& first, const std::vector<std::string>& rest);

// After a move among the games shown (`shown`, in their new order; a view
// may show only some of the games, such as the ones with mods): `order`,
// which names every game shown, with the places those games had refilled
// in their new order. Every other game keeps its place.
std::vector<std::string> refill_home_order(const std::vector<std::string>& order, const std::vector<std::string>& shown);

}  // namespace riftwii
