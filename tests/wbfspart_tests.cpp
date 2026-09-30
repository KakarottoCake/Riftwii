// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/disc.hpp"
#include "riftwii/imagevolume.hpp"
#include "riftwii/titles.hpp"
#include "riftwii/wbfspart.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

// 64-bit seek and tell: fseeko/ftello are not declared on the Windows host build.
namespace {
int seek64(std::FILE* f, std::uint64_t at) {
#if defined(_WIN32)
    return _fseeki64(f, static_cast<__int64>(at), SEEK_SET);
#else
    return fseeko(f, static_cast<off_t>(at), SEEK_SET);
#endif
}
std::uint64_t tell64(std::FILE* f) {
#if defined(_WIN32)
    return static_cast<std::uint64_t>(_ftelli64(f));
#else
    return static_cast<std::uint64_t>(ftello(f));
#endif
}
}  // namespace

using namespace riftwii;

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { const auto va_ = (a); const auto vb_ = (b); if (va_ != vb_) { std::cerr << "FAILED: " #a " == " #b " (" << va_ << " != " << vb_ << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

using Block = std::vector<std::uint8_t>;

void be32(std::uint8_t* p, std::uint32_t x) { p[0] = x >> 24; p[1] = x >> 16; p[2] = x >> 8; p[3] = x; }
void le32(std::uint8_t* p, std::uint32_t x) { p[0] = x; p[1] = x >> 8; p[2] = x >> 16; p[3] = x >> 24; }
void le64(std::uint8_t* p, std::uint64_t x) { le32(p, static_cast<std::uint32_t>(x)); le32(p + 4, static_cast<std::uint32_t>(x >> 32)); }

// A drive of 512-byte blocks: the ones set are held, the rest read as
// zeros, and any listed in `bad` fail.
struct Drive {
    std::map<std::uint64_t, Block> blocks;
    std::set<std::uint64_t> bad;
    Block& at(std::uint64_t lba) {
        Block& b = blocks[lba];
        b.resize(512);
        return b;
    }
    BlockReader reader() const {
        return [this](std::uint64_t lba, std::uint32_t count, std::uint8_t* out) {
            for (std::uint32_t i = 0; i < count; ++i) {
                if (bad.count(lba + i)) return false;
                const auto it = blocks.find(lba + i);
                if (it == blocks.end()) std::memset(out + i * 512, 0, 512);
                else std::memcpy(out + i * 512, it->second.data(), 512);
            }
            return true;
        };
    }
};

void mbr_entry(Drive& d, int index, std::uint8_t type, std::uint32_t lba, std::uint32_t count) {
    Block& mbr = d.at(0);
    std::uint8_t* e = mbr.data() + 0x1BE + index * 16;
    e[4] = type;
    le32(e + 8, lba);
    le32(e + 12, count);
    mbr[510] = 0x55;
    mbr[511] = 0xAA;
}

// A WBFS header like the user's drive: 512-byte sectors, 16 MiB blocks.
void wbfs_header(Drive& d, std::uint64_t lba, std::uint32_t hd_sectors, std::uint8_t wbfs_shift = 24,
                 std::uint8_t hd_shift = 9) {
    Block& h = d.at(lba);
    std::memcpy(h.data(), "WBFS", 4);
    be32(h.data() + 4, hd_sectors);
    h[8] = hd_shift;
    h[9] = wbfs_shift;
    h[10] = 1;
}

// Marks `slot` used and gives it a disc header copy.
void wbfs_disc(Drive& d, std::uint64_t lba, std::uint32_t info_sectors, std::uint32_t slot, const char* id,
               const char* title, bool wii = true) {
    d.at(lba)[12 + slot] = 1;
    Block& info = d.at(lba + 1 + std::uint64_t(slot) * info_sectors);
    std::memcpy(info.data(), id, 6);
    if (wii) be32(info.data() + 0x18, 0x5D1C9EA3);
    std::strncpy(reinterpret_cast<char*>(info.data() + 0x20), title, 63);
}

constexpr std::uint32_t kSingleLayerSectors = static_cast<std::uint32_t>(143432ull * 0x8000 / 512);
constexpr std::uint32_t kUserSectors = 1945600000;  // the user's drive, from its header
constexpr std::uint32_t kUserInfo = 3;              // 1536-byte disc infos

void test_layout_matches_libwbfs() {
    // 16 MiB blocks, as on the user's drive: 560 blocks per disc, 500 slots.
    Drive d;
    wbfs_header(d, 0, kUserSectors);
    WbfsLayout l;
    std::string error;
    EXPECT_TRUE(parse_wbfs_header(d.at(0).data(), l, error));
    EXPECT_EQ(l.hd_sectors, kUserSectors);
    EXPECT_EQ(l.block_sectors, 32768u);
    EXPECT_EQ(l.blocks, 59375u);
    EXPECT_EQ(l.disc_blocks, 560u);
    EXPECT_EQ(l.disc_info_sectors, kUserInfo);
    EXPECT_EQ(l.slots, 500u);
    EXPECT_TRUE(l.used.empty());

    // 2 MiB blocks on 20 GiB, as wwt reports it: 4482 blocks per disc,
    // 9728-byte disc infos and 215 slots before the free-block table.
    Drive s;
    wbfs_header(s, 0, 41943040, 21);
    EXPECT_TRUE(parse_wbfs_header(s.at(0).data(), l, error));
    EXPECT_EQ(l.blocks, 10240u);
    EXPECT_EQ(l.disc_blocks, 4482u);
    EXPECT_EQ(l.disc_info_sectors, 19u);
    EXPECT_EQ(l.slots, 215u);
}

void test_mbr_partition_with_fat16_type() {
    // WBFS tools leave a FAT16 type byte; the partition is found by content.
    for (std::uint8_t type : {std::uint8_t(0x04), std::uint8_t(0x06), std::uint8_t(0x0E), std::uint8_t(0x07)}) {
        Drive d;
        constexpr std::uint32_t kStart = 2048;
        mbr_entry(d, 0, type, kStart, kUserSectors);
        wbfs_header(d, kStart, kUserSectors);
        wbfs_disc(d, kStart, kUserInfo, 0, "RMAP01", "Mario Tennis");
        wbfs_disc(d, kStart, kUserInfo, 2, "RSPP01", "Wii Sports");
        wbfs_disc(d, kStart, kUserInfo, 7, "RYQP69", "Quiz");
        WbfsPartition p;
        std::string error;
        EXPECT_TRUE(find_wbfs_partition(d.reader(), p, error));
        EXPECT_EQ(p.lba, std::uint64_t(kStart));
        EXPECT_EQ(p.layout.used.size(), std::size_t(3));
        std::vector<WbfsDisc> discs;
        std::vector<std::string> skipped;
        EXPECT_TRUE(list_wbfs_discs(d.reader(), p, discs, skipped, error));
        EXPECT_EQ(discs.size(), std::size_t(3));
        EXPECT_TRUE(skipped.empty());
        if (discs.size() == 3) {
            EXPECT_EQ(discs[0].slot, 0u);
            EXPECT_EQ(discs[0].id, std::string("RMAP01"));
            EXPECT_EQ(discs[0].title, std::string("Mario Tennis"));
            EXPECT_EQ(discs[1].slot, 2u);
            EXPECT_EQ(discs[1].id, std::string("RSPP01"));
            EXPECT_EQ(discs[2].slot, 7u);
            EXPECT_EQ(discs[2].id, std::string("RYQP69"));
        }
        // The same drive is no FAT32 or NTFS volume.
        std::unique_ptr<ImageVolume> volume;
        EXPECT_FALSE(mount_image_volume(d.reader(), volume, error));
    }
}

void test_second_partition_and_gpt() {
    // Behind another partition in the MBR.
    Drive d;
    mbr_entry(d, 0, 0x83, 63, 1000);
    mbr_entry(d, 1, 0x06, 4096, 100000000);
    wbfs_header(d, 4096, 100000000);
    WbfsPartition p;
    std::string error;
    EXPECT_TRUE(find_wbfs_partition(d.reader(), p, error));
    EXPECT_EQ(p.lba, std::uint64_t(4096));

    // In a GPT, with the size from the entry's last LBA.
    Drive g;
    mbr_entry(g, 0, 0xEE, 1, 0xFFFFFFFF);
    Block& header = g.at(1);
    std::memcpy(header.data(), "EFI PART", 8);
    le64(header.data() + 0x48, 2);
    le32(header.data() + 0x50, 4);
    le32(header.data() + 0x54, 128);
    Block& entries = g.at(2);
    entries[0] = 0xA2;  // any type GUID
    le64(entries.data() + 0x20, 8192);
    le64(entries.data() + 0x28, 8192 + 100000000 - 1);
    wbfs_header(g, 8192, 100000000);
    EXPECT_TRUE(find_wbfs_partition(g.reader(), p, error));
    EXPECT_EQ(p.lba, std::uint64_t(8192));
    const std::vector<DrivePartition> parts = drive_partitions(g.reader(), g.at(0).data());
    EXPECT_EQ(parts.size(), std::size_t(1));
    if (!parts.empty()) EXPECT_EQ(parts[0].blocks, std::uint64_t(100000000));
}

void test_block_zero_without_mbr() {
    Drive d;
    wbfs_header(d, 0, kUserSectors);
    wbfs_disc(d, 0, kUserInfo, 499, "RMCP01", "Mario Kart Wii");  // the last slot
    WbfsPartition p;
    std::string error;
    EXPECT_TRUE(find_wbfs_partition(d.reader(), p, error));
    EXPECT_EQ(p.lba, std::uint64_t(0));
    std::vector<WbfsDisc> discs;
    std::vector<std::string> skipped;
    EXPECT_TRUE(list_wbfs_discs(d.reader(), p, discs, skipped, error));
    EXPECT_EQ(discs.size(), std::size_t(1));
    if (!discs.empty()) EXPECT_EQ(discs[0].slot, 499u);
}

bool refuses(Drive& d, const std::string& want) {
    WbfsPartition p;
    std::string error;
    if (find_wbfs_partition(d.reader(), p, error)) return false;
    if (error.find(want) == std::string::npos) {
        std::cerr << "  error was: " << error << std::endl;
        return false;
    }
    return true;
}

void test_bad_headers() {
    {
        Drive d;  // 4096-byte sectors: d2x cannot use it
        wbfs_header(d, 0, 1000000, 24, 12);
        EXPECT_TRUE(refuses(d, "512-byte"));
    }
    {
        Drive d;  // blocks smaller than a Wii sector
        wbfs_header(d, 0, 1000000, 14);
        EXPECT_TRUE(refuses(d, "block-size shift 14"));
    }
    {
        Drive d;  // too small to hold one block past the tables
        wbfs_header(d, 0, 32768);
        EXPECT_TRUE(refuses(d, "1 blocks"));
    }
    {
        Drive d;  // more blocks than a u16 can name
        wbfs_header(d, 0, 0xFFFFFFFF, 15);
        EXPECT_TRUE(refuses(d, "do not make a valid WBFS"));
    }
    {
        Drive d;  // larger than the partition holding it
        mbr_entry(d, 0, 0x06, 2048, 1000000);
        wbfs_header(d, 2048, 2000000);
        EXPECT_TRUE(refuses(d, "its partition holds 1000000"));
    }
    {
        Drive d;  // neither at block 0 nor in a partition
        mbr_entry(d, 0, 0x06, 2048, 1000000);
        EXPECT_TRUE(refuses(d, "no WBFS partition found"));
    }
    {
        Drive d;  // no MBR and no WBFS
        EXPECT_TRUE(refuses(d, "no WBFS partition found"));
    }
    {
        Drive d;  // block 0 unreadable
        d.bad.insert(0);
        EXPECT_TRUE(refuses(d, "cannot read block 0"));
    }
}

void test_damaged_slots_are_skipped() {
    Drive d;
    wbfs_header(d, 0, kUserSectors);
    wbfs_disc(d, 0, kUserInfo, 0, "RMAP01", "Good");
    wbfs_disc(d, 0, kUserInfo, 1, "RMAP01", "No magic", false);
    wbfs_disc(d, 0, kUserInfo, 2, "RM\x01P01", "Bad id");
    wbfs_disc(d, 0, kUserInfo, 3, "RSPP01", "Unreadable");
    wbfs_disc(d, 0, kUserInfo, 4, "RYQP69", "Good too");
    d.bad.insert(1 + 3 * kUserInfo);
    WbfsPartition p;
    std::string error;
    EXPECT_TRUE(find_wbfs_partition(d.reader(), p, error));
    std::vector<WbfsDisc> discs;
    std::vector<std::string> skipped;
    EXPECT_TRUE(list_wbfs_discs(d.reader(), p, discs, skipped, error));
    EXPECT_EQ(discs.size(), std::size_t(2));
    EXPECT_EQ(skipped.size(), std::size_t(3));
    if (discs.size() == 2) {
        EXPECT_EQ(discs[0].slot, 0u);
        EXPECT_EQ(discs[1].slot, 4u);
    }
    if (skipped.size() == 3) {
        EXPECT_TRUE(skipped[0].find("slot 1 (\"RMAP01\"): ") == 0);
        EXPECT_TRUE(skipped[1].find("slot 2 (\"RM?P01\"): ") == 0);
        EXPECT_TRUE(skipped[2].find("slot 3: cannot read") == 0);
    }

    // Nothing readable at all is a failure, not an empty list.
    Drive all_bad;
    wbfs_header(all_bad, 0, kUserSectors);
    wbfs_disc(all_bad, 0, kUserInfo, 0, "RMAP01", "Gone");
    all_bad.bad.insert(1);
    EXPECT_TRUE(find_wbfs_partition(all_bad.reader(), p, error));
    EXPECT_FALSE(list_wbfs_discs(all_bad.reader(), p, discs, skipped, error));
}

void test_slot_fragments() {
    // 2 MiB blocks (4096 sectors), the partition at block 2048: slot 3's
    // disc blocks 0 and 1 are WBFS blocks 7 and 9, block 2 is not stored.
    Drive d;
    constexpr std::uint32_t kStart = 2048, kSectors = 41943040, kInfo = 19;
    mbr_entry(d, 0, 0x06, kStart, kSectors);
    wbfs_header(d, kStart, kSectors, 21);
    wbfs_disc(d, kStart, kInfo, 0, "RMAP01", "Other");
    wbfs_disc(d, kStart, kInfo, 3, "RSPP01", "Wii Sports");
    const std::uint64_t info = kStart + 1 + 3 * kInfo;
    d.at(info)[0x100] = 0; d.at(info)[0x101] = 7;
    d.at(info)[0x102] = 0; d.at(info)[0x103] = 9;
    d.at(kStart + 7 * 4096)[0] = 0xA7;
    d.at(kStart + 9 * 4096 + 4095)[511] = 0xB9;
    WbfsPartition p;
    std::string error;
    EXPECT_TRUE(find_wbfs_partition(d.reader(), p, error));
    const UsbImage image = wbfs_slot_image(d.reader(), p, 3);
    D2xFragmentList list;
    EXPECT_TRUE(build_usb_fragments(image, list, error));
    EXPECT_EQ(list.size, kSingleLayerSectors);  // single layer: nothing stored past it
    EXPECT_EQ(list.entries.size(), std::size_t(2));
    if (list.entries.size() == 2) {
        EXPECT_EQ(list.entries[0].offset, 0u);
        EXPECT_EQ(list.entries[0].sector, kStart + 7u * 4096);
        EXPECT_EQ(list.entries[0].count, 4096u);
        EXPECT_EQ(list.entries[1].offset, 4096u);
        EXPECT_EQ(list.entries[1].sector, kStart + 9u * 4096);
    }
    std::unique_ptr<UsbDiscSource> disc;
    EXPECT_TRUE(UsbDiscSource::open(image, disc, error));
    if (disc) {
        std::uint8_t b[2] = {};
        EXPECT_TRUE(disc->read(0, b, 1));
        EXPECT_EQ(int(b[0]), 0xA7);
        EXPECT_TRUE(disc->read(2 * 0x200000 - 1, b, 1));
        EXPECT_EQ(int(b[0]), 0xB9);
        EXPECT_FALSE(disc->read(2 * 0x200000, b, 1));  // block 2 is absent, never zeros
    }
    // Unused slots and slots past the table are refused.
    D2xFragmentList none;
    EXPECT_FALSE(build_usb_fragments(wbfs_slot_image(d.reader(), p, 1), none, error));
    EXPECT_TRUE(error.find("slot 1 holds no disc") != std::string::npos);
    EXPECT_FALSE(build_usb_fragments(wbfs_slot_image(d.reader(), p, 600), none, error));
    // A block number past the partition is refused, not read.
    d.at(info)[0x103] = 0;
    d.at(info)[0x102] = 0x28;  // WBFS block 10240 of 10240
    EXPECT_FALSE(build_usb_fragments(wbfs_slot_image(d.reader(), p, 3), none, error));
    EXPECT_TRUE(error.find("exceeds") != std::string::npos);
}

void test_catalog_names() {
    // The menu lists a WBFS drive's game as "usb:/wbfs slot N": its name
    // comes from the title list, else the disc header, never the path.
    const std::string path = "usb:/wbfs slot 242";
    EXPECT_EQ(id_from_image_path(path), std::string());
    EXPECT_EQ(folder_title(path, "RMCP01"), std::string());
    EXPECT_EQ(display_title(nullptr, "RMCP01", path, "MARIO KART WII"), std::string("MARIO KART WII"));
    TitleTable table;
    table.add_text("RMCP01 = Mario Kart Wii\n");
    EXPECT_EQ(display_title(&table, "RMCP01", path, "MARIO KART WII"), std::string("Mario Kart Wii"));
}

// The first 64 MiB of the user's WBFS partition (header, disc table and
// every disc info), kept out of git in tests/local. Skipped when absent.
void test_real_drive_backup(const std::string& dir) {
    std::ifstream file(dir + "/wbfs-drive-backup.bin", std::ios::binary);
    if (!file) {
        std::cout << "wbfspart: no drive backup in " << dir << ", skipping that test\n";
        return;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    // At block 0 of a drive with no MBR, then behind an MBR at a nonzero
    // block with a FAT16 type byte, as on the drive itself.
    for (const std::uint64_t start : {std::uint64_t(0), std::uint64_t(2048)}) {
        Drive d;
        if (start != 0) mbr_entry(d, 0, 0x06, static_cast<std::uint32_t>(start), kUserSectors);
        BlockReader base = d.reader();
        BlockReader reader = [&](std::uint64_t lba, std::uint32_t count, std::uint8_t* out) {
            if (lba < start) return base(lba, count, out);
            const std::uint64_t at = (lba - start) * 512;
            if (at > bytes.size() || std::uint64_t(count) * 512 > bytes.size() - at) return false;
            std::memcpy(out, bytes.data() + at, std::size_t(count) * 512);
            return true;
        };
        WbfsPartition p;
        std::string error;
        EXPECT_TRUE(find_wbfs_partition(reader, p, error));
        EXPECT_EQ(p.lba, start);
        EXPECT_EQ(p.layout.hd_sectors, kUserSectors);
        EXPECT_EQ(p.layout.disc_blocks, 560u);
        EXPECT_EQ(p.layout.slots, 500u);
        std::vector<WbfsDisc> discs;
        std::vector<std::string> skipped;
        EXPECT_TRUE(list_wbfs_discs(reader, p, discs, skipped, error));
        // 327 slots in use: 326 games and Configurable USB Loader's
        // settings entry, which is not a game.
        EXPECT_EQ(p.layout.used.size(), std::size_t(327));
        EXPECT_EQ(discs.size(), std::size_t(326));
        EXPECT_EQ(skipped.size(), std::size_t(1));
        if (!skipped.empty()) EXPECT_TRUE(skipped[0].find("slot 242 (\"__CFG_\")") == 0);
        if (discs.size() >= 3) {
            EXPECT_EQ(discs[0].id, std::string("RMAP01"));
            EXPECT_EQ(discs[1].id, std::string("RYQP69"));
            EXPECT_EQ(discs[2].id, std::string("RSPP01"));
        }
        // Every game maps inside the partition, past its first block (the
        // tables), and no two games share a sector.
        const std::uint64_t first = start + p.layout.block_sectors;
        const std::uint64_t end = start + std::uint64_t(p.layout.blocks) * p.layout.block_sectors;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> used;
        std::size_t mapped = 0, dual_layer = 0;
        for (const WbfsDisc& disc : discs) {
            D2xFragmentList list;
            if (!build_usb_fragments(wbfs_slot_image(reader, p, disc.slot), list, error)) {
                std::cerr << "  slot " << disc.slot << " " << disc.id << ": " << error << std::endl;
                continue;
            }
            ++mapped;
            if (list.size > kSingleLayerSectors) ++dual_layer;
            for (const D2xFragment& f : list.entries) {
                EXPECT_TRUE(f.sector >= first && std::uint64_t(f.sector) + f.count <= end);
                used.push_back({f.sector, std::uint64_t(f.sector) + f.count});
            }
        }
        EXPECT_EQ(mapped, discs.size());
        std::sort(used.begin(), used.end());
        std::size_t overlaps = 0;
        for (std::size_t i = 1; i < used.size(); ++i) overlaps += used[i].first < used[i - 1].second ? 1 : 0;
        EXPECT_EQ(overlaps, std::size_t(0));
        if (start == 0) {
            std::cout << "wbfspart: drive backup: " << mapped << " games mapped (" << dual_layer
                      << " dual-layer), " << used.size() << " fragments, no shared sectors\n";
        }
    }
}

// A WBFS partition image holding real games, behind a fake MBR at block
// 2048, read as d2x will read it: each fragment's drive sectors must match
// the disc's ISO, and what no fragment covers must be zeros there. Made
// (git-ignored) with Wiimm's tools: `wwt ADD --part wbfs-partition.img`
// on a copy of an empty WBFS file, and each disc's `<ID6>.iso` from
// `wwt EXTRACT` or `wit COPY`. Skipped when absent.
void test_real_game_image(const std::string& dir) {
    std::FILE* img = std::fopen((dir + "/wbfs-partition.img").c_str(), "rb");
    if (!img) {
        std::cout << "wbfspart: no game image in " << dir << ", skipping that test\n";
        return;
    }
    std::uint8_t head[512];
    EXPECT_EQ(std::fread(head, 1, 512, img), std::size_t(512));
    constexpr std::uint64_t kStart = 2048;
    Drive mbr;
    mbr_entry(mbr, 0, 0x06, kStart, (head[4] << 24) | (head[5] << 16) | (head[6] << 8) | head[7]);
    BlockReader base = mbr.reader();
    BlockReader reader = [&](std::uint64_t lba, std::uint32_t count, std::uint8_t* out) {
        if (lba < kStart) return base(lba, count, out);
        if (seek64(img, (lba - kStart) * 512) != 0) return false;
        return std::fread(out, 512, count, img) == count;
    };

    WbfsPartition p;
    std::string error;
    EXPECT_TRUE(find_wbfs_partition(reader, p, error));
    EXPECT_EQ(p.lba, kStart);
    std::vector<WbfsDisc> discs;
    std::vector<std::string> skipped;
    EXPECT_TRUE(list_wbfs_discs(reader, p, discs, skipped, error));
    EXPECT_TRUE(discs.size() >= 2);
    std::vector<std::uint8_t> a(1 << 20), b(1 << 20);
    for (const WbfsDisc& disc : discs) {
        const UsbImage image = wbfs_slot_image(reader, p, disc.slot);
        // The catalog's view: the disc's own header, through UsbDiscSource.
        std::unique_ptr<UsbDiscSource> source;
        DiscHeader header;
        EXPECT_TRUE(UsbDiscSource::open(image, source, error));
        if (!source || !read_disc_header(*source, header, error)) {
            std::cerr << "FAILED: slot " << disc.slot << ": " << error << std::endl;
            ++g_failures;
            continue;
        }
        std::FILE* iso = std::fopen((dir + "/" + header.game_id + ".iso").c_str(), "rb");
        if (!iso) {
            std::cerr << "FAILED: slot " << disc.slot << ": no " << header.game_id << ".iso in " << dir << std::endl;
            ++g_failures;
            continue;
        }
        std::fseek(iso, 0, SEEK_END);
        const std::uint64_t iso_bytes = tell64(iso);
        auto iso_read = [&](std::uint64_t at, std::uint8_t* out, std::size_t n) {
            std::memset(out, 0, n);  // wwt ends an ISO after its last data
            if (at >= iso_bytes) return;
            const std::size_t have = static_cast<std::size_t>(std::min<std::uint64_t>(n, iso_bytes - at));
            seek64(iso, at);
            if (std::fread(out, 1, have, iso) != have) std::memset(out, 0xEE, n);
        };
        iso_read(0x50000, b.data(), 0x8000);
        EXPECT_TRUE(source->read(0x50000, a.data(), 0x8000));
        EXPECT_TRUE(std::memcmp(a.data(), b.data(), 0x8000) == 0);

        // The fragments d2x is given, read from the drive's own sectors.
        D2xFragmentList list;
        EXPECT_TRUE(build_usb_fragments(image, list, error));
        std::vector<std::uint8_t> encoded;
        EXPECT_TRUE(list.encode(encoded, error));
        std::uint64_t covered_to = 0, mismatched = 0, compared = 0, gaps_nonzero = 0;
        auto zeros_to = [&](std::uint64_t end) {  // not stored: zeros in the ISO
            for (std::uint64_t at = covered_to * 512; at < end; at += a.size()) {
                const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(a.size(), end - at));
                iso_read(at, a.data(), n);
                gaps_nonzero += std::any_of(a.begin(), a.begin() + n, [](std::uint8_t x) { return x != 0; });
            }
        };
        for (const D2xFragment& f : list.entries) {
            zeros_to(std::uint64_t(f.offset) * 512);
            for (std::uint64_t done = 0; done < f.count; done += 2048) {
                const std::uint32_t n = static_cast<std::uint32_t>(std::min<std::uint64_t>(2048, f.count - done));
                if (!reader(f.sector + done, n, a.data())) { ++mismatched; continue; }
                iso_read((f.offset + done) * 512, b.data(), std::size_t(n) * 512);
                mismatched += std::memcmp(a.data(), b.data(), std::size_t(n) * 512) != 0;
                compared += n;
            }
            covered_to = std::uint64_t(f.offset) + f.count;
        }
        zeros_to(iso_bytes);
        EXPECT_EQ(mismatched, std::uint64_t(0));
        EXPECT_EQ(gaps_nonzero, std::uint64_t(0));
        EXPECT_TRUE(compared > 0);
        std::cout << "wbfspart: slot " << disc.slot << " (listed as " << disc.id << ", disc " << header.game_id
                  << "): " << list.entries.size() << " fragments, " << compared * 512 / (1 << 20)
                  << " MiB match the ISO\n";
        std::fclose(iso);
    }
    std::fclose(img);
}

}  // namespace

int main(int argc, char** argv) {
    test_layout_matches_libwbfs();
    test_mbr_partition_with_fat16_type();
    test_second_partition_and_gpt();
    test_block_zero_without_mbr();
    test_bad_headers();
    test_damaged_slots_are_skipped();
    test_slot_fragments();
    test_catalog_names();
    test_real_drive_backup(argc > 1 ? argv[1] : "tests/local");
    test_real_game_image(argc > 1 ? argv[1] : "tests/local");
    if (g_failures == 0) std::cout << "wbfspart tests passed\n";
    else std::cerr << g_failures << " TEST CHECKS FAILED\n";
    return g_failures == 0 ? 0 : 1;
}
