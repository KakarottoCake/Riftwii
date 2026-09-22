// SPDX-License-Identifier: GPL-3.0-or-later
// FAT32 resolver against a FAT32 image built in memory: boot sector, two
// FATs, a root directory with a long-named subdirectory, one contiguous and
// one deliberately fragmented file, broken chains of every kind, and the
// same volume behind an MBR with a bigger sector/cluster geometry.
#include "riftwii/fat32.hpp"

#include "fat32_image.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_CONTAINS(s, sub) do { if (std::string(s).find(sub) == std::string::npos) { std::cerr << "FAILED: '" << (s) << "' should contain '" << (sub) << "' at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using Bytes = std::vector<std::uint8_t>;
using riftwii::Fragment;
using fatimg::Image;
using fatimg::short_entry;
using fatimg::lfn_entries;
using fatimg::ucs;
using fatimg::put16;
using fatimg::put32;

namespace {

Bytes pattern(std::size_t n, std::uint8_t seed) {
    Bytes b(n);
    for (std::size_t i = 0; i < n; ++i) b[i] = static_cast<std::uint8_t>(seed + i * 13 + (i >> 8));
    return b;
}

void cat(Bytes& to, const Bytes& from) { to.insert(to.end(), from.begin(), from.end()); }

struct Fixture {
    Image img;
    Bytes short_txt, frag_xml, deep_bin;
    std::vector<std::uint32_t> frag_clusters{8, 10, 12, 13, 14, 20};
    explicit Fixture(std::uint32_t bps, std::uint32_t spc, std::uint64_t at) : img(bps, spc, at) {
        const std::uint32_t cb = img.cluster_bytes();
        short_txt = pattern(2 * cb + 276, 0x11);        // three clusters, last partly used
        frag_xml = pattern(6 * cb - 100, 0x77);        // six clusters, four fragments
        deep_bin = {1, 2, 3};
        img.write_data({4, 5, 6}, short_txt);
        img.write_data(frag_clusters, frag_xml);
        img.write_data({7}, pattern(cb, 0));            // belongs to the deleted entry
        img.write_data({40}, pattern(10, 0x40));
        img.chain({30, 31}); img.set_fat(31, 30);       // loop
        img.chain({41, 42}); img.set_fat(42, 0x0FFFFFF7);  // bad cluster
        img.set_fat(43, 0);                             // free
        img.set_fat(44, 5000);                          // out of range
        img.chain({45});
        img.write_data({50}, {'h', 'e', 'l', 'l', 'o'});
        img.write_data({61}, deep_bin);
        img.write_data({62}, pattern(7, 0x62));

        Bytes root;
        cat(root, short_entry("RIFTWII    ", 0x08, 0, 0));
        cat(root, lfn_entries(ucs(u"riivolution"), "RIIVOL~1   ", 0x10, 3, 0));
        cat(root, short_entry("SHORT   TXT", 0x20, 4, static_cast<std::uint32_t>(short_txt.size()), 0x18));
        cat(root, short_entry("\xE5LETED  TXT", 0x20, 7, cb));
        cat(root, lfn_entries(ucs(u"orphan long name.txt"), "ORPHAN  TXT", 0x20, 40, 10, true));
        cat(root, short_entry("LOOP    BIN", 0x20, 30, 100));
        cat(root, short_entry("BAD     BIN", 0x20, 41, 100));
        cat(root, short_entry("FREE    BIN", 0x20, 43, 100));
        cat(root, short_entry("RANGE   BIN", 0x20, 44, 100));
        cat(root, short_entry("OVERSIZEBIN", 0x20, 45, 100000));
        cat(root, short_entry("EMPTY   BIN", 0x20, 0, 0));
        if (!img.write_dir({2}, root)) { std::cerr << "test image: directory does not fit" << std::endl; g_failures++; }

        Bytes riiv;
        cat(riiv, short_entry(".          ", 0x10, 3, 0));
        cat(riiv, short_entry("..         ", 0x10, 0, 0));
        cat(riiv, lfn_entries(ucs(u"My Long Fragmented Mod.xml"), "MYLONG~1XML", 0x20, 8, static_cast<std::uint32_t>(frag_xml.size())));
        cat(riiv, lfn_entries(ucs(u"Ünïcode ñame.txt"), "NICODE~1TXT", 0x20, 50, 5));
        cat(riiv, lfn_entries(ucs(u"nested dir"), "NESTED~1   ", 0x10, 60, 0));
        cat(riiv, lfn_entries(ucs(u"abcdefghijklmnopqrstuvwxyz0123.dat"), "ABCDEF~1DAT", 0x20, 62, 7));
        for (int i = 0; i < 4; ++i) {
            char n[12];
            std::snprintf(n, sizeof n, "FILL%d   BIN", i);
            cat(riiv, short_entry(n, 0x20, 0, 0));
        }
        if (!img.write_dir({3, 70}, riiv)) { std::cerr << "test image: directory does not fit" << std::endl; g_failures++; }  // spills into a second cluster at the smallest geometry

        Bytes nested;
        cat(nested, short_entry(".          ", 0x10, 60, 0));
        cat(nested, short_entry("..         ", 0x10, 3, 0));
        cat(nested, lfn_entries(ucs(u"deep.bin"), "DEEP    BIN", 0x20, 61, 3));
        if (!img.write_dir({60}, nested)) { std::cerr << "test image: directory does not fit" << std::endl; g_failures++; }
    }
    std::uint64_t bpc() const { return img.cluster_bytes() / 512; }
};

bool ReadAll(const riftwii::Fat32Volume& v, const riftwii::Fat32File& f, Bytes& out) {
    out.assign(f.entry.size, 0);
    return v.read(f, 0, out.data(), out.size());
}

}  // namespace

