// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpfat.hpp"

#include <cstring>
#include <utility>

namespace riftwii {
namespace {

std::uint16_t le16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

void put32(std::vector<std::uint8_t>& v, std::size_t at, std::uint32_t x) {
    v[at] = static_cast<std::uint8_t>(x >> 24);
    v[at + 1] = static_cast<std::uint8_t>(x >> 16);
    v[at + 2] = static_cast<std::uint8_t>(x >> 8);
    v[at + 3] = static_cast<std::uint8_t>(x);
}

}  // namespace

bool ctgp_parse_boot_sector(const std::uint8_t* s, std::size_t length, std::uint32_t start, CtgpFatVolume& out,
                            std::string& error) {
    if (length < 512) {
        error = "a boot sector is 512 bytes";
        return false;
    }
    if (s[0x1FE] != 0x55 || s[0x1FF] != 0xAA) {
        error = "no 0x55AA signature in the boot sector";
        return false;
    }
    const bool fat32 = std::memcmp(s + 0x52, "FAT", 3) == 0;
    if (!fat32 && std::memcmp(s + 0x36, "FAT", 3) != 0) {
        error = "the boot sector names no FAT file system";
        return false;
    }
    CtgpFatVolume v;
    v.start_sector = start;
    v.bytes_per_sector = le16(s + 0x0B);
    v.sectors_per_cluster = s[0x0D];
    v.reserved_sectors = le16(s + 0x0E);
    v.fat_count = s[0x10];
    v.root_entries = le16(s + 0x11);
    v.total_sectors = le16(s + 0x13);
    if (v.total_sectors == 0) v.total_sectors = le32(s + 0x20);
    v.sectors_per_fat = le16(s + 0x16);
    if (v.sectors_per_fat == 0) v.sectors_per_fat = le32(s + 0x24);
    if (fat32) {
        v.root_cluster = le32(s + 0x2C);
        v.ext_flags = le16(s + 0x28);
    }
    std::memcpy(v.label, s + (fat32 ? 0x47 : 0x2B), 11);
    if (v.bytes_per_sector == 0 || (v.bytes_per_sector % 512) != 0 || v.sectors_per_cluster == 0 ||
        v.sectors_per_fat == 0 || v.total_sectors == 0) {
        error = "the boot sector's sizes make no sense";
        return false;
    }
    out = v;
    return true;
}

bool ctgp_build_partition(const CtgpFatVolume& vol, const CtgpPartitionPlacement& at, std::vector<std::uint8_t>& out,
                          std::string& error) {
    namespace P = ctgp_part;
    if (vol.bytes_per_sector == 0 || vol.sectors_per_cluster == 0 || vol.sectors_per_fat == 0 ||
        vol.total_sectors == 0) {
        error = "the volume has no sizes";
        return false;
    }
    const std::uint32_t spc = (static_cast<std::uint32_t>(vol.sectors_per_cluster) * vol.bytes_per_sector) >> 9;
    if (spc == 0) {
        error = "a cluster smaller than a sector";
        return false;
    }
    std::uint32_t fat_start = vol.reserved_sectors + vol.start_sector;
    const std::uint32_t root_dir_start = fat_start + static_cast<std::uint32_t>(vol.fat_count) * vol.sectors_per_fat;
    const std::uint32_t data_start = ((static_cast<std::uint32_t>(vol.root_entries) << 5) >> 9) + root_dir_start;
    const std::uint32_t end_sector = vol.start_sector + vol.total_sectors;
    if (data_start < vol.start_sector || data_start - vol.start_sector > vol.total_sectors || end_sector < data_start) {
        error = "the data area starts past the volume's end";
        return false;
    }
    const std::uint32_t clusters = (end_sector - data_start) / spc;
    const std::uint64_t data_bytes = static_cast<std::uint64_t>(vol.total_sectors - (data_start - vol.start_sector)) * 512;

    std::uint32_t type = 3;
    std::uint32_t root_cluster = vol.root_cluster;
    if (clusters < 0xFF5) {
        type = 1;
        root_cluster = 0;
    } else if (clusters < 0xFFF5) {
        type = 2;
        root_cluster = 0;
    } else if ((vol.ext_flags & 0x80) == 0) {
        fat_start += (vol.ext_flags & 0xFu) * vol.sectors_per_fat;  // the active FAT
    }

    std::vector<std::uint8_t> p(kCtgpPartitionSize, 0);
    put32(p, P::disc, at.disc_interface);
    put32(p, P::cache, at.base + P::cache_object);
    put32(p, P::fs_type, type);
    put32(p, P::total_size, static_cast<std::uint32_t>(data_bytes >> 32));
    put32(p, P::total_size + 4, static_cast<std::uint32_t>(data_bytes));
    put32(p, P::root_dir_start, root_dir_start);
    put32(p, P::root_dir_cluster, root_cluster);
    put32(p, P::number_of_sectors, vol.total_sectors);
    put32(p, P::data_start, data_start);
    put32(p, P::bytes_per_sector, 512);
    put32(p, P::sectors_per_cluster, spc);
    put32(p, P::bytes_per_cluster, spc * 512);
    put32(p, P::fat_start, fat_start);
    put32(p, P::sectors_per_fat, vol.sectors_per_fat);
    put32(p, P::last_cluster, clusters + 1);
    put32(p, P::first_free, 2);
    put32(p, P::cwd_cluster, root_cluster);
    // open_file_count and first_open_file stay 0.
    p[P::read_only] = static_cast<std::uint8_t>(((at.features ^ 2u) >> 1) & 1u);
    std::memcpy(&p[P::label], vol.label, 11);  // p[P::label + 11] stays the NUL
    put32(p, P::lock, at.lock);
    put32(p, P::cache_pages_a, P::cache_page_count);
    put32(p, P::cache_object, at.disc_interface);   // the cache's own disc
    put32(p, P::cache_object + 4, end_sector);      // its end of partition
    put32(p, P::cache_object + 8, P::cache_page_count);
    put32(p, P::cache_object + 12, 8);              // sectors a page
    put32(p, P::cache_object + 16, at.base + P::cache_entries);
    for (std::size_t i = 0; i < P::cache_page_count; ++i) {
        const std::size_t e = P::cache_entries + i * P::cache_entry_size;
        put32(p, e, 0xFFFFFFFFu);  // no sector cached
        // count, last access and the dirty byte stay 0
        put32(p, e + 16, at.base + static_cast<std::uint32_t>(P::cache_pages + i * 0x1000));
    }
    out = std::move(p);
    return true;
}

bool ctgp_build_sda_partition(const BlockReader& reader, std::uint64_t volume_lba,
                              const CtgpPartitionPlacement& placement,
                              std::vector<std::uint8_t>& out, std::string& error) {
    if (!reader) {
        error = "no way to read the card";
        return false;
    }
    if (volume_lba > 0xFFFFFFFFull) {
        error = "volume starts beyond 2 TiB";
        return false;
    }
    std::vector<std::uint8_t> sector(512, 0);
    if (!reader(volume_lba, 1, sector.data())) {
        error = "could not read the boot sector";
        return false;
    }
    CtgpFatVolume vol;
    if (!ctgp_parse_boot_sector(sector.data(), sector.size(), static_cast<std::uint32_t>(volume_lba), vol, error))
        return false;
    return ctgp_build_partition(vol, placement, out, error);
}

}  // namespace riftwii
