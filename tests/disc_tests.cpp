// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/disc.hpp"
#include "riftwii/source.hpp"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using Bytes = std::vector<std::uint8_t>;

static void Put32(Bytes& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 24);
    b[at + 1] = static_cast<std::uint8_t>(v >> 16);
    b[at + 2] = static_cast<std::uint8_t>(v >> 8);
    b[at + 3] = static_cast<std::uint8_t>(v);
}
static void Put16(Bytes& b, std::size_t at, std::uint16_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 8);
    b[at + 1] = static_cast<std::uint8_t>(v);
}
static void Put64(Bytes& b, std::size_t at, std::uint64_t v) {
    Put32(b, at, static_cast<std::uint32_t>(v >> 32));
    Put32(b, at + 4, static_cast<std::uint32_t>(v));
}
static void PutStr(Bytes& b, std::size_t at, const char* s) {
    std::memcpy(b.data() + at, s, std::strlen(s));
}

constexpr std::size_t kDiscSize = 0x60000;
constexpr std::size_t kUpdatePart = 0x50000;
constexpr std::size_t kGamePart = 0x52000;
constexpr std::size_t kChannelPart = 0x54000;
constexpr std::size_t kTmdRel = 0x2C0;
constexpr std::size_t kDataRel = 0x8000;
constexpr std::size_t kDataSize = 0x4000;

// A synthetic raw disc: header, partition table with three partitions in
// two groups, a game partition header and a TMD asking for IOS56.
static Bytes Disc() {
    Bytes d(kDiscSize, 0);
    PutStr(d, 0, "RFTE01");
    d[6] = 0;
    d[7] = 2;
    Put32(d, 0x18, riftwii::kWiiMagic);
    PutStr(d, 0x20, "Riftwii Test Disc   ");
    // Partition table: group 0 has two entries at 0x40020, group 3 one at 0x40040.
    Put32(d, 0x40000, 2);
    Put32(d, 0x40004, 0x40020 >> 2);
    Put32(d, 0x40018, 1);
    Put32(d, 0x4001C, 0x40040 >> 2);
    Put32(d, 0x40020, kUpdatePart >> 2);
    Put32(d, 0x40024, 1);
    Put32(d, 0x40028, kGamePart >> 2);
    Put32(d, 0x4002C, 0);
    Put32(d, 0x40040, kChannelPart >> 2);
    Put32(d, 0x40044, 2);
    // Game partition header.
    Put32(d, kGamePart + 0x2A4, 0x208);
    Put32(d, kGamePart + 0x2A8, kTmdRel >> 2);
    Put32(d, kGamePart + 0x2AC, 0xA00);
    Put32(d, kGamePart + 0x2B0, 0x4C8 >> 2);
    Put32(d, kGamePart + 0x2B4, 0x7000 >> 2);
    Put32(d, kGamePart + 0x2B8, kDataRel >> 2);
    Put32(d, kGamePart + 0x2BC, kDataSize >> 2);
    // TMD.
    const std::size_t tmd = kGamePart + kTmdRel;
    Put64(d, tmd + 0x184, 0x0000000100000038ull);
    Put64(d, tmd + 0x18C, 0x0001000052465445ull);
    Put16(d, tmd + 0x1DC, 0x0102);
    Put16(d, tmd + 0x1DE, 1);
    return d;
}

// The decrypted partition data view: header copy, DOL/FST pointers, apploader.
static Bytes Data() {
    Bytes d(kDataSize, 0);
    PutStr(d, 0, "RFTE01");
    d[7] = 2;
    Put32(d, 0x18, riftwii::kWiiMagic);
    PutStr(d, 0x20, "Riftwii Test Disc");
    Put32(d, 0x420, 0x3000 >> 2);
    Put32(d, 0x424, 0x3800 >> 2);
    Put32(d, 0x428, 0x100 >> 2);
    Put32(d, 0x42C, 0x200 >> 2);
    PutStr(d, 0x2440, "2008/03/17      ");
    Put32(d, 0x2450, 0x81200000u);
    Put32(d, 0x2454, 0x400);
    Put32(d, 0x2458, 0x40);
    return d;
}