static void test_geometry_and_lookup(std::uint32_t bps, std::uint32_t spc, std::uint64_t at) {
    Fixture fx(bps, spc, at);
    riftwii::Fat32Volume v;
    std::string err;
    EXPECT_TRUE(riftwii::Fat32Volume::mount(fx.img.reader(), v, err));
    if (!err.empty()) { std::cerr << err << std::endl; return; }
    const riftwii::Fat32Geometry& g = v.geometry();
    EXPECT_EQ(g.volume_lba, at);
    EXPECT_EQ(g.bytes_per_sector, bps);
    EXPECT_EQ(g.sectors_per_cluster, spc);
    EXPECT_EQ(g.cluster_count, std::uint32_t(256));
    EXPECT_EQ(g.root_cluster, std::uint32_t(2));
    EXPECT_EQ(g.data_start_sector, std::uint32_t(fx.img.data_start_sector()));
    EXPECT_EQ(g.cluster_lba(2), fx.img.cluster_lba(2));
    EXPECT_EQ(g.cluster_lba(9), fx.img.cluster_lba(9));

    riftwii::Fat32File f;
    EXPECT_TRUE(v.lookup("/short.txt", f, err));
    EXPECT_EQ(f.entry.name, std::string("short.txt"));
    EXPECT_EQ(f.entry.short_name, std::string("short.txt"));
    EXPECT_EQ(f.entry.size, std::uint32_t(fx.short_txt.size()));
    EXPECT_FALSE(f.entry.is_directory);
    EXPECT_EQ(f.fragments.size(), std::size_t(1));
    if (!f.fragments.empty()) {
        EXPECT_EQ(f.fragments[0].sector, fx.img.cluster_lba(4));
        EXPECT_EQ(f.fragments[0].sector_count, 3 * fx.bpc());
    }
    Bytes data;
    EXPECT_TRUE(ReadAll(v, f, data));
    EXPECT_TRUE(data == fx.short_txt);
    EXPECT_TRUE(v.lookup("SHORT.TXT", f, err));
    EXPECT_TRUE(v.lookup("\\Short.Txt", f, err));

    EXPECT_TRUE(v.lookup("/riivolution/My Long Fragmented Mod.xml", f, err));
    EXPECT_EQ(f.entry.name, std::string("My Long Fragmented Mod.xml"));
    EXPECT_EQ(f.entry.short_name, std::string("MYLONG~1.XML"));
    EXPECT_EQ(f.entry.size, std::uint32_t(fx.frag_xml.size()));
    EXPECT_EQ(f.fragments.size(), std::size_t(4));
    if (f.fragments.size() == 4) {
        EXPECT_EQ(f.fragments[0].sector, fx.img.cluster_lba(8));
        EXPECT_EQ(f.fragments[0].sector_count, fx.bpc());
        EXPECT_EQ(f.fragments[1].sector, fx.img.cluster_lba(10));
        EXPECT_EQ(f.fragments[2].sector, fx.img.cluster_lba(12));
        EXPECT_EQ(f.fragments[2].sector_count, 3 * fx.bpc());
        EXPECT_EQ(f.fragments[3].sector, fx.img.cluster_lba(20));
        EXPECT_EQ(f.fragments[3].sector_count, fx.bpc());
    }
    EXPECT_TRUE(ReadAll(v, f, data));
    EXPECT_TRUE(data == fx.frag_xml);
    // Straddling reads and the end of the file.
    const std::uint32_t cb = fx.img.cluster_bytes();
    Bytes part(64);
    EXPECT_TRUE(v.read(f, cb - 32, part.data(), part.size()));
    EXPECT_TRUE(std::equal(part.begin(), part.end(), fx.frag_xml.begin() + cb - 32));
    EXPECT_TRUE(v.read(f, 2 * cb - 32, part.data(), part.size()));
    EXPECT_TRUE(std::equal(part.begin(), part.end(), fx.frag_xml.begin() + 2 * cb - 32));
    EXPECT_TRUE(v.read(f, fx.frag_xml.size() - 64, part.data(), part.size()));
    EXPECT_TRUE(std::equal(part.begin(), part.end(), fx.frag_xml.end() - 64));
    EXPECT_FALSE(v.read(f, fx.frag_xml.size() - 63, part.data(), part.size()));
    EXPECT_TRUE(v.read(f, fx.frag_xml.size(), part.data(), 0));
    // Alternate spellings of the same path.
    EXPECT_TRUE(v.lookup("/RIIVOLUTION/my long fragmented mod.xml", f, err));
    EXPECT_TRUE(v.lookup("/RIIVOL~1/MYLONG~1.XML", f, err));
    EXPECT_TRUE(v.lookup("//riivolution/./My Long Fragmented Mod.xml", f, err));

    EXPECT_TRUE(v.lookup("/riivolution/nested dir/deep.bin", f, err));
    EXPECT_EQ(f.entry.size, std::uint32_t(3));
    EXPECT_TRUE(ReadAll(v, f, data));
    EXPECT_TRUE(data == fx.deep_bin);
    EXPECT_TRUE(v.lookup("/riivolution/abcdefghijklmnopqrstuvwxyz0123.dat", f, err));
    EXPECT_EQ(f.entry.name, std::string("abcdefghijklmnopqrstuvwxyz0123.dat"));
    EXPECT_TRUE(v.lookup("/riivolution/\xC3\x9Cn\xC3\xAF" "code \xC3\xB1" "ame.txt", f, err));
    EXPECT_EQ(f.entry.name, std::string("\xC3\x9Cn\xC3\xAF" "code \xC3\xB1" "ame.txt"));
    EXPECT_EQ(f.entry.short_name, std::string("NICODE~1.TXT"));
    EXPECT_TRUE(v.lookup("/riivolution/nicode~1.txt", f, err));
    // Directories resolve too, with their chain.
    EXPECT_TRUE(v.lookup("/riivolution", f, err));
    EXPECT_TRUE(f.entry.is_directory);
    EXPECT_EQ(f.entry.size, std::uint32_t(0));
    EXPECT_EQ(f.fragments.size(), std::size_t(2));
}

