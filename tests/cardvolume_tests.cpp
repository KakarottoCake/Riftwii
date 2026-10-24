// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// An SD card's partition and file system as boot.log describes them before
// d2x's FAT module mounts the card (riftwii/cardvolume.hpp, #128).
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/cardvolume.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

using Sector = std::vector<std::uint8_t>;

Sector blank() { return Sector(kCardSectorBytes, 0); }

void sign(Sector& s) {
    s[510] = 0x55;
    s[511] = 0xAA;
}

void put_partition(Sector& mbr, int entry, std::uint8_t type, std::uint32_t start) {
    std::uint8_t* row = mbr.data() + 446 + entry * 16;
    row[4] = type;
    row[8] = static_cast<std::uint8_t>(start);
    row[9] = static_cast<std::uint8_t>(start >> 8);
    row[10] = static_cast<std::uint8_t>(start >> 16);
    row[11] = static_cast<std::uint8_t>(start >> 24);
}

// A boot sector as a formatter writes it.
Sector boot_sector(const char* type_name, std::size_t type_offset, unsigned sectors_per_cluster) {
    Sector s = blank();
    s[0] = 0xEB;
    s[11] = 0x00;
    s[12] = 0x02;
    s[13] = static_cast<std::uint8_t>(sectors_per_cluster);
    std::memcpy(s.data() + type_offset, type_name, std::strlen(type_name));
    sign(s);
    return s;
}

void test_fat32_in_the_first_partition() {
    Sector mbr = blank();
    put_partition(mbr, 0, 0x0C, 8192);
    sign(mbr);
    const CardVolumeStart start = find_card_volume(mbr.data());
    EXPECT_TRUE(start.scheme == CardVolumeStart::Scheme::Mbr);
    EXPECT_EQ(start.partition, 1);
    EXPECT_EQ(start.first_sector, 8192u);
    const Sector boot = boot_sector("FAT32   ", 82, 64);
    EXPECT_EQ(describe_card_volume(start, boot.data()),
              std::string("FAT32, 512-byte sectors, 64 sectors per cluster, in MBR partition 1 (type 0x0C) from sector 8192"));
}

// The first used entry is the one described, wherever it is in the table.
void test_later_entry_and_fat16() {
    Sector mbr = blank();
    put_partition(mbr, 2, 0x06, 63);
    sign(mbr);
    const CardVolumeStart start = find_card_volume(mbr.data());
    EXPECT_EQ(start.partition, 3);
    const Sector boot = boot_sector("FAT16   ", 54, 32);
    EXPECT_EQ(describe_card_volume(start, boot.data()),
              std::string("FAT16, 512-byte sectors, 32 sectors per cluster, in MBR partition 3 (type 0x06) from sector 63"));
}

// Formatted without a partition table: sector 0 is the boot sector.
void test_no_partition_table() {
    const Sector boot = boot_sector("FAT32   ", 82, 8);
    const CardVolumeStart start = find_card_volume(boot.data());
    EXPECT_TRUE(start.scheme == CardVolumeStart::Scheme::NoTable);
    EXPECT_EQ(start.first_sector, 0u);
    EXPECT_EQ(describe_card_volume(start, boot.data()),
              std::string("FAT32, 512-byte sectors, 8 sectors per cluster, with no partition table (the volume starts at sector 0)"));
}

void test_gpt() {
    Sector mbr = blank();
    put_partition(mbr, 0, 0xEE, 1);
    sign(mbr);
    const CardVolumeStart start = find_card_volume(mbr.data());
    EXPECT_TRUE(start.scheme == CardVolumeStart::Scheme::Gpt);
    EXPECT_EQ(describe_card_volume(start, nullptr),
              std::string("a GPT partition table (protective MBR entry 1 from sector 1)"));
}

void test_exfat_and_ntfs_named_only() {
    Sector mbr = blank();
    put_partition(mbr, 0, 0x07, 32768);
    sign(mbr);
    const CardVolumeStart start = find_card_volume(mbr.data());
    EXPECT_EQ(describe_card_volume(start, boot_sector("EXFAT   ", 3, 0).data()),
              std::string("exFAT in MBR partition 1 (type 0x07) from sector 32768"));
    EXPECT_EQ(describe_card_volume(start, boot_sector("NTFS    ", 3, 8).data()),
              std::string("NTFS in MBR partition 1 (type 0x07) from sector 32768"));
}

void test_unreadable_cards() {
    const Sector empty = blank();
    CardVolumeStart start = find_card_volume(empty.data());
    EXPECT_TRUE(start.scheme == CardVolumeStart::Scheme::Unreadable);
    EXPECT_EQ(describe_card_volume(start, nullptr),
              std::string("no file system that RiftWii could find (sector 0 has no boot signature, 55 AA)"));

    Sector no_entries = blank();
    sign(no_entries);
    start = find_card_volume(no_entries.data());
    EXPECT_EQ(describe_card_volume(start, nullptr),
              std::string("no file system that RiftWii could find (the partition table in sector 0 has no used entry)"));

    Sector mbr = blank();
    put_partition(mbr, 0, 0x0B, 2048);
    sign(mbr);
    start = find_card_volume(mbr.data());
    EXPECT_EQ(describe_card_volume(start, nullptr),
              std::string("a volume in MBR partition 1 (type 0x0B) from sector 2048, whose boot sector could not be read"));
    EXPECT_EQ(describe_card_volume(start, empty.data()),
              std::string("no FAT, exFAT or NTFS boot sector and no boot signature in MBR partition 1 (type 0x0B) from sector 2048"));
}

}  // namespace

int main() {
    test_fat32_in_the_first_partition();
    test_later_entry_and_fat16();
    test_no_partition_table();
    test_gpt();
    test_exfat_and_ntfs_named_only();
    test_unreadable_cards();
    if (g_failures == 0) std::cout << "cardvolume: all tests passed\n";
    return g_failures == 0 ? 0 : 1;
}
