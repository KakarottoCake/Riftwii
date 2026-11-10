// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/fat32.hpp"

// The FAT partition objects CTGP-R's code in the game expects the loader to
// have mounted already (docs/CTGPR.md): three libfat-style PARTITION
// structures of 0x18260 bytes, for "sda:" (the SD card), "efa:" (the
// encrypted package disk, blob.bin) and "efb:" (jelly.bin). The layout
// follows the channel's partition constructor.
namespace riftwii {

constexpr std::uint32_t kCtgpPartitionSize = 0x18260;

// What a FAT boot sector says, as the constructor reads it.
struct CtgpFatVolume {
    std::uint32_t start_sector = 0;       // first sector of the volume on its device
    std::uint16_t bytes_per_sector = 0;   // BPB; the object always counts 512-byte sectors
    std::uint8_t sectors_per_cluster = 0;
    std::uint16_t reserved_sectors = 0;
    std::uint8_t fat_count = 0;
    std::uint16_t root_entries = 0;
    std::uint32_t total_sectors = 0;      // 16-bit field, else the 32-bit one
    std::uint32_t sectors_per_fat = 0;    // 16-bit field, else the 32-bit one
    std::uint32_t root_cluster = 0;       // FAT32
    std::uint16_t ext_flags = 0;          // FAT32: bit 7 set = one FAT is active, low bits say which
    char label[11] = {};
};

// Reads the boot sector (512 bytes at least) the way the constructor does:
// the 0x55AA signature, "FAT" at 0x36 (FAT12/16) or 0x52 (FAT32). `start` is
// the volume's first sector on its device. False with `error` for a sector
// that is not a FAT boot sector or has sizes that make no sense.
bool ctgp_parse_boot_sector(const std::uint8_t* sector, std::size_t length, std::uint32_t start,
                            CtgpFatVolume& out, std::string& error);

// Where the object goes in the game and what it points at.
struct CtgpPartitionPlacement {
    std::uint32_t base = 0;            // address the object will have in the game
    std::uint32_t disc_interface = 0;  // its DISC_INTERFACE (io type, features, 6 functions)
    std::uint32_t features = 0;        // that interface's features word: bit 1 write, bit 0 read
    std::uint32_t lock = 0;            // partition+0x60; the in-game code never reads it, so 0 is fine
};

// The object's 0x18260 bytes, big endian, pointers absolute from `base`: the
// volume's numbers, an empty 24-page sector cache (8 sectors a page) and no
// open files.
bool ctgp_build_partition(const CtgpFatVolume& volume, const CtgpPartitionPlacement& placement,
                          std::vector<std::uint8_t>& out, std::string& error);

// The "sda:" object for the SD card: reads the boot sector of the volume at
// `volume_lba` (Fat32Volume::geometry().volume_lba) through `reader` and
// builds the partition from it. The sector numbers stay absolute on the card.
// 512-byte sectors only, which is all libfat's SD driver speaks.
bool ctgp_build_sda_partition(const BlockReader& reader, std::uint64_t volume_lba,
                              const CtgpPartitionPlacement& placement,
                              std::vector<std::uint8_t>& out, std::string& error);

// Byte offsets inside the object (for the tests and for whoever reads a
// dump of the game's memory).
namespace ctgp_part {
constexpr std::size_t disc = 0x00;
constexpr std::size_t cache = 0x04;
constexpr std::size_t fs_type = 0x08;          // 1 FAT12, 2 FAT16, 3 FAT32
constexpr std::size_t total_size = 0x10;       // 64 bit: bytes of data area
constexpr std::size_t root_dir_start = 0x18;
constexpr std::size_t root_dir_cluster = 0x1C;
constexpr std::size_t number_of_sectors = 0x20;
constexpr std::size_t data_start = 0x24;
constexpr std::size_t bytes_per_sector = 0x28;
constexpr std::size_t sectors_per_cluster = 0x2C;
constexpr std::size_t bytes_per_cluster = 0x30;
constexpr std::size_t fat_start = 0x34;
constexpr std::size_t sectors_per_fat = 0x38;
constexpr std::size_t last_cluster = 0x3C;
constexpr std::size_t first_free = 0x40;
constexpr std::size_t cwd_cluster = 0x44;
constexpr std::size_t open_file_count = 0x48;
constexpr std::size_t first_open_file = 0x4C;
constexpr std::size_t read_only = 0x50;        // byte
constexpr std::size_t label = 0x51;            // 11 bytes and a NUL
constexpr std::size_t lock = 0x60;
constexpr std::size_t cache_pages_a = 0x68;
constexpr std::size_t cache_object = 0x6C;     // disc, end sector, pages, sectors a page, entries
constexpr std::size_t cache_entries = 0x80;    // 24 x { sector, count, last access, dirty byte, page }
constexpr std::size_t cache_pages = 0x260;     // 24 x 0x1000
constexpr std::size_t cache_page_count = 24;
constexpr std::size_t cache_entry_size = 20;
}  // namespace ctgp_part

}  // namespace riftwii