static void test_listing_and_edge_entries() {
    Fixture fx(512, 1, 0);
    riftwii::Fat32Volume v;
    std::string err;
    EXPECT_TRUE(riftwii::Fat32Volume::mount(fx.img.reader(), v, err));
    std::vector<riftwii::Fat32Entry> entries;
    EXPECT_TRUE(v.list("/", entries, err));
    EXPECT_EQ(entries.size(), std::size_t(9));
    if (entries.size() == 9) {
        EXPECT_EQ(entries[0].name, std::string("riivolution"));
        EXPECT_TRUE(entries[0].is_directory);
        EXPECT_EQ(entries[1].name, std::string("short.txt"));
        EXPECT_EQ(entries[2].name, std::string("ORPHAN.TXT"));  // its long name failed the checksum
        EXPECT_EQ(entries[8].name, std::string("EMPTY.BIN"));
    }
    EXPECT_TRUE(v.list("/riivolution/", entries, err));
    EXPECT_EQ(entries.size(), std::size_t(8));
    if (entries.size() == 8) {
        EXPECT_EQ(entries[0].name, std::string("My Long Fragmented Mod.xml"));
        EXPECT_EQ(entries[7].name, std::string("FILL3.BIN"));  // lives in the directory's second cluster
    }
    EXPECT_TRUE(v.list("/riivolution/nested dir", entries, err));
    EXPECT_EQ(entries.size(), std::size_t(1));

    riftwii::Fat32File f;
    EXPECT_FALSE(v.lookup("/deleted.txt", f, err));
    EXPECT_CONTAINS(err, "no such file");
    EXPECT_FALSE(v.lookup("/orphan long name.txt", f, err));
    EXPECT_TRUE(v.lookup("/orphan.txt", f, err));
    EXPECT_EQ(f.entry.size, std::uint32_t(10));
    EXPECT_TRUE(v.lookup("/empty.bin", f, err));
    EXPECT_EQ(f.entry.size, std::uint32_t(0));
    EXPECT_TRUE(f.fragments.empty());
    Bytes none;
    EXPECT_TRUE(v.read(f, 0, none.data(), 0));
    EXPECT_FALSE(v.lookup("/loop.bin", f, err));
    EXPECT_CONTAINS(err, "loops");
    EXPECT_FALSE(v.lookup("/bad.bin", f, err));
    EXPECT_CONTAINS(err, "bad cluster");
    EXPECT_FALSE(v.lookup("/free.bin", f, err));
    EXPECT_CONTAINS(err, "free cluster");
    EXPECT_FALSE(v.lookup("/range.bin", f, err));
    EXPECT_CONTAINS(err, "leaves the volume");
    EXPECT_FALSE(v.lookup("/oversize.bin", f, err));
    EXPECT_CONTAINS(err, "larger than its cluster chain");
    EXPECT_FALSE(v.lookup("/", f, err));
    EXPECT_CONTAINS(err, "no file name");
    EXPECT_FALSE(v.lookup("/../short.txt", f, err));
    EXPECT_CONTAINS(err, "'..'");
    EXPECT_FALSE(v.lookup("/short.txt/x", f, err));
    EXPECT_CONTAINS(err, "not a directory");
    EXPECT_FALSE(v.lookup("/missing/x", f, err));
    EXPECT_CONTAINS(err, "no such directory");
    EXPECT_FALSE(v.list("/short.txt", entries, err));
    EXPECT_FALSE(v.lookup("/riivolution/fill0.bin/x", f, err));

    // Volume label never resolves as a file.
    EXPECT_FALSE(v.lookup("/RIFTWII", f, err));

    // Limits.
    riftwii::Fat32Limits tight;
    tight.max_depth = 2;
    riftwii::Fat32Volume lv;
    EXPECT_TRUE(riftwii::Fat32Volume::mount(fx.img.reader(), lv, err, tight));
    EXPECT_FALSE(lv.lookup("/riivolution/nested dir/deep.bin", f, err));
    EXPECT_CONTAINS(err, "too deep");
    EXPECT_TRUE(lv.lookup("/riivolution/fill0.bin", f, err));
    tight.max_depth = 64;
    tight.max_directory_entries = 4;
    EXPECT_TRUE(riftwii::Fat32Volume::mount(fx.img.reader(), lv, err, tight));
    EXPECT_FALSE(lv.lookup("/empty.bin", f, err));
    EXPECT_CONTAINS(err, "too many entries");

    // A directory chain that loops is caught too.
    Fixture broken(512, 1, 0);
    broken.img.set_fat(3, 3);  // the first (full) cluster points at itself
    EXPECT_TRUE(riftwii::Fat32Volume::mount(broken.img.reader(), v, err));
    EXPECT_FALSE(v.lookup("/riivolution/nothing", f, err));
    EXPECT_CONTAINS(err, "loops");
}

