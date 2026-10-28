// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2009 Kwiirk (libwbfs, the WBFS layout)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "riftwii/overlay.hpp"

// Copying a Wii disc to a .wbfs file, as USB loaders do (WiiFlow Lite's
// "Install game"), so it can be played from the SD card or a USB drive
// without the disc.
//
// A .wbfs file keeps only the parts of the disc that are used: the disc is
// cut into 2 MiB blocks and a block is stored only when something on it is
// read by a game or by the Wii. What is used is worked out here, for every
// partition the disc lists in any of its four partition groups (the game,
// the system update, a channel installer, and any other), never only the
// game's: nothing a partition needs is left out. Inside a partition that
// is the ticket, TMD, certificates and hash tables in front of its data,
// then the clusters that hold its boot header, apploader, main program,
// file table and every file the table names. A partition whose files
// cannot be read (no console key, a Korean or vWii ticket, or a file table
// that does not parse) is kept whole, from the start of its data to its
// end as the partition header gives it.
//
// The file is laid out as libwbfs writes one (its wbfs_add_disc and
// wbfs_trim): a 512-byte header sector, one disc slot, 2 MiB blocks, the
// disc's blocks stored in disc order from the file's second block on, and
// the file ending after the last stored block. RiftWii's own reader
// (riftwii/usbgame.hpp) and the other loaders read it. A file larger than
// FAT32 allows is split into .wbfs, .wbf1, .wbf2 ... pieces of
// kWbfsSplitBytes each, as USB Loader GX and WiiFlow split theirs.
namespace riftwii {

constexpr std::uint32_t kWiiSectorBytes = 0x8000;
constexpr std::uint32_t kWiiSingleLayerSectors = 143432;
constexpr std::uint32_t kWiiDualLayerSectors = kWiiSingleLayerSectors * 2;
constexpr std::uint32_t kWbfsFileBlockShift = 21;  // 2 MiB blocks, libwbfs's choice for a disc-sized file
constexpr std::uint32_t kWbfsFileBlockBytes = 1u << kWbfsFileBlockShift;
constexpr std::uint32_t kWbfsSectorsPerBlock = kWbfsFileBlockBytes / kWiiSectorBytes;
// Entries in the block table of a disc slot (a dual-layer disc's blocks).
constexpr std::uint32_t kWbfsFileDiscBlocks = kWiiDualLayerSectors / kWbfsSectorsPerBlock;
// 4 GiB less 32 KiB: the largest piece FAT32 holds, rounded down to a Wii
// sector, as the other loaders split.
constexpr std::uint64_t kWbfsSplitBytes = (std::uint64_t(4) << 30) - 32 * 1024;

// One partition of the disc and how much of it the copy keeps.
struct DiscUsagePartition {
    std::uint64_t offset = 0;  // absolute disc offset
    std::uint32_t type = 0;    // 0 game, 1 update, 2 channel installer; others are kept as well
    std::uint32_t group = 0;   // which of the four partition groups listed it
    bool whole = false;        // its data is kept whole: `why` says why its files were not read
    std::string why;
};

struct DiscUsage {
    std::vector<bool> sectors;  // one per 32 KiB disc sector, kWiiDualLayerSectors of them
    std::vector<DiscUsagePartition> partitions;
    std::uint32_t used_sectors() const;
};

// Works out the sectors of `disc` (the raw, encrypted disc) that a copy
// needs. `common_key` is the console's (wii/menu/otpkey.hpp); null keeps
// every partition whole. False only when the disc is not a Wii disc or its
// partition table cannot be read; a partition that cannot be looked into
// is kept whole and noted, never dropped.
bool map_disc_usage(const ByteSource& disc, const std::uint8_t* common_key, DiscUsage& out, std::string& error);

// Where each used block of the disc goes in the file.
struct WbfsImagePlan {
    std::vector<std::uint16_t> block_table;  // per disc block: its block in the file, 0 not stored
    std::vector<std::uint32_t> stored;       // the disc blocks stored, in file order
    std::uint64_t file_bytes = 0;            // the whole image, every piece together
};
bool plan_wbfs_image(const DiscUsage& usage, WbfsImagePlan& out, std::string& error);

// The image's first bytes: the header sector and the disc slot's info (the
// first 0x100 bytes of the disc, then the block table). The rest of the
// file's first block is zeros: a full free-block map, as libwbfs leaves a
// trimmed file.
std::vector<std::uint8_t> wbfs_image_head(const std::uint8_t disc_header[0x100], const WbfsImagePlan& plan);

// The pieces of a split image: "x.wbfs", then "x.wbf1", "x.wbf2" ...
std::string wbfs_piece_path(const std::string& primary, unsigned index);
unsigned wbfs_piece_count(std::uint64_t file_bytes, std::uint64_t split_bytes = kWbfsSplitBytes);

// The folder a copied game gets in wbfs/, as the other loaders name it:
// "<title> [<ID6>]". The title loses the characters a FAT32 name cannot
// hold, and its trailing dots and spaces; an empty one becomes the ID.
std::string wbfs_game_folder_name(const std::string& title, const std::string& id6);

// Where the image's bytes go, in order. The Wii's writes the pieces of the
// split image on the SD card or a USB drive.
class WbfsSink {
public:
    virtual ~WbfsSink() = default;
    virtual bool write(const std::uint8_t* data, std::size_t length, std::string& error) = 0;
};

// Told the bytes read from the disc so far and in all; false stops the copy.
using DiscCopyProgress = std::function<bool(std::uint64_t done, std::uint64_t total)>;

// Reads the plan's blocks from `disc` and writes the image to `sink`. A
// sector the copy needs must read; one that only shares a block with them
// and does not read (past the end of a single-layer disc) is written as
// zeros. False with `error` on a read or write that failed, or when
// `progress` stopped it.
bool write_wbfs_image(const ByteSource& disc, const WbfsImagePlan& plan, const DiscUsage& usage, WbfsSink& sink,
                      const DiscCopyProgress& progress, std::string& error);

}  // namespace riftwii
