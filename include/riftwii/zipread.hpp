// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// A reader for the .zip files of the theme browser (wii/menu/themestore.hpp),
// which come from the internet and so are untrusted: every offset and size
// is checked against the file before it is used, and nothing is unpacked
// into memory as a whole. The archive is read through its central
// directory, in a buffer the caller owns. Stored and deflated entries only;
// encrypted, split and zip64 archives are refused.
namespace riftwii {

struct ZipEntry {
    std::string name;
    std::uint16_t method = 0;        // 0 stored, 8 deflated
    std::uint32_t crc = 0;           // of the unpacked bytes
    std::uint32_t packed_size = 0;
    std::uint32_t unpacked_size = 0;
    std::size_t data_offset = 0;     // of the packed bytes, in the archive
    bool directory = false;
};

// Reads the central directory of the archive in `data`. Fails (with `error`
// in plain words) for anything that is not a whole, plain zip, for more
// than `max_entries` entries, and for a packed range outside the file.
bool zip_read_directory(const std::uint8_t* data, std::size_t size, std::vector<ZipEntry>& entries,
                        std::string& error, std::size_t max_entries = 1000);

// What a theme's zip may hold. `files` and `unpacked_size` come from the
// theme list (what the maintainers' checks counted); the hard caps stand
// over them, so a list that promises more than RiftWii will take is refused.
struct ZipThemeLimits {
    std::size_t files = 0;
    std::uint64_t unpacked_size = 0;
    std::size_t max_files = 64;
    std::uint64_t max_unpacked_size = 24u << 20;
    std::uint64_t max_file_size = 8u << 20;
};

struct ZipThemeFile {
    std::string file;       // the plain name inside the theme's folder
    std::size_t entry = 0;  // its index in the directory
};

// Whether `name` is an entry path a theme's zip may have: "<folder>/<file>",
// nothing else. Not absolute, no drive letter, no backslash, no "..", no
// empty or hidden part, no control character, one folder deep, and `file`
// a plain file name (themepack.hpp's theme_pack_name_ok). `file` is the
// part after the folder.
bool zip_theme_path_ok(const std::string& name, const std::string& folder, std::string& file);

// The files to unpack from `entries` into the theme's folder: every entry's
// path checked (zip_theme_path_ok; the theme's own folder entry is
// skipped), no name twice (FAT does not tell A from a), theme.ini present,
// and the counts and sizes within `limits`.
bool zip_plan_theme(const std::vector<ZipEntry>& entries, const std::string& folder, const ZipThemeLimits& limits,
                    std::vector<ZipThemeFile>& plan, std::string& error);

// Receives the unpacked bytes in order; false stops the unpacking.
using ZipSink = std::function<bool(const std::uint8_t* data, std::size_t size)>;
// Inflates raw deflate data (RFC 1951) of `in_size` bytes into `sink`,
// producing exactly `expected` bytes. The Wii's is zlib's.
using ZipInflater = bool (*)(const std::uint8_t* in, std::size_t in_size, std::uint64_t expected,
                             const ZipSink& sink);

// Unpacks `entry` of the archive into `sink`: stored bytes are passed on,
// deflated ones through `inflate`. Checks that the entry has exactly
// its declared size and CRC-32. False with `error` otherwise.
bool zip_extract(const std::uint8_t* data, std::size_t size, const ZipEntry& entry, ZipInflater inflate,
                 const ZipSink& sink, std::string& error);

// The CRC-32 (zip's, the PNG one) of more bytes after `crc`: start from 0.
std::uint32_t zip_crc32_update(std::uint32_t crc, const std::uint8_t* data, std::size_t size);

}  // namespace riftwii