static void test_fast_cycle_detection_and_fat_cache() {
    Fixture fx(512, 1, 0);
    // Advertise a large valid geometry without allocating a matching data
    // area. chain() needs only the first FAT block for these short cycles;
    // the old volume-sized guard would have repeated that read one million
    // times before failing.
    put32(fx.img.boot() + 0x24, 10000);
    put32(fx.img.boot() + 0x20, 1020004);
    fx.img.set_fat(30, 30);
    int reads = 0;
    auto counted = [&fx, &reads](std::uint64_t lba, std::uint32_t count, std::uint8_t* out) {
        ++reads;
        return fx.img.reader()(lba, count, out);
    };
    riftwii::Fat32Volume v;
    std::string err;
    std::vector<Fragment> fragments;
    EXPECT_TRUE(riftwii::Fat32Volume::mount(counted, v, err));
    EXPECT_TRUE(v.geometry().cluster_count >= 1000000);
    reads = 0;
    EXPECT_FALSE(v.chain(30, fragments, err));
    EXPECT_CONTAINS(err, "loops");
    EXPECT_TRUE(reads <= 2);  // one cached FAT block services a self-cycle

    // Remount invalidates the FAT cache; a two-cluster cycle is still bounded.
    fx.img.set_fat(30, 31);
    fx.img.set_fat(31, 30);
    EXPECT_TRUE(riftwii::Fat32Volume::mount(counted, v, err));
    reads = 0;
    EXPECT_FALSE(v.chain(30, fragments, err));
    EXPECT_CONTAINS(err, "loops");
    EXPECT_TRUE(reads <= 2);

    // A normal contiguous run stays intact and uses that same one FAT block.
    fx.img.chain({40, 41, 42});
    EXPECT_TRUE(riftwii::Fat32Volume::mount(counted, v, err));
    reads = 0;
    EXPECT_TRUE(v.chain(40, fragments, err));
    EXPECT_EQ(fragments.size(), std::size_t(1));
    EXPECT_TRUE(reads <= 2);
}

