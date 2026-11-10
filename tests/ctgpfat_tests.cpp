// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The FAT partition objects CTGP-R's in-game code expects (riftwii/ctgpfat.hpp).
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/ctgpfat.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

// The first sector of CTGP-R's package disk (sd:/ctgpr/blob.bin, "efa:")
// once decrypted: the BPB of a FAT32 volume, 512 bytes a sector, 8 sectors a
// cluster, 8 reserved sectors, one FAT of 4096 sectors, root cluster 2.
std::vector<std::uint8_t> efa_sector() {
    std::vector<std::uint8_t> s(512, 0);
    const char* oem = "\0\0\0CTGP-EFS";
    std::memcpy(s.data(), oem, 3);
    std::memcpy(s.data() + 3, "CTGP-EFS", 8);
    s[0x0B] = 0x00; s[0x0C] = 0x02;       // 512 bytes a sector
    s[0x0D] = 8;                          // sectors a cluster
    s[0x0E] = 8; s[0x0F] = 0;             // reserved sectors
    s[0x10] = 1;                          // FATs
    s[0x15] = 0xF8;                       // media
    s[0x18] = 0x3F; s[0x1A] = 0xFF;
    s[0x22] = 0x40;                       // 32-bit total sectors: 0x00400000
    s[0x25] = 0x10;                       // 32-bit sectors a FAT: 0x00001000
    s[0x2C] = 2;                          // root cluster
    std::memcpy(s.data() + 0x47, "CTGP-EFA000", 11);
    std::memcpy(s.data() + 0x52, "FAT32   ", 8);
    s[0x1FE] = 0x55; s[0x1FF] = 0xAA;
    return s;
}

std::uint32_t be32(const std::vector<std::uint8_t>& v, std::size_t at) {
    return (static_cast<std::uint32_t>(v[at]) << 24) | (static_cast<std::uint32_t>(v[at + 1]) << 16) |
           (static_cast<std::uint32_t>(v[at + 2]) << 8) | v[at + 3];
}

}  // namespace

static void test_parse() {
    CtgpFatVolume v;
    std::string error;
    const auto s = efa_sector();
    EXPECT_TRUE(ctgp_parse_boot_sector(s.data(), s.size(), 0, v, error));
    EXPECT_EQ(v.bytes_per_sector, 512);
    EXPECT_EQ(static_cast<int>(v.sectors_per_cluster), 8);
    EXPECT_EQ(v.reserved_sectors, 8);
    EXPECT_EQ(static_cast<int>(v.fat_count), 1);
    EXPECT_EQ(v.total_sectors, 0x400000u);
    EXPECT_EQ(v.sectors_per_fat, 0x1000u);
    EXPECT_EQ(v.root_cluster, 2u);
    EXPECT_EQ(std::string(v.label, 11), std::string("CTGP-EFA000"));
}

static void test_parse_refuses() {
    CtgpFatVolume v;
    std::string error;
    auto s = efa_sector();
    s[0x1FF] = 0;
    EXPECT_FALSE(ctgp_parse_boot_sector(s.data(), s.size(), 0, v, error));
    EXPECT_TRUE(error.find("0x55AA") != std::string::npos);
    s = efa_sector();
    std::memcpy(s.data() + 0x52, "NTFS    ", 8);
    EXPECT_FALSE(ctgp_parse_boot_sector(s.data(), s.size(), 0, v, error));
    s = efa_sector();
    s[0x0D] = 0;
    EXPECT_FALSE(ctgp_parse_boot_sector(s.data(), s.size(), 0, v, error));
    EXPECT_FALSE(ctgp_parse_boot_sector(s.data(), 100, 0, v, error));
}