static void test_disc_header() {
    riftwii::MemorySource disc(Disc());
    riftwii::DiscHeader h;
    std::string err;
    EXPECT_TRUE(riftwii::read_disc_header(disc, h, err));
    EXPECT_EQ(h.game_id, std::string("RFTE01"));
    EXPECT_EQ(int(h.disc_number), 0);
    EXPECT_EQ(int(h.version), 2);
    EXPECT_TRUE(h.wii_magic);
    EXPECT_FALSE(h.gamecube_magic);
    EXPECT_EQ(h.title, std::string("Riftwii Test Disc"));
    riftwii::DiscIdentity id = h.identity();
    EXPECT_EQ(id.id, std::string("RFTE01"));
    EXPECT_EQ(int(id.revision), 2);
    EXPECT_EQ(int(id.number), 0);

    Bytes bad = Disc();
    bad[3] = 'e';
    riftwii::DiscHeader out;
    EXPECT_FALSE(riftwii::parse_disc_header(bad.data(), bad.size(), out, err));
    bad = Disc();
    Put32(bad, 0x18, 0);
    EXPECT_FALSE(riftwii::parse_disc_header(bad.data(), bad.size(), out, err));
    Put32(bad, 0x1C, riftwii::kGameCubeMagic);
    EXPECT_TRUE(riftwii::parse_disc_header(bad.data(), bad.size(), out, err));
    EXPECT_TRUE(out.gamecube_magic);
    EXPECT_FALSE(riftwii::parse_disc_header(bad.data(), 0x40, out, err));
    riftwii::MemorySource tiny(Bytes(16, 0));
    EXPECT_FALSE(riftwii::read_disc_header(tiny, out, err));
}

static void test_partition_table() {
    riftwii::MemorySource disc(Disc());
    std::vector<riftwii::PartitionEntry> table;
    std::string err;
    EXPECT_TRUE(riftwii::read_partition_table(disc, table, err));
    EXPECT_EQ(table.size(), std::size_t(3));
    if (table.size() == 3) {
        EXPECT_EQ(table[0].offset, std::uint64_t(kUpdatePart));
        EXPECT_EQ(table[0].type, std::uint32_t(1));
        EXPECT_EQ(table[0].group, std::uint32_t(0));
        EXPECT_EQ(table[1].offset, std::uint64_t(kGamePart));
        EXPECT_EQ(table[1].type, std::uint32_t(0));
        EXPECT_EQ(table[2].offset, std::uint64_t(kChannelPart));
        EXPECT_EQ(table[2].type, std::uint32_t(2));
        EXPECT_EQ(table[2].group, std::uint32_t(3));
    }
    riftwii::PartitionEntry game;
    EXPECT_TRUE(riftwii::find_game_partition(table, game));
    EXPECT_EQ(game.offset, std::uint64_t(kGamePart));

    Bytes bad = Disc();
    Put32(bad, 0x40000, 33);
    EXPECT_FALSE(riftwii::read_partition_table(riftwii::MemorySource(bad), table, err));
    bad = Disc();
    Put32(bad, 0x40028, 0);
    EXPECT_FALSE(riftwii::read_partition_table(riftwii::MemorySource(bad), table, err));
    bad = Disc();
    Put32(bad, 0x40028, 0x40000000u);  // 4 GiB, beyond the image
    EXPECT_FALSE(riftwii::read_partition_table(riftwii::MemorySource(bad), table, err));
    bad = Disc();
    Put32(bad, 0x40004, 0x5FFF0 >> 2);  // info table runs off the end
    EXPECT_FALSE(riftwii::read_partition_table(riftwii::MemorySource(bad), table, err));
    bad = Disc();
    Put32(bad, 0x40000, 0);
    Put32(bad, 0x40018, 0);
    EXPECT_FALSE(riftwii::read_partition_table(riftwii::MemorySource(bad), table, err));
    std::vector<riftwii::PartitionEntry> only_update{table[0]};
    EXPECT_FALSE(riftwii::find_game_partition(only_update, game));
}

