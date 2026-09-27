// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/wbfspart.hpp"

#include <algorithm>
#include <cstring>

#include "riftwii/disc.hpp"
#include "riftwii/imagevolume.hpp"

namespace riftwii {
namespace {

constexpr std::uint32_t kHeaderBytes = 12;          // magic, sector count, two shifts, version, padding
constexpr std::uint64_t kWiiDiscSectors = 143432ull * 2;  // 32 KiB sectors of a dual-layer disc
constexpr std::uint32_t kDiscHeaderCopy = 0x100;

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

bool is_wbfs(const std::uint8_t* block) { return std::memcmp(block, "WBFS", 4) == 0; }

// Reads the header at `lba`; false with `error` empty when it is not WBFS.
bool try_partition(const BlockReader& reader, std::uint64_t lba, std::uint64_t partition_blocks,
                   WbfsPartition& out, std::string& error) {
    error.clear();
    std::uint8_t block[512];
    if (!reader(lba, 1, block) || !is_wbfs(block)) return false;
    WbfsPartition p;
    p.lba = lba;
    if (!parse_wbfs_header(block, p.layout, error)) {
        error = "WBFS at block " + std::to_string(lba) + ": " + error;
        return false;
    }
    if (partition_blocks != 0 && p.layout.hd_sectors > partition_blocks) {
        error = "WBFS at block " + std::to_string(lba) + " claims " + std::to_string(p.layout.hd_sectors) +
                " sectors but its partition holds " + std::to_string(partition_blocks);
        return false;
    }
    out = std::move(p);
    return true;
}

}  // namespace

bool parse_wbfs_header(const std::uint8_t* block, WbfsLayout& out, std::string& error) {
    if (!is_wbfs(block)) {
        error = "WBFS magic is missing";
        return false;
    }
    WbfsLayout l;
    l.hd_sectors = be32(block + 4);
    const std::uint8_t hd_shift = block[8], wbfs_shift = block[9];
    if (hd_shift != 9) {
        error = "its sector-size shift is " + std::to_string(hd_shift) + "; only 512-byte sectors are supported";
        return false;
    }
    if (wbfs_shift < 15 || wbfs_shift > 30) {
        error = "its block-size shift " + std::to_string(wbfs_shift) + " is out of range";
        return false;
    }
    l.block_sectors = std::uint32_t(1) << (wbfs_shift - 9);
    l.blocks = l.hd_sectors >> (wbfs_shift - 9);
    // Block 0 holds the tables; libwbfs sizes the blocks so a u16 names each.
    if (l.blocks < 2 || l.blocks > 0x10000) {
        error = "its " + std::to_string(l.blocks) + " blocks of " + std::to_string(l.block_sectors * 512ull) +
                " bytes do not make a valid WBFS";
        return false;
    }
    l.disc_blocks = static_cast<std::uint32_t>(kWiiDiscSectors >> (wbfs_shift - 15));
    l.disc_info_sectors = (kDiscHeaderCopy + l.disc_blocks * 2 + 511) / 512;
    // libwbfs keeps the free-block bitmap at the end of block 0, and the
    // disc infos fill the blocks between the header and it.
    const std::uint64_t block_bytes = std::uint64_t(l.block_sectors) * 512;
    const std::uint64_t bitmap_bytes = l.blocks / 8;
    const std::uint64_t bitmap_lba = (block_bytes - bitmap_bytes) / 512;
    const std::uint64_t fit = bitmap_lba > 1 ? (bitmap_lba - 1) / l.disc_info_sectors : 0;
    l.slots = static_cast<std::uint32_t>(std::min<std::uint64_t>(fit, 512 - kHeaderBytes));
    if (l.slots == 0) {
        error = "its first block has no room for a disc table";
        return false;
    }
    for (std::uint32_t i = 0; i < l.slots; ++i) {
        if (block[kHeaderBytes + i] != 0) l.used.push_back(i);
    }
    out = std::move(l);
    error.clear();
    return true;
}

bool find_wbfs_partition(const BlockReader& reader, WbfsPartition& out, std::string& error) {
    if (!reader) {
        error = "no block reader";
        return false;
    }
    std::uint8_t mbr[512];
    if (!reader(0, 1, mbr)) {
        error = "cannot read block 0";
        return false;
    }
    if (try_partition(reader, 0, 0, out, error)) return true;
    if (!error.empty()) return false;
    for (const DrivePartition& p : drive_partitions(reader, mbr)) {
        if (try_partition(reader, p.lba, p.blocks, out, error)) return true;
        if (!error.empty()) return false;
    }
    error = "no WBFS partition found";
    return false;
}

bool list_wbfs_discs(const BlockReader& reader, const WbfsPartition& partition, std::vector<WbfsDisc>& out,
                     std::vector<std::string>& skipped, std::string& error) {
    out.clear();
    skipped.clear();
    const WbfsLayout& l = partition.layout;
    bool any_read = false;
    for (std::uint32_t slot : l.used) {
        const std::uint64_t lba = partition.lba + 1 + std::uint64_t(slot) * l.disc_info_sectors;
        std::uint8_t block[512];
        if (!reader(lba, 1, block)) {
            skipped.push_back("slot " + std::to_string(slot) + ": cannot read its disc info");
            continue;
        }
        any_read = true;
        DiscHeader header;
        std::string why;
        if (!parse_disc_header(block, sizeof(block), header, why) || !header.wii_magic) {
            // Named by what it holds: loaders keep their own entries in
            // slots too (Configurable USB Loader's "__CFG_" settings).
            std::string seen;
            for (std::size_t i = 0; i < 6; ++i) seen += (block[i] >= 0x20 && block[i] < 0x7F) ? char(block[i]) : '?';
            skipped.push_back("slot " + std::to_string(slot) + " (\"" + seen + "\"): " +
                              (why.empty() ? "not a Wii disc" : why));
            continue;
        }
        out.push_back({slot, header.game_id, header.title});
    }
    if (!l.used.empty() && !any_read) {
        error = "cannot read the WBFS disc infos";
        return false;
    }
    error.clear();
    return true;
}

}  // namespace riftwii