static void test_efa_partition() {
    CtgpFatVolume v;
    std::string error;
    const auto s = efa_sector();
    EXPECT_TRUE(ctgp_parse_boot_sector(s.data(), s.size(), 0, v, error));
    CtgpPartitionPlacement at;
    at.base = 0x816CD9A0;
    at.disc_interface = 0x816B12E0;
    at.features = 3;
    at.lock = 0x1234;
    std::vector<std::uint8_t> p;
    EXPECT_TRUE(ctgp_build_partition(v, at, p, error));
    EXPECT_EQ(p.size(), static_cast<std::size_t>(0x18260));
    EXPECT_EQ(be32(p, ctgp_part::disc), 0x816B12E0u);
    EXPECT_EQ(be32(p, ctgp_part::cache), 0x816CD9A0u + 0x6C);
    EXPECT_EQ(be32(p, ctgp_part::fs_type), 3u);
    EXPECT_EQ(be32(p, ctgp_part::fat_start), 8u);
    EXPECT_EQ(be32(p, ctgp_part::root_dir_start), 4104u);  // 8 + one FAT of 4096
    EXPECT_EQ(be32(p, ctgp_part::data_start), 4104u);
    EXPECT_EQ(be32(p, ctgp_part::root_dir_cluster), 2u);
    EXPECT_EQ(be32(p, ctgp_part::cwd_cluster), 2u);
    EXPECT_EQ(be32(p, ctgp_part::number_of_sectors), 0x400000u);
    EXPECT_EQ(be32(p, ctgp_part::bytes_per_sector), 512u);
    EXPECT_EQ(be32(p, ctgp_part::sectors_per_cluster), 8u);
    EXPECT_EQ(be32(p, ctgp_part::bytes_per_cluster), 4096u);
    EXPECT_EQ(be32(p, ctgp_part::sectors_per_fat), 0x1000u);
    EXPECT_EQ(be32(p, ctgp_part::last_cluster), (0x400000u - 4104u) / 8 + 1);
    EXPECT_EQ(be32(p, ctgp_part::first_free), 2u);
    EXPECT_EQ(be32(p, ctgp_part::open_file_count), 0u);
    EXPECT_EQ(be32(p, ctgp_part::first_open_file), 0u);
    EXPECT_EQ(static_cast<int>(p[ctgp_part::read_only]), 0);  // features 3: writable
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(&p[ctgp_part::label]), 11), std::string("CTGP-EFA000"));
    EXPECT_EQ(static_cast<int>(p[ctgp_part::label + 11]), 0);
    EXPECT_EQ(be32(p, ctgp_part::lock), 0x1234u);
    // 64-bit size of the data area: (0x400000 - 4104) sectors of 512 bytes.
    EXPECT_EQ(be32(p, ctgp_part::total_size), 0u);
    EXPECT_EQ(be32(p, ctgp_part::total_size + 4), (0x400000u - 4104u) * 512u);
    // The cache: 24 pages of 8 sectors, none holding a sector yet.
    EXPECT_EQ(be32(p, ctgp_part::cache_object), 0x816B12E0u);
    EXPECT_EQ(be32(p, ctgp_part::cache_object + 4), 0x400000u);
    EXPECT_EQ(be32(p, ctgp_part::cache_object + 8), 24u);
    EXPECT_EQ(be32(p, ctgp_part::cache_object + 12), 8u);
    EXPECT_EQ(be32(p, ctgp_part::cache_object + 16), 0x816CD9A0u + 0x80);
    for (std::size_t i = 0; i < 24; ++i) {
        const std::size_t e = ctgp_part::cache_entries + i * 20;
        EXPECT_EQ(be32(p, e), 0xFFFFFFFFu);
        EXPECT_EQ(be32(p, e + 4), 0u);
        EXPECT_EQ(be32(p, e + 8), 0u);
        EXPECT_EQ(be32(p, e + 16), 0x816CD9A0u + 0x260 + static_cast<std::uint32_t>(i) * 0x1000);
    }
    // The last page ends where the object does.
    EXPECT_EQ(0x260u + 24u * 0x1000u, 0x18260u);
}

static void test_read_only_follows_features() {
    CtgpFatVolume v;
    std::string error;
    const auto s = efa_sector();
    EXPECT_TRUE(ctgp_parse_boot_sector(s.data(), s.size(), 0, v, error));
    CtgpPartitionPlacement at;
    at.base = 0x81000000;
    at.disc_interface = 0x81000100;
    at.features = 1;  // read only medium
    std::vector<std::uint8_t> p;
    EXPECT_TRUE(ctgp_build_partition(v, at, p, error));
    EXPECT_EQ(static_cast<int>(p[ctgp_part::read_only]), 1);
}

static void test_volume_not_at_sector_zero() {
    // An SD card's FAT32 volume after a partition table: every sector number
    // the object holds counts from the device, not the volume.
    CtgpFatVolume v;
    std::string error;
    const auto s = efa_sector();
    EXPECT_TRUE(ctgp_parse_boot_sector(s.data(), s.size(), 2048, v, error));
    CtgpPartitionPlacement at;
    at.base = 0x816B5740;
    at.disc_interface = 0x816B12C0;
    at.features = 0x103;
    std::vector<std::uint8_t> p;
    EXPECT_TRUE(ctgp_build_partition(v, at, p, error));
    EXPECT_EQ(be32(p, ctgp_part::fat_start), 2048u + 8);
    EXPECT_EQ(be32(p, ctgp_part::data_start), 2048u + 8 + 4096);
    EXPECT_EQ(be32(p, ctgp_part::cache_object + 4), 2048u + 0x400000u);  // end of the partition
    EXPECT_EQ(be32(p, ctgp_part::number_of_sectors), 0x400000u);
    EXPECT_EQ(static_cast<int>(p[ctgp_part::read_only]), 0);  // 0x103 & 2: writable
}

