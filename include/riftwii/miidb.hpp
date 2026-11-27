// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The Wii's Mii database (/shared2/menu/FaceLib/RFL_DB.dat), one Mii at a
// time: listing them by name, copying one into another database and taking
// one out, for Settings > Games > Manage emulated NAND > Miis.
//
// The layout, from WiiBrew's public notes on the file and checked against a
// real console's copy: "RNOD", then 100 slots of 0x4A bytes, an empty slot
// all zeros; each Mii's name (10 UTF-16BE characters) at 0x02 and its Mii ID
// (4 bytes, which game saves use to find it) at 0x18. A CRC-16 (CCITT,
// XMODEM: polynomial 0x1021, starting at 0) of the first 0x1F1DE bytes is
// stored big-endian at 0x1F1DE. The rest of the file (the Mii Parade) is
// left as it is.
namespace riftwii {

constexpr std::size_t kMiiSlots = 100;
constexpr std::size_t kMiiSize = 0x4A;
constexpr std::size_t kMiiFirst = 4;              // the first slot, after "RNOD"
constexpr std::size_t kMiiCrcAt = 0x1F1DE;       // the CRC, and the bytes it covers
constexpr std::size_t kMiiDbMinSize = kMiiCrcAt + 2;
constexpr std::size_t kMiiDbMaxSize = 1024 * 1024;  // the Wii's own is 0xBE6C0

struct MiiEntry {
    std::size_t slot = 0;
    std::string name;                 // UTF-8; empty when the Mii has none
    std::array<std::uint8_t, 4> id{};  // the Mii ID
};

// CRC-16/XMODEM, as the database's check.
std::uint16_t mii_db_crc(const std::uint8_t* data, std::size_t size);

// Whether `db` is a Mii database whose check matches: the right magic, at
// least kMiiDbMinSize bytes and at most kMiiDbMaxSize. `error` says why not.
bool mii_db_valid(const std::vector<std::uint8_t>& db, std::string& error);

// The Miis in a valid database, by slot.
std::vector<MiiEntry> mii_db_list(const std::vector<std::uint8_t>& db);

// Where a copy of the Mii in `from`'s slot goes in `to`: the slot that
// already holds a Mii with the same ID (it is replaced, and keeps its
// place), else the first empty one. -1 when `to` is full.
int mii_db_target_slot(const std::vector<std::uint8_t>& from, std::size_t slot, const std::vector<std::uint8_t>& to);

// Copies the Mii in `from`'s `slot` into `to` (mii_db_target_slot), its
// check made again. Both must be valid. `placed` is the slot it went to.
bool mii_db_copy(const std::vector<std::uint8_t>& from, std::size_t slot, std::vector<std::uint8_t>& to,
                 std::size_t& placed, std::string& error);

// Empties `slot` of `db` and makes its check again.
bool mii_db_remove(std::vector<std::uint8_t>& db, std::size_t slot, std::string& error);

}  // namespace riftwii
