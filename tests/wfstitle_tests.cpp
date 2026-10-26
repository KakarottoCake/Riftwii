// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/wfstitle.hpp"

#include <algorithm>
#include <cstdint>
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

const std::uint8_t kKey[16] = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

// A TMD with no contents: what the title finder reads of it.
Bytes tmd(std::uint64_t title_id, std::uint16_t version, std::uint32_t ios, bool wfs) {
    Bytes t(0x1E4, 0);
    be32(&t[0x184], 1);
    be32(&t[0x188], ios);
    be32(&t[0x18C], static_cast<std::uint32_t>(title_id >> 32));
    be32(&t[0x190], static_cast<std::uint32_t>(title_id));
    be32(&t[0x194], wfs ? 0x21 : 0x01);
    be16(&t[0x198], 0x4744);
    be16(&t[0x1DC], version);
    return t;
}

Entry file_entry(Drive& d, const Area& area, const std::string& name, const Bytes& data, std::uint32_t block) {
    Entry e;
    e.name = name;
    e.file_size = static_cast<std::uint32_t>(data.size());
    if (data.size() <= 300) {
        e.size_on_disk = e.file_size;
        e.inline_data = data;
        return e;
    }
    e.category = 1;
    e.size_on_disk = 8192;
    add_block_unit(e, block, d.put_data(area, block, data));
    return e;
}

Entry dir_entry(const std::string& name, std::uint32_t tree) {
    Entry e;
    e.name = name;
    e.flags = kDirectory;
    e.directory_block = tree;
    return e;
}

void put_dir(Drive& d, const Area& area, std::uint32_t block, const std::vector<Entry>& entries) {
    TreeBlock t(area.log2, true);
    for (const Entry& e : entries) t.add_entry(e);
    d.put_metadata(area, block, t.finish());
}

// What Dragon Quest X's installer leaves, with an add-on, a second
// title whose install did not finish, and folders that are no titles.
Drive installed_drive(bool with_program = true) {
    Drive d(256, kKey);
    const Area root{0, 0x600DCAFE, 13};
    d.put_header(root, 3);
    put_dir(d, root, 3, {dir_entry("title", 4), dir_entry("tmp", 5), dir_entry("_install", 6)});
    put_dir(d, root, 5, {});
    put_dir(d, root, 6, {});
    put_dir(d, root, 4, {dir_entry("GD", 7), dir_entry("toolong", 8)});
    put_dir(d, root, 8, {});
    put_dir(d, root, 7, {dir_entry("S4MJ", 9), dir_entry("S4SJ", 10), dir_entry("xx", 11)});
    put_dir(d, root, 11, {});
    // S4MJ: meta, content with the program.
    put_dir(d, root, 9, {dir_entry("meta", 12), dir_entry("content", 13), dir_entry("save", 14)});
    put_dir(d, root, 12, {file_entry(d, root, "0001000453344d4a.tmd", tmd(0x0001000453344D4Aull, 0x104, 59, true), 30),
                          file_entry(d, root, "0001000553345341.tmd", tmd(0x0001000553345341ull, 3, 59, true), 31),
                          file_entry(d, root, "notes.txt", Bytes{'x'}, 0)});
    std::vector<Entry> content;
    if (with_program) content.push_back(file_entry(d, root, "default.dol", pattern(5000, 7), 32));
    put_dir(d, root, 13, content);
    put_dir(d, root, 14, {});
    // S4SJ: a TMD that names another title.
    put_dir(d, root, 10, {dir_entry("meta", 15)});
    put_dir(d, root, 15, {file_entry(d, root, "0001000453345341.tmd", tmd(0x0001000453344D4Aull, 1, 59, true), 33)});
    return d;
}

