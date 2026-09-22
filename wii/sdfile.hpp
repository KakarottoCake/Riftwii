// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/fat32.hpp"
#include "rtfat.h"

// Where an SD file's bytes are on the card: the raw device sectors the
// resident runtime will read without a file system (riftwii/fat32.hpp's
// walker over libogc's SD driver, so the card must be mounted). Paths are
// the sd:/ paths the rest of the loader uses.
namespace riftwii::wii {

bool resolve_sd_file(const std::string& sd_path, Fat32File& out, std::string& error);
// The same, telling a missing file or folder (`missing`) from a card that
// cannot be read.
bool resolve_sd_file(const std::string& sd_path, Fat32File& out, bool& missing, std::string& error);
// A folder's entries, read raw like the lookups above (and cached with
// them).
bool list_sd_directory(const std::string& sd_path, std::vector<Fat32Entry>& out, bool& missing,
                       std::string& error);
// Bytes of a file found above, read raw from the card.
bool read_sd_file(const Fat32File& file, std::uint64_t offset, std::uint8_t* out, std::size_t length);
// Drops the folder listings and FAT blocks remembered by the lookups, for
// after libfat has written the card (a save folder created, say).
void forget_sd_layout();

// The volume as the resident FAT engine (runtime/rtfat.h) needs it and
// the first cluster of a directory on it, for the savegame redirect: the
// card must have 512-byte sectors (the engine's only size). The
// allocation hint is FSInfo's next-free cluster when the sector is valid,
// else 2. Reads the card raw, so it also works right after fatUnmount
// (which flushes libfat's cache) while the device is still up.
bool resolve_sd_directory(const std::string& sd_path, rtfat_volume& out, std::string& error);

}  // namespace riftwii::wii
