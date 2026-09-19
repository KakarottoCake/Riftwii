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
