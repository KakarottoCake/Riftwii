// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/fat32.hpp"

namespace riftwii {

// A drive formatted as WBFS itself, with no file system: what the WBFS
// tools call a WBFS partition. Games sit in slots of the
// partition's own table instead of files, so the drive is read here, not
// through ImageVolume. Read-only, like the FAT32 and NTFS readers.
//
// The layout is libwbfs's. The partition's first 512-byte block holds
// "WBFS", its size in 512-byte sectors (big-endian), the shifts of the
// sector size and of the WBFS block size, then from byte 12 one byte per
// slot (non-zero: used). Slot i's disc info starts at block
// 1 + i * disc_info_sectors: a copy of the disc's first 0x100 bytes, then
// a big-endian u16 per WBFS block of the disc (0: not stored), each naming
// the partition's WBFS block that holds it.

struct WbfsLayout {
    std::uint32_t hd_sectors = 0;         // the partition, in 512-byte sectors
    std::uint32_t block_sectors = 0;      // 512-byte sectors per WBFS block
    std::uint32_t blocks = 0;             // WBFS blocks in the partition
    std::uint32_t disc_blocks = 0;        // entries in a disc's block table
    std::uint32_t disc_info_sectors = 0;  // one slot's disc info
    std::uint32_t slots = 0;              // slots in the disc table
    std::vector<std::uint32_t> used;      // used slots, in table order
};

// Checks a partition's first block and works out its layout. d2x reads the
// drive in 512-byte sectors, so a WBFS made for other sector sizes is refused.
bool parse_wbfs_header(const std::uint8_t* block, WbfsLayout& out, std::string& error);

struct WbfsPartition {
    std::uint64_t lba = 0;  // device block of the WBFS header
    WbfsLayout layout;
};

// Finds a WBFS partition by its content, never by the partition type (WBFS
// tools leave FAT16's and others): at block 0 of a drive with no partition
// table, or at the start of a partition the MBR or GPT lists. A WBFS that
// is damaged or larger than its partition is refused with the reason.
bool find_wbfs_partition(const BlockReader& reader, WbfsPartition& out, std::string& error);

struct WbfsDisc {
    std::uint32_t slot = 0;
    std::string id;     // ID6, from the disc info's copy of the disc header
    std::string title;  // the disc header's internal name
};

// The discs in the used slots, in slot order. A slot whose disc info is
// unreadable or not a Wii disc's is left out and named in `skipped`, so one
// damaged slot does not hide the rest; false only when nothing can be read.
bool list_wbfs_discs(const BlockReader& reader, const WbfsPartition& partition, std::vector<WbfsDisc>& out,
                     std::vector<std::string>& skipped, std::string& error);

}  // namespace riftwii
