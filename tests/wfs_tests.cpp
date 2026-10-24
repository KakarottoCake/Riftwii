// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/wfs.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "wfs_image.hpp"

using namespace riftwii;
using namespace wfsimage;

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { const auto va_ = (a); const auto vb_ = (b); if (va_ != vb_) { std::cerr << "FAILED: " #a " == " #b " (" << va_ << " != " << vb_ << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

const std::uint8_t kKey[16] = {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
                               0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
const std::uint8_t kOtherKey[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

// The files the drive holds, to compare reads with.
struct Contents {
    Bytes blocks_file = pattern(10000, 1);
    Bytes large_file = pattern(20000, 2);
    Bytes plain_file = pattern(3000, 3);
    Bytes clusters_file = pattern(100000, 4);
    Bytes meta_file = pattern(40000, 5);
    std::string readme = "hello wfs";
};

Entry inline_file(const std::string& name, const std::string& text) {
    Entry e;
    e.name = name;
    e.file_size = e.size_on_disk = static_cast<std::uint32_t>(text.size());
    e.inline_data.assign(text.begin(), text.end());
    return e;
}

Entry directory(const std::string& name, std::uint32_t tree_block, std::uint32_t extra_flags = 0) {
    Entry e;
    e.name = name;
    e.flags = kDirectory | extra_flags;
    e.directory_block = tree_block;
    return e;
}

// The drive the tests read. The root area has 8 KiB blocks; "quota" is
// an area of 4 KiB blocks inside it.
//   /Readme.TXT                 inline
//   /title/Blocks.bin           two 8 KiB blocks
//   /title/large.bin            one 64 KiB large block
//   /title/plain.bin            one block, stored unencrypted
//   /big/{alpha,kilo,mike,zulu} a directory of two leaf blocks under a parent tree
//   /quota/clusters.bin         one cluster, listed in its entry
//   /quota/meta.bin             one cluster, listed in a metadata block
Drive make_drive(const std::uint8_t* key, const Contents& c) {
    Drive d(512, key);
    const Area root{0, 0xA1B2C3D4, 13};
    d.put_header(root, 3);

    // /title
    Entry blocks;
    blocks.name = "Blocks.bin";
    blocks.category = 1;
    blocks.file_size = 10000;
    blocks.size_on_disk = 16384;
    add_block_unit(blocks, 11, d.put_data(root, 11, Bytes(c.blocks_file.begin(), c.blocks_file.begin() + 8192)));
    add_block_unit(blocks, 12, d.put_data(root, 12, Bytes(c.blocks_file.begin() + 8192, c.blocks_file.end())));
    Entry large;
    large.name = "large.bin";
    large.category = 2;
    large.file_size = 20000;
    large.size_on_disk = 65536;
    add_block_unit(large, 16, d.put_data(root, 16, c.large_file));
    Entry plain;
    plain.name = "plain.bin";
    plain.flags = kUnencrypted;
    plain.category = 1;
    plain.file_size = 3000;
    plain.size_on_disk = 8192;
    add_block_unit(plain, 13, d.put_data(root, 13, c.plain_file, false));
    TreeBlock title(13, true);
    title.add_entry(blocks);
    title.add_entry(large);
    title.add_entry(plain);
    d.put_metadata(root, 10, title.finish());

    // /big: names before "m" in block 31, the rest in block 32.
    TreeBlock big(13, false);
    big.add_child("", 31);
    big.add_child("m", 32);
    d.put_metadata(root, 30, big.finish());
    TreeBlock left(13, true);
    left.add_entry(inline_file("alpha", "A"));
    left.add_entry(inline_file("kilo", "K"));
    d.put_metadata(root, 31, left.finish());
    TreeBlock right(13, true);
    right.add_entry(inline_file("mike", "M"));
    right.add_entry(inline_file("zulu", "Z"));
    d.put_metadata(root, 32, right.finish());

    // /quota: its header at the root area's block 40.
    const Area quota{root.physical(40), 0x0BADF00D, 12};
    d.put_quota_header(root, 40, quota, 3);
    Entry clusters;
    clusters.name = "clusters.bin";
    clusters.category = 3;
    clusters.file_size = 100000;
    clusters.size_on_disk = 262144;
    {
        // One cluster of eight 32 KiB large blocks at quota block 64; the
        // file fills four of them.
        Bytes unit(4 + 8 * 20, 0);
        be32(unit.data(), 64);
        for (std::uint32_t j = 0; j < 4; ++j) {
            const std::size_t from = j * 32768;
            const std::size_t to = std::min<std::size_t>(from + 32768, c.clusters_file.size());
            const auto h = d.put_data(quota, 64 + j * 8, Bytes(c.clusters_file.begin() + from, c.clusters_file.begin() + to));
            std::memcpy(unit.data() + 4 + j * 20, h.data(), 20);
        }
        clusters.units = unit;
        clusters.unit_bytes = unit.size();
    }
    Entry meta;
    meta.name = "meta.bin";
    meta.category = 4;
    meta.file_size = 40000;
    meta.size_on_disk = 262144;
    {
        // The entry lists one metadata block (quota block 10), which lists
        // one cluster (quota block 256).
        Bytes list(4096, 0);
        be32(list.data(), 0);
        be32(list.data() + 0x18, 256);
        for (std::uint32_t j = 0; j < 2; ++j) {
            const std::size_t from = j * 32768;
            const std::size_t to = std::min<std::size_t>(from + 32768, c.meta_file.size());
            const auto h = d.put_data(quota, 256 + j * 8, Bytes(c.meta_file.begin() + from, c.meta_file.begin() + to));
            std::memcpy(list.data() + 0x18 + 4 + j * 20, h.data(), 20);
        }
        d.put_metadata(quota, 10, list);
        meta.units.resize(4);
        be32(meta.units.data(), 10);
        meta.unit_bytes = 4;
    }
    TreeBlock quota_root(12, true);
    quota_root.add_entry(clusters);
    quota_root.add_entry(meta);
    d.put_metadata(quota, 3, quota_root.finish());

    TreeBlock top(13, true);
    top.add_entry(inline_file("Readme.TXT", c.readme));
    top.add_entry(directory("title", 10));
    top.add_entry(directory("big", 30));
    top.add_entry(directory("quota", 40, kQuota | kAreaBasic));
    d.put_metadata(root, 3, top.finish());
    return d;
}

bool read_file(const WfsVolume& v, const std::string& path, Bytes& out) {
    std::string error;
    if (!v.read_all(path, out, 1 << 20, error)) {
        std::cerr << "read " << path << ": " << error << "\n";
        return false;
    }
    return true;
}

void test_open_and_read_every_layout() {
    const Contents c;
    Drive d = make_drive(kKey, c);
    WfsVolume v;
    std::string error;
    EXPECT_TRUE(v.open(d.reader(), 0, d.sectors(), kKey, error));
    EXPECT_EQ(v.version(), 0x01010800u);
    EXPECT_EQ(v.root_block_log2(), 13);

    Bytes got;
    EXPECT_TRUE(read_file(v, "/Readme.TXT", got));
    EXPECT_TRUE(std::string(got.begin(), got.end()) == c.readme);
    EXPECT_TRUE(read_file(v, "/title/Blocks.bin", got) && got == c.blocks_file);
    EXPECT_TRUE(read_file(v, "/title/large.bin", got) && got == c.large_file);
    EXPECT_TRUE(read_file(v, "/title/plain.bin", got) && got == c.plain_file);
    EXPECT_TRUE(read_file(v, "/quota/clusters.bin", got) && got == c.clusters_file);
    EXPECT_TRUE(read_file(v, "/quota/meta.bin", got) && got == c.meta_file);
    EXPECT_TRUE(read_file(v, "/big/zulu", got) && got == Bytes{'Z'});
    EXPECT_TRUE(read_file(v, "/big/alpha", got) && got == Bytes{'A'});
}

void test_names_and_listing() {
    const Contents c;
    Drive d = make_drive(kKey, c);
    WfsVolume v;
    std::string error;
    EXPECT_TRUE(v.open(d.reader(), 0, d.sectors(), kKey, error));

    // Any case finds a name; the entry keeps the case it was made with.
    WfsEntry e;
    EXPECT_TRUE(v.find("/TITLE/blocks.BIN", e, error) == WfsFind::Found);
    EXPECT_EQ(e.name, std::string("Blocks.bin"));
    EXPECT_EQ(e.size, 10000u);
    EXPECT_FALSE(e.directory);

    WfsEntry root;
    EXPECT_TRUE(v.find("/", root, error) == WfsFind::Found);
    std::vector<WfsEntry> listed;
    EXPECT_TRUE(v.list(root, listed, error));
    EXPECT_EQ(listed.size(), std::size_t(4));
    if (listed.size() == 4) {
        EXPECT_EQ(listed[0].name, std::string("big"));
        EXPECT_EQ(listed[1].name, std::string("quota"));
        EXPECT_EQ(listed[2].name, std::string("Readme.TXT"));
        EXPECT_EQ(listed[3].name, std::string("title"));
        EXPECT_TRUE(listed[1].directory);
    }

    // A directory split over two blocks lists as one.
    WfsEntry big;
    EXPECT_TRUE(v.find("/big", big, error) == WfsFind::Found);
    EXPECT_TRUE(v.list(big, listed, error));
    std::string names;
    for (const WfsEntry& x : listed) names += x.name + ",";
    EXPECT_EQ(names, std::string("alpha,kilo,mike,zulu,"));

    // A quota lists through its own area.
    WfsEntry quota;
    EXPECT_TRUE(v.find("/quota", quota, error) == WfsFind::Found);
    EXPECT_TRUE(v.list(quota, listed, error));
    EXPECT_EQ(listed.size(), std::size_t(2));

    EXPECT_TRUE(v.find("/title/nothing.bin", e, error) == WfsFind::Missing);
    EXPECT_TRUE(v.find("/big/lima", e, error) == WfsFind::Missing);
    EXPECT_TRUE(v.find("/Readme.TXT/inside", e, error) == WfsFind::Missing);
    EXPECT_TRUE(v.find("/nothing/at/all", e, error) == WfsFind::Missing);
}

void test_partial_reads() {
    const Contents c;
    Drive d = make_drive(kKey, c);
    WfsVolume v;
    std::string error;
    EXPECT_TRUE(v.open(d.reader(), 0, d.sectors(), kKey, error));
    WfsEntry e;
    EXPECT_TRUE(v.find("/title/Blocks.bin", e, error) == WfsFind::Found);
    // Across the boundary of its two blocks.
    std::uint8_t part[1000];
    EXPECT_TRUE(v.read(e, 8000, part, sizeof part, error));
    EXPECT_TRUE(std::memcmp(part, c.blocks_file.data() + 8000, sizeof part) == 0);
    EXPECT_FALSE(v.read(e, 9500, part, sizeof part, error));  // past its end
    EXPECT_TRUE(v.find("/quota/clusters.bin", e, error) == WfsFind::Found);
    EXPECT_TRUE(v.read(e, 65000, part, sizeof part, error));  // the second and third large blocks
    EXPECT_TRUE(std::memcmp(part, c.clusters_file.data() + 65000, sizeof part) == 0);
}

void test_keys() {
    const Contents c;
    Drive d = make_drive(kKey, c);
    WfsVolume v;
    std::string error;
    // Another console's key does not open the drive, nor does none.
    EXPECT_FALSE(v.open(d.reader(), 0, d.sectors(), kOtherKey, error));
    EXPECT_TRUE(error.find("key") != std::string::npos);
    EXPECT_FALSE(v.open(d.reader(), 0, d.sectors(), nullptr, error));
    // The sector count is part of every IV: a size the drive reports wrong
    // falls back to the one block 0 proves.
    EXPECT_TRUE(v.open(d.reader(), 0, d.sectors() - 8, kKey, error));
    EXPECT_EQ(v.sectors(), d.sectors());

    // Without the drive's size, it is worked out from block 0.
    EXPECT_TRUE(v.open(d.reader(), 0, 0, kKey, error));
    EXPECT_EQ(v.sectors(), d.sectors());
    Bytes inner;
    EXPECT_TRUE(read_file(v, "/quota/clusters.bin", inner) && inner == c.clusters_file);
    EXPECT_FALSE(v.open(d.reader(), 0, 0, kOtherKey, error));

    // A drive made without encryption opens without a key.
    Drive plain = make_drive(nullptr, c);
    EXPECT_TRUE(v.open(plain.reader(), 0, plain.sectors(), nullptr, error));
    Bytes got;
    EXPECT_TRUE(read_file(v, "/quota/meta.bin", got) && got == c.meta_file);
    EXPECT_TRUE(v.open(plain.reader(), 0, 0, nullptr, error));
    EXPECT_EQ(v.sectors(), plain.sectors());
}

void test_damage() {
    const Contents c;
    Drive d = make_drive(kKey, c);
    // A byte of Blocks.bin's second block (root block 12, physical 24).
    d.image()[24 * 4096 + 100] ^= 0x40;
    WfsVolume v;
    std::string error;
    EXPECT_TRUE(v.open(d.reader(), 0, d.sectors(), kKey, error));
    Bytes got;
    EXPECT_FALSE(v.read_all("/title/Blocks.bin", got, 1 << 20, error));
    EXPECT_TRUE(error.find("hash") != std::string::npos);
    // Its first block is still whole.
    WfsEntry e;
    std::uint8_t first[100];
    EXPECT_TRUE(v.find("/title/Blocks.bin", e, error) == WfsFind::Found);
    EXPECT_TRUE(v.read(e, 0, first, sizeof first, error));

    // A damaged directory block fails, rather than reading as missing.
    d.image()[20 * 4096 + 300] ^= 0x01;  // /title's tree (root block 10)
    WfsVolume fresh;
    EXPECT_TRUE(fresh.open(d.reader(), 0, d.sectors(), kKey, error));
    EXPECT_TRUE(fresh.find("/title/large.bin", e, error) == WfsFind::Failed);

    // A file larger than the caller allows is refused.
    EXPECT_FALSE(fresh.read_all("/quota/clusters.bin", got, 50000, error));
}

void test_block_zero_size_and_drive_end() {
    WfsVolume v;
    std::string error;
    // A drive smaller than block 0.
    Drive tiny(1, kKey);
    EXPECT_FALSE(v.open(tiny.reader(), 0, 4, kKey, error));
    // A root area of 4 KiB blocks: block 0 is one physical block.
    const Contents c;
    Drive d(64, kKey);
    const Area root{0, 0x55, 12};
    d.put_header(root, 3);
    TreeBlock top(12, true);
    top.add_entry(inline_file("a.txt", "four"));
    d.put_metadata(root, 3, top.finish());
    EXPECT_TRUE(v.open(d.reader(), 0, d.sectors(), kKey, error));
    Bytes got;
    EXPECT_TRUE(read_file(v, "/A.TXT", got) && got == Bytes({'f', 'o', 'u', 'r'}));
}

}  // namespace

int main() {
    test_open_and_read_every_layout();
    test_names_and_listing();
    test_partial_reads();
    test_keys();
    test_damage();
    test_block_zero_size_and_drive_end();
    if (g_failures == 0) std::cout << "wfs tests passed\n";
    else std::cerr << g_failures << " TEST CHECKS FAILED\n";
    return g_failures == 0 ? 0 : 1;
}
