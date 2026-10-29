// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/discdump.hpp"
#include "riftwii/disc.hpp"
#include "riftwii/source.hpp"
#include "riftwii/usbgame.hpp"
#include "riftwii/wiicrypt.hpp"

#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using Bytes = std::vector<std::uint8_t>;
using riftwii::kClusterBytes;
using riftwii::kClusterDataBytes;
using riftwii::kWiiSectorBytes;

namespace {

void put32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

// A single-layer disc that keeps only the sectors written to it; the rest
// read as zeros, and nothing past the single-layer end reads at all, as on
// a real drive.
class SparseDisc final : public riftwii::ByteSource {
public:
    std::uint64_t size() const override { return std::uint64_t(riftwii::kWiiSingleLayerSectors) * kWiiSectorBytes; }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        if (offset > size() || length > size() - offset) return false;
        while (length > 0) {
            const std::uint64_t sector = offset / kWiiSectorBytes;
            const std::size_t skip = static_cast<std::size_t>(offset % kWiiSectorBytes);
            const std::size_t take = std::min<std::size_t>(length, kWiiSectorBytes - skip);
            const auto found = sectors_.find(sector);
            if (found == sectors_.end()) {
                std::memset(destination, 0, take);
            } else {
                std::memcpy(destination, found->second.data() + skip, take);
            }
            offset += take;
            destination += take;
            length -= take;
        }
        return true;
    }
    void write(std::uint64_t offset, const std::uint8_t* data, std::size_t length) {
        while (length > 0) {
            const std::uint64_t sector = offset / kWiiSectorBytes;
            const std::size_t skip = static_cast<std::size_t>(offset % kWiiSectorBytes);
            const std::size_t take = std::min<std::size_t>(length, kWiiSectorBytes - skip);
            Bytes& bytes = sectors_[sector];
            if (bytes.empty()) bytes.assign(kWiiSectorBytes, 0);
            std::memcpy(bytes.data() + skip, data, take);
            offset += take;
            data += take;
            length -= take;
        }
    }
    void write32(std::uint64_t offset, std::uint32_t v) {
        std::uint8_t b[4];
        put32(b, v);
        write(offset, b, 4);
    }
    bool has(std::uint64_t sector) const { return sectors_.count(sector) != 0; }

private:
    std::map<std::uint64_t, Bytes> sectors_;
};