// Folders are read once and then served from memory (a big mod looks up
// thousands of files under the same folders); forget_cached() drops them
// after the card was written behind the volume's back.
static void test_directory_cache() {
    Fixture fx(512, 1, 0);
    int reads = 0;
    auto counted = [&fx, &reads](std::uint64_t lba, std::uint32_t count, std::uint8_t* out) {
        ++reads;
        return fx.img.reader()(lba, count, out);
    };
    riftwii::Fat32Volume v;
    std::string err;
    EXPECT_TRUE(riftwii::Fat32Volume::mount(counted, v, err));
    riftwii::Fat32File f;
    bool missing = true;
    EXPECT_TRUE(v.lookup("/riivolution/fill0.bin", f, missing, err));
    EXPECT_FALSE(missing);
    reads = 0;
    EXPECT_TRUE(v.lookup("/RIIVOLUTION/FILL3.BIN", f, err));
    EXPECT_TRUE(v.lookup("/riivolution/nested dir", f, err));
    std::vector<riftwii::Fat32Entry> entries;
    EXPECT_TRUE(v.list("/riivolution", entries, err));
    EXPECT_EQ(entries.size(), std::size_t(8));
    EXPECT_EQ(reads, 0);

    // Missing, as opposed to unreadable.
    EXPECT_FALSE(v.lookup("/riivolution/new.bin", f, missing, err));
    EXPECT_TRUE(missing);
    EXPECT_FALSE(v.lookup("/missing/x", f, missing, err));
    EXPECT_TRUE(missing);
    EXPECT_FALSE(v.lookup("/short.txt/x", f, missing, err));
    EXPECT_TRUE(missing);
    EXPECT_FALSE(v.list("/nowhere", entries, missing, err));
    EXPECT_TRUE(missing);
    EXPECT_FALSE(v.lookup("/loop.bin", f, missing, err));
    EXPECT_FALSE(missing);

    // A file added behind the volume's back shows up only once forgotten.
    Bytes riiv;
    cat(riiv, short_entry(".          ", 0x10, 3, 0));
    cat(riiv, short_entry("..         ", 0x10, 0, 0));
    cat(riiv, short_entry("NEW     BIN", 0x20, 0, 0));
    EXPECT_TRUE(fx.img.write_dir({3, 70}, riiv));
    EXPECT_FALSE(v.lookup("/riivolution/new.bin", f, missing, err));
    EXPECT_TRUE(missing);
    v.forget_cached();
    EXPECT_TRUE(v.lookup("/riivolution/new.bin", f, missing, err));
    EXPECT_FALSE(v.lookup("/riivolution/fill0.bin", f, missing, err));
    EXPECT_TRUE(missing);
}