static void test_partition_header_and_tmd() {
    riftwii::MemorySource disc(Disc());
    riftwii::PartitionHeader p;
    std::string err;
    EXPECT_TRUE(riftwii::read_partition_header(disc, kGamePart, p, err));
    EXPECT_EQ(p.offset, std::uint64_t(kGamePart));
    EXPECT_EQ(p.tmd_size, std::uint32_t(0x208));
    EXPECT_EQ(p.tmd_offset, std::uint64_t(kGamePart + kTmdRel));
    EXPECT_EQ(p.cert_size, std::uint32_t(0xA00));
    EXPECT_EQ(p.cert_offset, std::uint64_t(kGamePart + 0x4C8));
    EXPECT_EQ(p.h3_offset, std::uint64_t(kGamePart + 0x7000));
    EXPECT_EQ(p.data_offset, std::uint64_t(kGamePart + kDataRel));
    EXPECT_EQ(p.data_size, std::uint64_t(kDataSize));

    riftwii::Tmd tmd;
    EXPECT_TRUE(riftwii::read_tmd(disc, p, tmd, err));
    EXPECT_EQ(tmd.sys_version, std::uint64_t(0x0000000100000038ull));
    EXPECT_EQ(tmd.required_ios(), std::uint32_t(56));
    EXPECT_EQ(tmd.title_id, std::uint64_t(0x0001000052465445ull));
    EXPECT_EQ(int(tmd.title_version), 0x102);
    EXPECT_EQ(int(tmd.content_count), 1);

    Bytes bad = Disc();
    Put32(bad, kGamePart + 0x2A4, 0x100);  // TMD smaller than its fixed header
    EXPECT_FALSE(riftwii::read_partition_header(riftwii::MemorySource(bad), kGamePart, p, err));
    bad = Disc();
    Put32(bad, kGamePart + 0x2B8, 0);
    EXPECT_FALSE(riftwii::read_partition_header(riftwii::MemorySource(bad), kGamePart, p, err));
    bad = Disc();
    Put32(bad, kGamePart + 0x2BC, 0x10000000u);
    EXPECT_FALSE(riftwii::read_partition_header(riftwii::MemorySource(bad), kGamePart, p, err));
    bad = Disc();
    Put32(bad, kGamePart + 0x2A8, 0x5FFF0 >> 2);
    EXPECT_FALSE(riftwii::read_partition_header(riftwii::MemorySource(bad), kGamePart, p, err));
    EXPECT_FALSE(riftwii::read_partition_header(disc, kDiscSize - 0x100, p, err));

    Bytes raw(0x208, 0);
    Put64(raw, 0x184, 0x0000000100000035ull);
    Put16(raw, 0x1DE, 100);
    EXPECT_FALSE(riftwii::parse_tmd(raw.data(), raw.size(), tmd, err));
    Put16(raw, 0x1DE, 1);
    EXPECT_TRUE(riftwii::parse_tmd(raw.data(), raw.size(), tmd, err));
    EXPECT_EQ(tmd.required_ios(), std::uint32_t(53));
    Put64(raw, 0x184, 0x0001000100000035ull);
    EXPECT_TRUE(riftwii::parse_tmd(raw.data(), raw.size(), tmd, err));
    EXPECT_EQ(tmd.required_ios(), std::uint32_t(0));
    EXPECT_FALSE(riftwii::parse_tmd(raw.data(), 0x100, tmd, err));
}

static void test_partition_data() {
    riftwii::MemorySource data(Data());
    riftwii::PartitionDataHeader d;
    std::string err;
    EXPECT_TRUE(riftwii::read_partition_data_header(data, d, err));
    EXPECT_EQ(d.dol_offset, std::uint64_t(0x3000));
    EXPECT_EQ(d.fst_offset, std::uint64_t(0x3800));
    EXPECT_EQ(d.fst_size, std::uint64_t(0x100));
    EXPECT_EQ(d.fst_max_size, std::uint64_t(0x200));

    riftwii::ApploaderHeader a;
    EXPECT_TRUE(riftwii::read_apploader_header(data, a, err));
    EXPECT_EQ(a.date, std::string("2008/03/17"));
    EXPECT_EQ(a.entry, std::uint32_t(0x81200000u));
    EXPECT_EQ(a.size, std::uint32_t(0x400));
    EXPECT_EQ(a.trailer_size, std::uint32_t(0x40));
    EXPECT_EQ(a.code_offset, std::uint64_t(0x2460));
    EXPECT_EQ(a.total_size(), std::uint64_t(0x440));

    Bytes bad = Data();
    Put32(bad, 0x428, 0x300 >> 2);  // FST larger than its maximum
    EXPECT_FALSE(riftwii::read_partition_data_header(riftwii::MemorySource(bad), d, err));
    bad = Data();
    Put32(bad, 0x424, 0x3F80 >> 2);  // FST runs off the end
    EXPECT_FALSE(riftwii::read_partition_data_header(riftwii::MemorySource(bad), d, err));
    bad = Data();
    Put32(bad, 0x420, 0);
    EXPECT_FALSE(riftwii::read_partition_data_header(riftwii::MemorySource(bad), d, err));
    bad = Data();
    Put32(bad, 0x18, 0);
    EXPECT_FALSE(riftwii::read_partition_data_header(riftwii::MemorySource(bad), d, err));

    bad = Data();
    Put32(bad, 0x2454, 0);
    EXPECT_FALSE(riftwii::read_apploader_header(riftwii::MemorySource(bad), a, err));
    bad = Data();
    Put32(bad, 0x2450, 0);
    EXPECT_FALSE(riftwii::read_apploader_header(riftwii::MemorySource(bad), a, err));
    bad = Data();
    Put32(bad, 0x2454, 0x300000);
    EXPECT_FALSE(riftwii::read_apploader_header(riftwii::MemorySource(bad), a, err));
    bad = Data();
    Put32(bad, 0x2454, 0x2000);  // extends past the view
    EXPECT_FALSE(riftwii::read_apploader_header(riftwii::MemorySource(bad), a, err));
}

int main() {
    test_disc_header();
    test_partition_table();
    test_partition_header_and_tmd();
    test_partition_data();
    if (g_failures == 0) {
        std::cout << "ALL DISC TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
