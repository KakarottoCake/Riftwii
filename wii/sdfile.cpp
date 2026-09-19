// SPDX-License-Identifier: GPL-3.0-or-later
#include "sdfile.hpp"

#include <sdcard/wiisd_io.h>

#include "log.hpp"

namespace riftwii::wii {
namespace {

Fat32Volume g_volume;
bool g_mounted = false;

bool read_blocks(std::uint64_t lba, std::uint32_t count, std::uint8_t* out) {
    if (lba > 0xFFFFFFFFull) return false;
    return __io_wiisd.readSectors(static_cast<sec_t>(lba), count, out);
}

bool ensure_mounted(std::string& error) {
    if (g_mounted) return true;
    if (!Fat32Volume::mount(&read_blocks, g_volume, error)) return false;
    const Fat32Geometry& g = g_volume.geometry();
    logf("SD volume: FAT32 at block %llu, %u-byte sectors, %u per cluster, %u clusters\n",
         static_cast<unsigned long long>(g.volume_lba), g.bytes_per_sector, g.sectors_per_cluster, g.cluster_count);
    g_mounted = true;
    return true;
}

}  // namespace

bool resolve_sd_directory(const std::string& sd_path, rtfat_volume& out, std::string& error) {
    if (sd_path.compare(0, 4, "sd:/") != 0 || sd_path.size() < 5) {
        error = "'" + sd_path + "' is not an sd:/ folder path";
        return false;
    }
    if (!ensure_mounted(error)) return false;
    const Fat32Geometry& g = g_volume.geometry();
    if (g.bytes_per_sector != 512) {
        error = "the SD card has " + std::to_string(g.bytes_per_sector) + "-byte sectors; savegame redirection needs 512";
        return false;
    }
    Fat32File dir;
    if (!g_volume.lookup(sd_path.substr(3), dir, error)) return false;
    if (!dir.entry.is_directory || dir.entry.first_cluster < 2) {
        error = "'" + sd_path + "' is not a directory";
        return false;
    }
    out = rtfat_volume{};
    out.sectors_per_cluster = g.sectors_per_cluster;
    out.fat_lba = static_cast<std::uint32_t>(g.volume_lba + g.reserved_sectors);
    out.fat_count = g.fat_count;
    out.fat_sectors = g.fat_sectors;
    out.data_lba = static_cast<std::uint32_t>(g.volume_lba + g.data_start_sector);
    out.cluster_count = g.cluster_count;
    out.dir_cluster = dir.entry.first_cluster;
    out.alloc_hint = 2;
    // FSInfo (boot sector word 0x30 names its sector): "RRaA" at 0,
    // "rrAa" at 0x1E4, next free cluster at 0x1EC.
    alignas(32) std::uint8_t sector[512];
    if (read_blocks(g.volume_lba, 1, sector)) {
        const std::uint32_t fsinfo = sector[0x30] | (sector[0x31] << 8);
        if (fsinfo != 0 && fsinfo != 0xFFFF && fsinfo < g.reserved_sectors && read_blocks(g.volume_lba + fsinfo, 1, sector)) {
            const auto le32 = [&](std::size_t at) {
                return static_cast<std::uint32_t>(sector[at]) | (static_cast<std::uint32_t>(sector[at + 1]) << 8) |
                       (static_cast<std::uint32_t>(sector[at + 2]) << 16) | (static_cast<std::uint32_t>(sector[at + 3]) << 24);
            };
            const std::uint32_t next_free = le32(0x1EC);
            if (le32(0) == 0x41615252u && le32(0x1E4) == 0x61417272u && next_free >= 2 &&
                next_free < g.cluster_count + 2) {
                out.alloc_hint = next_free;
            }
        }
    }
    logf("SD folder %s: cluster %u, %u clusters of %u sectors, FATs at %u (%u x %u sectors), data at %u, next free %u\n",
         sd_path.c_str(), out.dir_cluster, out.cluster_count, out.sectors_per_cluster, out.fat_lba, out.fat_count,
         out.fat_sectors, out.data_lba, out.alloc_hint);
    error.clear();
    return true;
}

bool resolve_sd_file(const std::string& sd_path, Fat32File& out, std::string& error) {
    if (sd_path.compare(0, 4, "sd:/") != 0) {
        error = "'" + sd_path + "' is not an sd:/ path";
        return false;
    }
    if (!ensure_mounted(error)) return false;
    if (!g_volume.lookup(sd_path.substr(3), out, error)) return false;
    if (out.entry.is_directory) {
        error = "'" + sd_path + "' is a directory";
        return false;
    }
    error.clear();
    return true;
}

}  // namespace riftwii::wii