static void test_mount_failures() {
    std::string err;
    riftwii::Fat32Volume v;
    {
        Fixture fx(512, 1, 0);
        fx.img.boot()[0x1FE] = 0;
        EXPECT_FALSE(riftwii::Fat32Volume::mount(fx.img.reader(), v, err));
        EXPECT_CONTAINS(err, "neither");
    }
    {
        Fixture fx(512, 1, 0);
        put16(fx.img.boot() + 0x16, 9);  // FAT16-style size field
        EXPECT_FALSE(riftwii::Fat32Volume::mount(fx.img.reader(), v, err));
        EXPECT_CONTAINS(err, "not a FAT32");
    }
    {
        Fixture fx(512, 1, 0);
        fx.img.boot()[0x0D] = 3;  // not a power of two
        EXPECT_FALSE(riftwii::Fat32Volume::mount(fx.img.reader(), v, err));
        EXPECT_CONTAINS(err, "sectors per cluster");
    }
    {
        Fixture fx(512, 1, 0);
        put32(fx.img.boot() + 0x2C, 1000);  // root beyond the volume
        EXPECT_FALSE(riftwii::Fat32Volume::mount(fx.img.reader(), v, err));
        EXPECT_CONTAINS(err, "root cluster");
    }
    {
        Fixture fx(512, 1, 0);
        put16(fx.img.boot() + 0x28, 0x0085);  // active FAT 5 of 2
        EXPECT_FALSE(riftwii::Fat32Volume::mount(fx.img.reader(), v, err));
        EXPECT_CONTAINS(err, "active FAT");
        put16(fx.img.boot() + 0x28, 0x0081);  // active FAT 1: still readable (mirrored copy)
        EXPECT_TRUE(riftwii::Fat32Volume::mount(fx.img.reader(), v, err));
        EXPECT_EQ(v.geometry().active_fat, std::uint32_t(1));
        riftwii::Fat32File f;
        EXPECT_TRUE(v.lookup("/short.txt", f, err));
        EXPECT_EQ(f.fragments.size(), std::size_t(1));
    }
    {
        // MBR whose partition does not hold a FAT32 volume.
        Fixture fx(512, 1, 2048);
        fx.img.boot()[0x1FE] = 0;
        EXPECT_FALSE(riftwii::Fat32Volume::mount(fx.img.reader(), v, err));
        EXPECT_CONTAINS(err, "no FAT32 partition");
        // mount_at still works when told where the boot sector is.
        fx.img.boot()[0x1FE] = 0x55;
        EXPECT_TRUE(riftwii::Fat32Volume::mount_at(fx.img.reader(), 2048, v, err));
    }
    {
        // Reader failures surface as errors rather than garbage.
        Fixture fx(512, 1, 0);
        riftwii::BlockReader flaky = [&fx](std::uint64_t lba, std::uint32_t count, std::uint8_t* out) {
            if (lba >= fx.img.cluster_lba(3)) return false;  // everything past the root dir fails
            return fx.img.reader()(lba, count, out);
        };
        EXPECT_TRUE(riftwii::Fat32Volume::mount(flaky, v, err));
        riftwii::Fat32File f;
        EXPECT_FALSE(v.lookup("/riivolution/fill0.bin", f, err));
        EXPECT_CONTAINS(err, "cannot read");
        EXPECT_TRUE(v.lookup("/short.txt", f, err));
        Bytes data;
        EXPECT_FALSE(ReadAll(v, f, data));
    }
    EXPECT_FALSE(riftwii::Fat32Volume::mount(nullptr, v, err));
    {
        riftwii::Fat32Volume unmounted;
        riftwii::Fat32File f;
        std::vector<riftwii::Fat32Entry> entries;
        std::vector<Fragment> frags;
        EXPECT_FALSE(unmounted.lookup("/x", f, err));
        EXPECT_CONTAINS(err, "not mounted");
        EXPECT_FALSE(unmounted.list("/", entries, err));
        EXPECT_FALSE(unmounted.chain(2, frags, err));
        EXPECT_FALSE(unmounted.read(f, 0, nullptr, 0));
    }
}

int main() {
    test_geometry_and_lookup(512, 1, 0);
    test_geometry_and_lookup(512, 8, 0);
    test_geometry_and_lookup(1024, 4, 2048);
    test_geometry_and_lookup(4096, 1, 63);
    test_listing_and_edge_entries();
    test_fast_cycle_detection_and_fat_cache();
    test_directory_cache();
    test_mount_failures();
    if (g_failures == 0) {
        std::cout << "ALL FAT32 TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