const std::uint8_t kCommonKey[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

// One partition being built: its decrypted data, cluster by cluster, until
// seal() encrypts it onto the disc.
struct TestPartition {
    std::uint64_t offset = 0;
    std::uint64_t data_size = 0;  // raw bytes of the data area
    std::uint8_t title_key[16] = {};
    std::uint8_t key_index = 0;   // 1: the Korean common key
    std::map<std::uint64_t, Bytes> clusters;

    static constexpr std::uint64_t kDataRel = 0x20000;
    std::uint64_t data_offset() const { return offset + kDataRel; }

    void put(std::uint64_t at, const std::uint8_t* data, std::size_t length) {
        while (length > 0) {
            const std::uint64_t cluster = at / kClusterDataBytes;
            const std::size_t skip = static_cast<std::size_t>(at % kClusterDataBytes);
            const std::size_t take = std::min<std::size_t>(length, kClusterDataBytes - skip);
            Bytes& bytes = clusters[cluster];
            if (bytes.empty()) bytes.assign(kClusterDataBytes, 0);
            std::memcpy(bytes.data() + skip, data, take);
            at += take;
            data += take;
            length -= take;
        }
    }
    void put32(std::uint64_t at, std::uint32_t v) {
        std::uint8_t b[4];
        ::put32(b, v);
        put(at, b, 4);
    }
    void fill(std::uint64_t at, std::size_t length, std::uint8_t value) {
        const Bytes bytes(length, value);
        put(at, bytes.data(), bytes.size());
    }

    void seal(SparseDisc& disc, std::uint32_t type_marker) const {
        // Ticket: the title key, encrypted with the common key and the
        // title ID as its IV.
        Bytes header(0x2C0, 0);
        const std::uint8_t title_id[8] = {0, 1, 0, 0, 'R', 'D', 'C', static_cast<std::uint8_t>(type_marker)};
        std::memcpy(header.data() + 0x1DC, title_id, 8);
        std::uint8_t iv[16] = {};
        std::memcpy(iv, title_id, 8);
        riftwii::Aes128(kCommonKey).cbc_encrypt(iv, title_key, header.data() + 0x1BF, 16);
        header[0x1F1] = key_index;
        ::put32(header.data() + 0x2A4, 0x208);              // TMD size
        ::put32(header.data() + 0x2A8, 0x2C0 >> 2);         // TMD
        ::put32(header.data() + 0x2AC, 0xA00);              // certificates
        ::put32(header.data() + 0x2B0, 0x4C8 >> 2);
        ::put32(header.data() + 0x2B4, 0x8000 >> 2);        // H3
        ::put32(header.data() + 0x2B8, kDataRel >> 2);
        ::put32(header.data() + 0x2BC, static_cast<std::uint32_t>(data_size >> 2));
        disc.write(offset, header.data(), header.size());
        Bytes marker(0x18000, static_cast<std::uint8_t>(0x30 + type_marker));
        disc.write(offset + 0x8000, marker.data(), marker.size());
        const riftwii::Aes128 aes(title_key);
        for (const auto& [cluster, plain] : clusters) {
            Bytes raw(kClusterBytes, 0);
            for (int i = 0; i < 16; ++i) raw[0x3D0 + i] = static_cast<std::uint8_t>(cluster * 7 + i);
            aes.cbc_encrypt(raw.data() + 0x3D0, plain.data(), raw.data() + 0x400, kClusterDataBytes);
            disc.write(data_offset() + cluster * kClusterBytes, raw.data(), raw.size());
        }
    }
};

// A partition's data: boot header, apploader, a DOL and a file table naming
// the files in `files` (offset, size), each filled with `fill`.
void lay_out(TestPartition& p, const std::vector<std::pair<std::uint64_t, std::uint32_t>>& files) {
    const char id[] = "RDCE01";
    p.put(0, reinterpret_cast<const std::uint8_t*>(id), 6);
    p.put32(0x18, riftwii::kWiiMagic);
    constexpr std::uint64_t kDol = 0x20000, kFst = 0x40000;
    p.put32(0x420, kDol >> 2);
    p.put32(0x424, kFst >> 2);
    // Apploader: 0x9000 bytes of code, 0x100 of trailer.
    p.put32(0x2440 + 0x14, 0x9000);
    p.put32(0x2440 + 0x18, 0x100);
    p.fill(0x2460, 0x9100, 0xA5);
    // DOL: one text section of 0x10000 bytes after its header.
    p.put32(kDol + 0x00, 0x100);
    p.put32(kDol + 0x48, 0x80004000);
    p.put32(kDol + 0x90, 0x10000);
    p.put32(kDol + 0xE0, 0x80004000);
    p.fill(kDol + 0x100, 0x10000, 0xD0);
    // FST: root, a directory, then the files, and the names.
    const std::uint32_t entries = static_cast<std::uint32_t>(2 + files.size());
    Bytes fst(entries * 12, 0);
    std::string names("dir");
    names += '\0';
    ::put32(fst.data() + 0, 0x01000000);
    ::put32(fst.data() + 8, entries);
    ::put32(fst.data() + 12, 0x01000000);  // "dir", parent 0, next = end
    ::put32(fst.data() + 20, entries);
    for (std::size_t i = 0; i < files.size(); ++i) {
        std::uint8_t* e = fst.data() + 12 * (2 + i);
        ::put32(e, static_cast<std::uint32_t>(names.size()));
        ::put32(e + 4, static_cast<std::uint32_t>(files[i].first >> 2));
        ::put32(e + 8, files[i].second);
        names += "f" + std::to_string(i);
        names += '\0';
        p.fill(files[i].first, files[i].second, static_cast<std::uint8_t>(0x60 + i));
    }
    fst.insert(fst.end(), names.begin(), names.end());
    while (fst.size() % 4) fst.push_back(0);
    p.put(kFst, fst.data(), fst.size());
    p.put32(0x428, static_cast<std::uint32_t>(fst.size() >> 2));
    p.put32(0x42C, static_cast<std::uint32_t>(fst.size() >> 2));
}

constexpr std::uint64_t kUpdate = 0x50000;
constexpr std::uint64_t kChannel = 0x800000;
constexpr std::uint64_t kGame = 0x1000000;
constexpr std::uint64_t kKorean = 0x14000000;
constexpr std::uint64_t kFarFile = 200ull << 20;  // decrypted offset of the game's far file

struct Built {
    SparseDisc disc;
    TestPartition update, channel, game, korean;
};

// Four partitions in three of the four groups: the update and game in
// group 0, a channel installer in group 1, and an odd partition with a
// Korean ticket in group 3, which cannot be looked into.
std::unique_ptr<Built> build_disc() {
    auto b = std::make_unique<Built>();
    SparseDisc& d = b->disc;
    const char id[] = "RDCE01";
    d.write(0, reinterpret_cast<const std::uint8_t*>(id), 6);
    d.write32(0x18, riftwii::kWiiMagic);
    const char title[] = "Disc Copy Test";
    d.write(0x20, reinterpret_cast<const std::uint8_t*>(title), sizeof(title) - 1);
    d.write32(0x40000, 2);
    d.write32(0x40004, 0x40020 >> 2);
    d.write32(0x40008, 1);
    d.write32(0x4000C, 0x40040 >> 2);
    d.write32(0x40018, 1);
    d.write32(0x4001C, 0x40060 >> 2);
    d.write32(0x40020, kUpdate >> 2);
    d.write32(0x40024, 1);
    d.write32(0x40028, kGame >> 2);
    d.write32(0x4002C, 0);
    d.write32(0x40040, kChannel >> 2);
    d.write32(0x40044, 2);
    d.write32(0x40060, kKorean >> 2);
    d.write32(0x40064, 0x52445858);  // a type no one names: copied all the same

    const auto setup = [](TestPartition& p, std::uint64_t offset, std::uint64_t data_size, std::uint8_t key) {
        p.offset = offset;
        p.data_size = data_size;
        for (int i = 0; i < 16; ++i) p.title_key[i] = static_cast<std::uint8_t>(key + i);
    };
    setup(b->update, kUpdate, 0x700000, 0x10);
    lay_out(b->update, {{0x80000, 0x1000}});
    setup(b->channel, kChannel, 0x600000, 0x20);
    lay_out(b->channel, {{0x60000, 0x20}});
    setup(b->game, kGame, 0x10000000, 0x30);  // 256 MiB of data
    lay_out(b->game, {{0x100000, 0x8000}, {kFarFile, 0x10000}});
    setup(b->korean, kKorean, 0x400000, 0x40);
    b->korean.key_index = 1;
    lay_out(b->korean, {});

    b->update.seal(d, 1);
    b->channel.seal(d, 2);
    b->game.seal(d, 0);
    b->korean.seal(d, 3);
    return b;
}

bool sector_used(const riftwii::DiscUsage& usage, std::uint64_t raw_offset) {
    return usage.sectors[static_cast<std::size_t>(raw_offset / kWiiSectorBytes)];
}

// The raw offset of the cluster that holds decrypted byte `at`.
std::uint64_t cluster_at(const TestPartition& p, std::uint64_t at) {
    return p.data_offset() + at / kClusterDataBytes * kClusterBytes;
}

void test_usage_covers_every_partition() {
    const auto b = build_disc();
    riftwii::DiscUsage usage;
    std::string error;
    EXPECT_TRUE(riftwii::map_disc_usage(b->disc, kCommonKey, usage, error));
    EXPECT_EQ(error, std::string());
    EXPECT_EQ(usage.partitions.size(), std::size_t(4));
    if (usage.partitions.size() != 4) return;
    // In table order: group 0's update and game, group 1's channel, group 3's odd one.
    EXPECT_EQ(usage.partitions[0].type, 1u);
    EXPECT_EQ(usage.partitions[1].type, 0u);
    EXPECT_EQ(usage.partitions[2].type, 2u);
    EXPECT_EQ(usage.partitions[2].group, 1u);
    EXPECT_EQ(usage.partitions[3].group, 3u);
    EXPECT_FALSE(usage.partitions[0].whole);
    EXPECT_FALSE(usage.partitions[1].whole);
    EXPECT_FALSE(usage.partitions[2].whole);
    EXPECT_TRUE(usage.partitions[3].whole);
    EXPECT_TRUE(usage.partitions[3].why.find("common key") != std::string::npos);

    // The system area, every partition's header and H3, and its files.
    for (std::uint64_t at = 0; at < 0x50000; at += kWiiSectorBytes) EXPECT_TRUE(sector_used(usage, at));
    for (const TestPartition* p : {&b->update, &b->channel, &b->game, &b->korean}) {
        EXPECT_TRUE(sector_used(usage, p->offset));
        EXPECT_TRUE(sector_used(usage, p->offset + 0x8000));
        EXPECT_TRUE(sector_used(usage, p->offset + 0x1FFFF));
        EXPECT_TRUE(sector_used(usage, cluster_at(*p, 0)));
    }
    EXPECT_TRUE(sector_used(usage, cluster_at(b->update, 0x80000)));
    EXPECT_TRUE(sector_used(usage, cluster_at(b->channel, 0x60000)));
    // The game's DOL, apploader end, file table, and both files: the far
    // one spans three clusters.
    EXPECT_TRUE(sector_used(usage, cluster_at(b->game, 0x2460 + 0x9100 - 1)));
    EXPECT_TRUE(sector_used(usage, cluster_at(b->game, 0x20000 + 0x10100 - 1)));
    EXPECT_TRUE(sector_used(usage, cluster_at(b->game, 0x40000)));
    EXPECT_TRUE(sector_used(usage, cluster_at(b->game, 0x100000)));
    EXPECT_TRUE(sector_used(usage, cluster_at(b->game, 0x100000 + 0x7FFF)));
    EXPECT_TRUE(sector_used(usage, cluster_at(b->game, kFarFile)));
    EXPECT_TRUE(sector_used(usage, cluster_at(b->game, kFarFile + 0xFFFF)));
    // What no file uses is left out: between the two files of the game,
    // and past its far file.
    EXPECT_FALSE(sector_used(usage, cluster_at(b->game, 100ull << 20)));
    EXPECT_FALSE(sector_used(usage, cluster_at(b->game, kFarFile + 0x20000)));
    // The partition kept whole: all of its data, to its last sector.
    EXPECT_TRUE(sector_used(usage, b->korean.data_offset() + 0x200000));
    EXPECT_TRUE(sector_used(usage, b->korean.data_offset() + b->korean.data_size - 1));
    EXPECT_FALSE(sector_used(usage, b->korean.data_offset() + b->korean.data_size));
}

void test_without_key_everything_is_whole() {
    const auto b = build_disc();
    riftwii::DiscUsage usage;
    std::string error;
    EXPECT_TRUE(riftwii::map_disc_usage(b->disc, nullptr, usage, error));
    EXPECT_EQ(usage.partitions.size(), std::size_t(4));
    for (const auto& p : usage.partitions) EXPECT_TRUE(p.whole);
    EXPECT_TRUE(sector_used(usage, cluster_at(b->game, 100ull << 20)));
    EXPECT_TRUE(sector_used(usage, b->game.data_offset() + b->game.data_size - 1));
}

void test_wrong_key_keeps_the_partition_whole() {
    const auto b = build_disc();
    const std::uint8_t wrong[16] = {9};
    riftwii::DiscUsage usage;
    std::string error;
    EXPECT_TRUE(riftwii::map_disc_usage(b->disc, wrong, usage, error));
    for (const auto& p : usage.partitions) EXPECT_TRUE(p.whole);
}

void test_not_a_wii_disc() {
    SparseDisc empty;
    riftwii::DiscUsage usage;
    std::string error;
    EXPECT_FALSE(riftwii::map_disc_usage(empty, kCommonKey, usage, error));
    EXPECT_FALSE(error.empty());
}

class MemorySink final : public riftwii::WbfsSink {
public:
    Bytes bytes;
    bool write(const std::uint8_t* data, std::size_t length, std::string&) override {
        bytes.insert(bytes.end(), data, data + length);
        return true;
    }
};

void test_image_reads_back_as_the_disc() {
    const auto b = build_disc();
    riftwii::DiscUsage usage;
    std::string error;
    EXPECT_TRUE(riftwii::map_disc_usage(b->disc, kCommonKey, usage, error));
    riftwii::WbfsImagePlan plan;
    EXPECT_TRUE(riftwii::plan_wbfs_image(usage, plan, error));
    EXPECT_EQ(plan.block_table.size(), std::size_t(riftwii::kWbfsFileDiscBlocks));
    EXPECT_EQ(plan.block_table[0], 1);
    EXPECT_EQ(plan.file_bytes, (plan.stored.size() + 1) * std::uint64_t(riftwii::kWbfsFileBlockBytes));
    // The gap in the game partition is not stored.
    EXPECT_EQ(plan.block_table[static_cast<std::size_t>(cluster_at(b->game, 100ull << 20) >> 21)], 0);

    MemorySink sink;
    std::uint64_t last_done = 0, last_total = 0;
    EXPECT_TRUE(riftwii::write_wbfs_image(b->disc, plan, usage, sink,
                                          [&](std::uint64_t done, std::uint64_t total) {
                                              last_done = done;
                                              last_total = total;
                                              return true;
                                          },
                                          error));
    EXPECT_EQ(sink.bytes.size(), plan.file_bytes);
    EXPECT_EQ(last_done, last_total);
    EXPECT_EQ(std::memcmp(sink.bytes.data(), "WBFS", 4), 0);
    EXPECT_EQ(int(sink.bytes[9]), 21);

    // RiftWii's own reader takes the image as a game, and every used
    // sector reads back as it is on the disc.
    riftwii::UsbImage image;
    image.format = riftwii::UsbImageFormat::Wbfs;
    riftwii::UsbImagePiece piece;
    const std::uint64_t file_bytes = sink.bytes.size();
    piece.source = std::make_shared<riftwii::MemorySource>(std::move(sink.bytes));
    piece.file.entry.size = static_cast<std::uint32_t>(file_bytes);
    piece.file.fragments.push_back({0, file_bytes / 512});
    image.pieces.push_back(piece);
    std::unique_ptr<riftwii::UsbDiscSource> copy;
    EXPECT_TRUE(riftwii::UsbDiscSource::open(image, copy, error));
    if (!copy) return;
    riftwii::DiscHeader header;
    EXPECT_TRUE(riftwii::read_disc_header(*copy, header, error));
    EXPECT_EQ(header.game_id, std::string("RDCE01"));
    Bytes want(kWiiSectorBytes), got(kWiiSectorBytes);
    unsigned compared = 0;
    for (std::size_t sector = 0; sector < usage.sectors.size(); ++sector) {
        if (!usage.sectors[sector]) continue;
        const std::uint64_t at = std::uint64_t(sector) * kWiiSectorBytes;
        EXPECT_TRUE(b->disc.read(at, want.data(), want.size()));
        EXPECT_TRUE(copy->read(at, got.data(), got.size()));
        if (want != got) {
            std::cerr << "sector " << sector << " differs" << std::endl;
            ++g_failures;
            break;
        }
        ++compared;
    }
    EXPECT_EQ(compared, usage.used_sectors());
    // Each partition's files decrypt from the copy as from the disc.
    std::unique_ptr<riftwii::WiiPartitionSource> game;
    EXPECT_TRUE(riftwii::open_game_partition(*copy, kCommonKey, game, error));
    if (game) {
        std::uint8_t far[4] = {};
        EXPECT_TRUE(game->read(kFarFile + 0x8000, far, sizeof(far)));
        EXPECT_EQ(int(far[0]), 0x61);
    }
}

void test_stopped_copy() {
    const auto b = build_disc();
    riftwii::DiscUsage usage;
    std::string error;
    EXPECT_TRUE(riftwii::map_disc_usage(b->disc, kCommonKey, usage, error));
    riftwii::WbfsImagePlan plan;
    EXPECT_TRUE(riftwii::plan_wbfs_image(usage, plan, error));
    MemorySink sink;
    EXPECT_FALSE(riftwii::write_wbfs_image(b->disc, plan, usage, sink,
                                           [](std::uint64_t done, std::uint64_t) { return done == 0; }, error));
    EXPECT_TRUE(error.find("stopped") != std::string::npos);
}

// A needed sector that does not read stops the copy; one that only shares
// a block with needed ones (past the end of a single-layer disc) is zeros.
void test_unreadable_sectors() {
    riftwii::DiscUsage usage;
    usage.sectors.assign(riftwii::kWiiDualLayerSectors, false);
    usage.sectors[0] = true;
    usage.sectors[riftwii::kWiiSingleLayerSectors - 1] = true;  // in the block that crosses the end
    SparseDisc disc;
    const char id[] = "RDCE01";
    disc.write(0, reinterpret_cast<const std::uint8_t*>(id), 6);
    riftwii::WbfsImagePlan plan;
    std::string error;
    EXPECT_TRUE(riftwii::plan_wbfs_image(usage, plan, error));
    EXPECT_EQ(plan.stored.size(), std::size_t(2));
    MemorySink sink;
    EXPECT_TRUE(riftwii::write_wbfs_image(disc, plan, usage, sink, nullptr, error));
    usage.sectors[riftwii::kWiiSingleLayerSectors] = true;  // needed, and past the end
    MemorySink failed;
    EXPECT_FALSE(riftwii::write_wbfs_image(disc, plan, usage, failed, nullptr, error));
    EXPECT_TRUE(error.find("143432") != std::string::npos);
}

void test_names() {
    EXPECT_EQ(riftwii::wbfs_piece_path("usb:/wbfs/A [RDCE01]/RDCE01.wbfs", 0),
              std::string("usb:/wbfs/A [RDCE01]/RDCE01.wbfs"));
    EXPECT_EQ(riftwii::wbfs_piece_path("usb:/wbfs/A [RDCE01]/RDCE01.wbfs", 2),
              std::string("usb:/wbfs/A [RDCE01]/RDCE01.wbf2"));
    EXPECT_EQ(riftwii::wbfs_piece_count(riftwii::kWbfsSplitBytes), 1u);
    EXPECT_EQ(riftwii::wbfs_piece_count(riftwii::kWbfsSplitBytes + 1), 2u);
    EXPECT_EQ(riftwii::wbfs_piece_count(std::uint64_t(8) << 30), 3u);
    EXPECT_EQ(riftwii::wbfs_game_folder_name("Mario Kart Wii", "RMCE01"), std::string("Mario Kart Wii [RMCE01]"));
    EXPECT_EQ(riftwii::wbfs_game_folder_name("What?: A/B \"Game\". ", "RXXE01"),
              std::string("What__ A_B _Game_ [RXXE01]"));
    EXPECT_EQ(riftwii::wbfs_game_folder_name("  ", "RXXE01"), std::string("RXXE01"));
    // Shift-JIS katakana SO is 0x83 0x5C: its second byte is not a backslash.
    EXPECT_EQ(riftwii::wbfs_game_folder_name("\x83\x5C", "RXXJ01"), std::string("\xE3\x82\xBD [RXXJ01]"));
    // Cut at 200 bytes, never inside a character.
    const std::string long_title = std::string(199, 'a') + "\xC3\xA9" + "b";
    EXPECT_EQ(riftwii::wbfs_game_folder_name(long_title, "RXXE01"), std::string(199, 'a') + " [RXXE01]");
}

}  // namespace

int main() {
    test_usage_covers_every_partition();
    test_without_key_everything_is_whole();
    test_wrong_key_keeps_the_partition_whole();
    test_not_a_wii_disc();
    test_image_reads_back_as_the_disc();
    test_stopped_copy();
    test_unreadable_sectors();
    test_names();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "discdump tests passed" << std::endl;
    return 0;
}
