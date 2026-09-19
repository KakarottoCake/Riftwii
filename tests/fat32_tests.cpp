// SPDX-License-Identifier: GPL-3.0-or-later
// FAT32 resolver against a FAT32 image built in memory: boot sector, two
// FATs, a root directory with a long-named subdirectory, one contiguous and
// one deliberately fragmented file, broken chains of every kind, and the
// same volume behind an MBR with a bigger sector/cluster geometry.
#include "riftwii/fat32.hpp"

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

namespace {

void put16(std::uint8_t* p, std::uint32_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
void put32(std::uint8_t* p, std::uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }

std::uint8_t checksum11(const char* n) {
    std::uint8_t sum = 0;
    for (int i = 0; i < 11; ++i) sum = static_cast<std::uint8_t>(((sum & 1) ? 0x80 : 0) + (sum >> 1) + static_cast<std::uint8_t>(n[i]));
    return sum;
}

Bytes short_entry(const char* name11, std::uint8_t attr, std::uint32_t cluster, std::uint32_t size, std::uint8_t nt_flags = 0) {
    Bytes e(32, 0);
    std::memcpy(e.data(), name11, 11);
    e[11] = attr;
    e[12] = nt_flags;
    put16(e.data() + 0x14, cluster >> 16);
    put16(e.data() + 0x1A, cluster & 0xFFFF);
    put32(e.data() + 0x1C, size);
    return e;
}

// Long name entries (last part first) followed by the short entry.
Bytes lfn_entries(const std::vector<std::uint16_t>& name, const char* short11, std::uint8_t attr, std::uint32_t cluster,
                  std::uint32_t size, bool wrong_checksum = false) {
    const std::uint8_t sum = static_cast<std::uint8_t>(checksum11(short11) ^ (wrong_checksum ? 0xFF : 0));
    const std::size_t parts = (name.size() + 12) / 13;
    Bytes out;
    for (std::size_t p = parts; p >= 1; --p) {
        Bytes e(32, 0);
        e[0] = static_cast<std::uint8_t>(p | (p == parts ? 0x40 : 0));
        e[11] = 0x0F;
        e[13] = sum;
        std::uint16_t chars[13];
        for (int i = 0; i < 13; ++i) {
            const std::size_t idx = (p - 1) * 13 + i;
            chars[i] = idx < name.size() ? name[idx] : (idx == name.size() ? 0x0000 : 0xFFFF);
        }
        for (int i = 0; i < 5; ++i) put16(e.data() + 1 + i * 2, chars[i]);
        for (int i = 0; i < 6; ++i) put16(e.data() + 14 + i * 2, chars[5 + i]);
        for (int i = 0; i < 2; ++i) put16(e.data() + 28 + i * 2, chars[11 + i]);
        out.insert(out.end(), e.begin(), e.end());
    }
    Bytes s = short_entry(short11, attr, cluster, size);
    out.insert(out.end(), s.begin(), s.end());
    return out;
}

std::vector<std::uint16_t> ucs(const std::u16string& s) { return std::vector<std::uint16_t>(s.begin(), s.end()); }

struct Image {
    std::uint32_t bps, spc, reserved = 4, fats = 2, clusters = 256, fat_sectors, total_sectors;
    std::uint64_t volume_lba;
    Bytes bytes;

    Image(std::uint32_t bytes_per_sector, std::uint32_t sectors_per_cluster, std::uint64_t at_lba)
        : bps(bytes_per_sector), spc(sectors_per_cluster), volume_lba(at_lba) {
        fat_sectors = ((clusters + 2) * 4 + bps - 1) / bps;
        total_sectors = reserved + fats * fat_sectors + clusters * spc;
        bytes.assign(static_cast<std::size_t>(volume_lba * 512 + std::uint64_t(total_sectors) * bps), 0);
        std::uint8_t* b = boot();
        b[0] = 0xEB; b[1] = 0x58; b[2] = 0x90;
        std::memcpy(b + 3, "MSWIN4.1", 8);
        put16(b + 0x0B, bps);
        b[0x0D] = static_cast<std::uint8_t>(spc);
        put16(b + 0x0E, reserved);
        b[0x10] = static_cast<std::uint8_t>(fats);
        b[0x15] = 0xF8;
        put32(b + 0x20, total_sectors);
        put32(b + 0x24, fat_sectors);
        put32(b + 0x2C, 2);
        put16(b + 0x30, 1);
        put16(b + 0x32, 6);
        std::memcpy(b + 0x52, "FAT32   ", 8);
        b[0x1FE] = 0x55; b[0x1FF] = 0xAA;
        // FAT[0] media, FAT[1] EOC, FAT[2] root EOC.
        set_fat(0, 0x0FFFFFF8); set_fat(1, 0x0FFFFFFF); set_fat(2, 0x0FFFFFFF);
        if (volume_lba != 0) {
            // MBR: one FAT32 LBA partition. The boot sector signature also
            // lives here so the mounter looks at the partition table.
            std::uint8_t* m = bytes.data();
            m[0x1FE] = 0x55; m[0x1FF] = 0xAA;
            std::uint8_t* pe = m + 0x1BE;
            pe[4] = 0x0C;
            put32(pe + 8, static_cast<std::uint32_t>(volume_lba));
            put32(pe + 12, total_sectors * (bps / 512));
        }
    }
    std::uint8_t* boot() { return bytes.data() + volume_lba * 512; }
    std::uint64_t data_start_sector() const { return reserved + std::uint64_t(fats) * fat_sectors; }
    std::uint64_t cluster_lba(std::uint32_t c) const { return volume_lba + (data_start_sector() + std::uint64_t(c - 2) * spc) * (bps / 512); }
    std::uint8_t* cluster(std::uint32_t c) { return bytes.data() + cluster_lba(c) * 512; }
    std::uint32_t cluster_bytes() const { return bps * spc; }
    void set_fat(std::uint32_t c, std::uint32_t v) {
        for (std::uint32_t f = 0; f < fats; ++f) {
            put32(boot() + (std::uint64_t(reserved) + std::uint64_t(f) * fat_sectors) * bps + std::uint64_t(c) * 4, v);
        }
    }
    void chain(const std::vector<std::uint32_t>& cs) {
        for (std::size_t i = 0; i < cs.size(); ++i) set_fat(cs[i], i + 1 < cs.size() ? cs[i + 1] : 0x0FFFFFFF);
    }
    void write_data(const std::vector<std::uint32_t>& cs, const Bytes& data) {
        chain(cs);
        std::size_t pos = 0;
        for (std::uint32_t c : cs) {
            const std::size_t n = std::min<std::size_t>(cluster_bytes(), data.size() - pos);
            std::memcpy(cluster(c), data.data() + pos, n);
            pos += n;
            if (pos >= data.size()) break;
        }
    }
    void write_dir(const std::vector<std::uint32_t>& cs, const Bytes& entries) {
        chain(cs);
        std::size_t pos = 0;
        for (std::uint32_t c : cs) {
            std::memset(cluster(c), 0, cluster_bytes());
            const std::size_t n = std::min<std::size_t>(cluster_bytes(), entries.size() - pos);
            std::memcpy(cluster(c), entries.data() + pos, n);
            pos += n;
        }
        if (pos < entries.size()) { std::cerr << "test image: directory does not fit" << std::endl; g_failures++; }
    }
    riftwii::BlockReader reader() const {
        return [this](std::uint64_t lba, std::uint32_t count, std::uint8_t* out) {
            const std::uint64_t end = (lba + count) * 512;
            if (end > bytes.size()) return false;
            std::memcpy(out, bytes.data() + lba * 512, count * 512);
            return true;
        };
    }
};

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
        img.write_dir({2}, root);

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
        img.write_dir({3, 70}, riiv);  // spills into a second cluster at the smallest geometry

        Bytes nested;
        cat(nested, short_entry(".          ", 0x10, 60, 0));
        cat(nested, short_entry("..         ", 0x10, 3, 0));
        cat(nested, lfn_entries(ucs(u"deep.bin"), "DEEP    BIN", 0x20, 61, 3));
        img.write_dir({60}, nested);
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
}

int main() {
    test_geometry_and_lookup(512, 1, 0);
    test_geometry_and_lookup(512, 8, 0);
    test_geometry_and_lookup(1024, 4, 2048);
    test_geometry_and_lookup(4096, 1, 63);
    test_listing_and_edge_entries();
    test_mount_failures();
    if (g_failures == 0) {
        std::cout << "ALL FAT32 TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