void test_finds_installed_titles() {
    Drive d = installed_drive();
    WfsVolume v;
    std::string error;
    EXPECT_TRUE(v.open(d.reader(), 0, d.sectors(), kKey, error));
    std::vector<WfsTitle> titles;
    std::vector<std::string> notes;
    EXPECT_TRUE(find_wfs_titles(v, titles, notes, error));
    EXPECT_EQ(titles.size(), std::size_t(1));
    if (titles.size() == 1) {
        const WfsTitle& t = titles[0];
        EXPECT_EQ(t.path, std::string("/title/GD/S4MJ"));
        EXPECT_EQ(t.code, std::string("S4MJ"));
        EXPECT_EQ(t.group, std::string("GD"));
        EXPECT_EQ(t.title_id, 0x0001000453344D4Aull);
        EXPECT_EQ(t.version, 0x104);
        EXPECT_EQ(t.ios, 59u);
        EXPECT_TRUE(t.wfs_type);
        EXPECT_TRUE(t.program);
        EXPECT_EQ(t.program_bytes, 5000u);
        EXPECT_EQ(t.add_ons.size(), std::size_t(1));
        if (!t.add_ons.empty()) EXPECT_EQ(t.add_ons[0], 0x0001000553345341ull);
        EXPECT_EQ(wfs_title_text(t.title_id), std::string("00010004-S4MJ"));
    }
    // The bad title and the folders that are not titles are named.
    std::string all;
    for (const std::string& n : notes) all += n + "\n";
    EXPECT_TRUE(all.find("/title/toolong") != std::string::npos);
    EXPECT_TRUE(all.find("/title/GD/xx") != std::string::npos);
    EXPECT_TRUE(all.find("/title/GD/S4SJ") != std::string::npos);
}

void test_disc_to_title() {
    Drive d = installed_drive();
    WfsVolume v;
    std::string error;
    EXPECT_TRUE(v.open(d.reader(), 0, d.sectors(), kKey, error));
    std::vector<WfsTitle> titles;
    std::vector<std::string> notes;
    EXPECT_TRUE(find_wfs_titles(v, titles, notes, error));
    // Its own disc, the all-in-one disc (same maker), a stranger's.
    EXPECT_EQ(wfs_title_for_disc("S4MJGD", titles), 0);
    EXPECT_EQ(wfs_title_for_disc("S6TJGD", titles), 0);
    EXPECT_EQ(wfs_title_for_disc("RMGE01", titles), -1);
    EXPECT_EQ(wfs_title_for_disc("S4", titles), -1);

    // Without its program (an install that stopped), no disc maps to it.
    Drive partial = installed_drive(false);
    EXPECT_TRUE(v.open(partial.reader(), 0, partial.sectors(), kKey, error));
    EXPECT_TRUE(find_wfs_titles(v, titles, notes, error));
    EXPECT_EQ(titles.size(), std::size_t(1));
    if (!titles.empty()) EXPECT_FALSE(titles[0].program);
    EXPECT_EQ(wfs_title_for_disc("S4MJGD", titles), -1);
}

void test_empty_drive() {
    Drive d(64, kKey);
    const Area root{0, 1, 13};
    d.put_header(root, 3);
    put_dir(d, root, 3, {});
    WfsVolume v;
    std::string error;
    EXPECT_TRUE(v.open(d.reader(), 0, d.sectors(), kKey, error));
    std::vector<WfsTitle> titles;
    std::vector<std::string> notes;
    EXPECT_FALSE(find_wfs_titles(v, titles, notes, error));
    EXPECT_TRUE(error.find("/title") != std::string::npos);
}

void test_disc_and_tmd_kinds() {
    std::vector<PartitionEntry> table(2);
    table[0].type = 0;
    table[1].type = 1;
    EXPECT_FALSE(is_wfs_install_disc(table));
    table.push_back(PartitionEntry{});
    table.back().type = kWfsInstallerPartition;
    EXPECT_TRUE(is_wfs_install_disc(table));

    const Bytes wfs = tmd(1, 1, 59, true);
    const Bytes plain = tmd(1, 1, 59, false);
    EXPECT_TRUE(tmd_is_wfs_title(wfs.data(), wfs.size()));
    EXPECT_FALSE(tmd_is_wfs_title(plain.data(), plain.size()));
    EXPECT_FALSE(tmd_is_wfs_title(wfs.data(), 0x100));
}

}  // namespace