static void test_fat16_and_active_fat() {
    std::vector<std::uint8_t> s(512, 0);
    s[0x0B] = 0; s[0x0C] = 2; s[0x0D] = 4; s[0x0E] = 1; s[0x10] = 2;
    s[0x11] = 0; s[0x12] = 2;            // 512 root entries
    s[0x13] = 0x00; s[0x14] = 0x80;      // 32768 sectors
    s[0x16] = 0x40; s[0x17] = 0;         // 64 sectors a FAT
    std::memcpy(s.data() + 0x36, "FAT16   ", 8);
    s[0x1FE] = 0x55; s[0x1FF] = 0xAA;
    CtgpFatVolume v;
    std::string error;
    EXPECT_TRUE(ctgp_parse_boot_sector(s.data(), s.size(), 0, v, error));
    CtgpPartitionPlacement at;
    at.base = 0x80000000;
    at.disc_interface = 0x80001000;
    at.features = 3;
    std::vector<std::uint8_t> p;
    EXPECT_TRUE(ctgp_build_partition(v, at, p, error));
    EXPECT_EQ(be32(p, ctgp_part::fs_type), 2u);
    EXPECT_EQ(be32(p, ctgp_part::root_dir_cluster), 0u);
    EXPECT_EQ(be32(p, ctgp_part::root_dir_start), 1u + 2 * 64);
    EXPECT_EQ(be32(p, ctgp_part::data_start), 1u + 2 * 64 + 32);  // 512 entries of 32 bytes
}

static void test_active_fat() {
    for (const unsigned flags : {0x81u, 0x00u, 0x01u}) {
        auto sector = efa_sector();
        sector[0x10] = 2;
        sector[0x28] = flags;
        CtgpFatVolume volume;
        CtgpPartitionPlacement placement;
        std::string error;
        std::vector<std::uint8_t> partition;
        EXPECT_TRUE(ctgp_parse_boot_sector(sector.data(), sector.size(), 2048, volume, error));
        EXPECT_TRUE(ctgp_build_partition(volume, placement, partition, error));
        const std::uint32_t active_offset = flags == 0x81 ? volume.sectors_per_fat : 0;
        EXPECT_EQ(be32(partition, ctgp_part::fat_start), 2048u + volume.reserved_sectors + active_offset);
    }
}

static void test_sda_partition() {
    // A FAT32 card whose volume starts at block 2048.
    std::vector<std::uint8_t> card(512 * 2049, 0);
    std::uint8_t* b = card.data() + 512 * 2048;
    b[0x0B] = 0x00; b[0x0C] = 0x02; b[0x0D] = 64;
    b[0x0E] = 32; b[0x0F] = 0; b[0x10] = 2;
    b[0x20] = 0x00; b[0x21] = 0x00; b[0x22] = 0x80; b[0x23] = 0x00;  // 8388608 sectors (over 65524 clusters: FAT32)
    b[0x24] = 0x00; b[0x25] = 0x20; b[0x26] = 0; b[0x27] = 0;        // 8192 sectors a FAT
    b[0x2C] = 2;
    std::memcpy(b + 0x47, "SDCARD     ", 11);
    std::memcpy(b + 0x52, "FAT32   ", 8);
    b[0x1FE] = 0x55; b[0x1FF] = 0xAA;
    BlockReader reader = [&](std::uint64_t lba, std::uint32_t n, std::uint8_t* out) {
        if ((lba + n) * 512 > card.size()) return false;
        std::memcpy(out, card.data() + lba * 512, n * 512u);
        return true;
    };
    CtgpPartitionPlacement at;
    at.base = 0x816b5740;
    at.disc_interface = 0x816b12c0;
    at.features = 0x103;
    std::vector<std::uint8_t> p;
    std::string error;
    EXPECT_TRUE(ctgp_build_sda_partition(reader, 2048, at, p, error));
    EXPECT_EQ(p.size(), (std::size_t)kCtgpPartitionSize);
    EXPECT_EQ(be32(p, ctgp_part::fs_type), 3u);
    EXPECT_EQ(be32(p, ctgp_part::fat_start), 2048u + 32);
    EXPECT_EQ(be32(p, ctgp_part::data_start), 2048u + 32 + 2 * 8192);
    EXPECT_EQ(be32(p, ctgp_part::sectors_per_cluster), 64u);
    EXPECT_EQ(be32(p, ctgp_part::root_dir_cluster), 2u);
    EXPECT_EQ((int)p[ctgp_part::read_only], 0);       // features 0x103: writable
    EXPECT_EQ(be32(p, ctgp_part::cache), 0x816b5740u + 0x6c);
    EXPECT_TRUE(std::memcmp(&p[ctgp_part::label], "SDCARD     ", 11) == 0);
    // a card that cannot be read, or is not FAT
    EXPECT_FALSE(ctgp_build_sda_partition(reader, 4096, at, p, error));
    EXPECT_FALSE(ctgp_build_sda_partition(reader, 0, at, p, error));
    EXPECT_FALSE(ctgp_build_sda_partition(BlockReader(), 2048, at, p, error));
}

int main() {
    test_parse();
    test_parse_refuses();
    test_efa_partition();
    test_read_only_follows_features();
    test_volume_not_at_sector_zero();
    test_fat16_and_active_fat();
    test_sda_partition();
    test_active_fat();
    if (g_failures == 0) std::cout << "ctgpfat tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