// Whether the list of titles is the whole answer: a drive read whole, or
// one with no /title, is; a read error in a group or a title is not, so a
// game left out of the list is not taken as missing.
void test_complete_scan() {
    WfsVolume v;
    std::string error;
    std::vector<WfsTitle> titles;
    std::vector<std::string> notes;
    bool complete = false;

    // The test drive's odd folders and a TMD naming another title are
    // what is there, not read errors.
    Drive whole = installed_drive();
    EXPECT_TRUE(v.open(whole.reader(), 0, whole.sectors(), kKey, error));
    EXPECT_TRUE(find_wfs_titles(v, titles, notes, error, &complete));
    EXPECT_TRUE(complete);
    if (!titles.empty()) EXPECT_TRUE(titles[0].program_checked);

    // The root area's block b is physical block 2b: /title/GD's tree is
    // block 7, S4MJ's content folder block 13.
    Drive bad_group = installed_drive();
    bad_group.image()[14 * 4096 + 300] ^= 0x01;
    EXPECT_TRUE(v.open(bad_group.reader(), 0, bad_group.sectors(), kKey, error));
    EXPECT_TRUE(find_wfs_titles(v, titles, notes, error, &complete));
    EXPECT_FALSE(complete);
    EXPECT_EQ(titles.size(), std::size_t(0));

    Drive bad_content = installed_drive();
    bad_content.image()[26 * 4096 + 300] ^= 0x01;
    EXPECT_TRUE(v.open(bad_content.reader(), 0, bad_content.sectors(), kKey, error));
    EXPECT_TRUE(find_wfs_titles(v, titles, notes, error, &complete));
    EXPECT_FALSE(complete);
    EXPECT_EQ(titles.size(), std::size_t(1));
    if (!titles.empty()) {
        EXPECT_FALSE(titles[0].program);
        EXPECT_FALSE(titles[0].program_checked);
    }

    // No /title: nothing is installed, and that is known.
    Drive empty(64, kKey);
    const Area root{0, 1, 13};
    empty.put_header(root, 3);
    put_dir(empty, root, 3, {});
    EXPECT_TRUE(v.open(empty.reader(), 0, empty.sectors(), kKey, error));
    EXPECT_FALSE(find_wfs_titles(v, titles, notes, error, &complete));
    EXPECT_TRUE(complete);
}

// A key file, raw or as hex, opens the drive it is the key of.
void test_key_file() {
    std::uint8_t key[16] = {};
    EXPECT_TRUE(parse_wfs_key(kKey, sizeof kKey, key));
    EXPECT_TRUE(std::equal(key, key + 16, kKey));
    const std::string hex = "09 08 07 06 05 04 03 02\r\n01:00:aa:bb:CC:DD:ee:ff\n";
    std::uint8_t from_hex[16] = {};
    EXPECT_TRUE(parse_wfs_key(reinterpret_cast<const std::uint8_t*>(hex.data()), hex.size(), from_hex));
    EXPECT_TRUE(std::equal(from_hex, from_hex + 16, kKey));

    Drive d = installed_drive();
    WfsVolume v;
    std::string error;
    EXPECT_TRUE(v.open(d.reader(), 0, d.sectors(), from_hex, error));
    std::vector<WfsTitle> titles;
    std::vector<std::string> notes;
    EXPECT_TRUE(find_wfs_titles(v, titles, notes, error));
    EXPECT_EQ(wfs_title_for_disc("S6TJGD", titles), 0);

    std::uint8_t untouched[16] = {0x55};
    const std::uint8_t zero[16] = {};
    const std::string short_hex = "0908070605040302010000aabbccddeef";
    const std::string long_hex = short_hex + "f0";
    const std::string bad_hex = "0908070605040302010000aabbccddeeg0";
    EXPECT_FALSE(parse_wfs_key(zero, sizeof zero, untouched));
    EXPECT_FALSE(parse_wfs_key(reinterpret_cast<const std::uint8_t*>(short_hex.data()), short_hex.size(), untouched));
    EXPECT_FALSE(parse_wfs_key(reinterpret_cast<const std::uint8_t*>(long_hex.data()), long_hex.size(), untouched));
    EXPECT_FALSE(parse_wfs_key(reinterpret_cast<const std::uint8_t*>(bad_hex.data()), bad_hex.size(), untouched));
    EXPECT_FALSE(parse_wfs_key(nullptr, 16, untouched));
    EXPECT_EQ(untouched[0], 0x55);
}

int main() {
    test_complete_scan();
    test_key_file();
    test_finds_installed_titles();
    test_disc_to_title();
    test_empty_drive();
    test_disc_and_tmd_kinds();
    if (g_failures == 0) std::cout << "wfstitle tests passed\n";
    else std::cerr << g_failures << " TEST CHECKS FAILED\n";
    return g_failures == 0 ? 0 : 1;
}
