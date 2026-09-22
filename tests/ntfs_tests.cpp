// SPDX-License-Identifier: GPL-3.0-or-later
// NTFS walker against an NTFS volume built in memory: boot sector, an MFT
// split over two runs, a root directory whose index spills into INDX
// blocks, DOS-name aliases and metadata entries to hide, a file whose data
// runs jump backwards, a file whose $DATA is spread over two records by an
// $ATTRIBUTE_LIST, and the files d2x cannot read (compressed, sparse,
// resident). The same volume is then found behind an MBR and a GPT, and a
// FAT32 volume through the same entry point.
#include "riftwii/ntfs.hpp"

#include "fat32_image.hpp"

#include <algorithm>
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
using riftwii::BlockReader;
using riftwii::Fragment;
using riftwii::ImageVolume;
using riftwii::NtfsRun;
using riftwii::NtfsVolume;
using riftwii::VolumeEntry;
using riftwii::VolumeFile;

namespace {

constexpr std::uint32_t kCluster = 4096;
constexpr std::uint32_t kSpc = 8;
constexpr std::uint32_t kRecord = 1024;
constexpr std::uint64_t kClusters = 1024;
constexpr std::uint64_t kMftA = 4;   // records 0..15
constexpr std::uint64_t kMftB = 20;  // records 16..31
constexpr std::uint64_t kRootIndx = 30;

void put16(std::uint8_t* p, std::uint64_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
void put32(std::uint8_t* p, std::uint64_t v) { put16(p, v & 0xFFFF); put16(p + 2, (v >> 16) & 0xFFFF); }
void put64(std::uint8_t* p, std::uint64_t v) { put32(p, v & 0xFFFFFFFF); put32(p + 4, v >> 32); }
std::size_t align8(std::size_t n) { return (n + 7) & ~std::size_t(7); }

Bytes pattern(std::size_t n, std::uint8_t seed) {
    Bytes b(n);
    for (std::size_t i = 0; i < n; ++i) b[i] = static_cast<std::uint8_t>(seed + i * 7 + (i >> 9));
    return b;
}

// Protects a multi-sector block the way NTFS does: the last two bytes of
// every 512-byte stride move into the update sequence array.
void protect(std::uint8_t* b, std::size_t size, std::size_t usa_ofs) {
    const std::size_t count = size / 512 + 1;
    put16(b + 4, usa_ofs);
    put16(b + 6, count);
    put16(b + usa_ofs, 0x0007);
    for (std::size_t i = 1; i < count; ++i) {
        std::uint8_t* tail = b + i * 512 - 2;
        b[usa_ofs + 2 * i] = tail[0];
        b[usa_ofs + 2 * i + 1] = tail[1];
        put16(tail, 0x0007);
    }
}

// Mapping pairs for (lcn, length) runs; lcn < 0 marks a sparse run.
Bytes runs(const std::vector<std::pair<std::int64_t, std::uint64_t>>& rs) {
    Bytes out;
    std::int64_t prev = 0;
    for (const auto& r : rs) {
        Bytes len, off;
        for (std::uint64_t v = r.second; v != 0 || len.empty(); v >>= 8) len.push_back(v & 0xFF);
        if (len.back() & 0x80) len.push_back(0);
        if (r.first >= 0) {
            std::int64_t d = r.first - prev;
            prev = r.first;
            for (;;) {
                off.push_back(static_cast<std::uint8_t>(d & 0xFF));
                const std::int64_t rest = d >> 8;  // arithmetic shift
                const bool sign = (off.back() & 0x80) != 0;
                if ((rest == 0 && !sign) || (rest == -1 && sign)) break;
                d = rest;
            }
        }
        out.push_back(static_cast<std::uint8_t>(len.size() | (off.size() << 4)));
        out.insert(out.end(), len.begin(), len.end());
        out.insert(out.end(), off.begin(), off.end());
    }
    out.push_back(0);
    return out;
}

struct Record {
    Bytes b = Bytes(kRecord, 0);
    std::size_t pos = 0x38;
    Record(std::uint16_t flags, std::uint64_t base = 0) {
        std::memcpy(b.data(), "FILE", 4);
        put16(&b[0x10], 1);
        put16(&b[0x12], 1);
        put16(&b[0x14], 0x38);
        put16(&b[0x16], flags);
        put32(&b[0x1C], kRecord);
        put64(&b[0x20], base);
    }
    std::uint8_t* header(std::uint32_t type, std::size_t len, bool non_resident, const std::u16string& name,
                         std::size_t name_off) {
        std::uint8_t* a = &b[pos];
        put32(a, type);
        put32(a + 4, len);
        a[8] = non_resident ? 1 : 0;
        a[9] = static_cast<std::uint8_t>(name.size());
        put16(a + 0x0A, name_off);
        for (std::size_t i = 0; i < name.size(); ++i) put16(a + name_off + 2 * i, name[i]);
        pos += len;
        return a;
    }
    void resident(std::uint32_t type, const Bytes& value, const std::u16string& name = u"") {
        const std::size_t voff = align8(0x18 + 2 * name.size());
        std::uint8_t* a = header(type, align8(voff + value.size()), false, name, 0x18);
        put32(a + 0x10, value.size());
        put16(a + 0x14, voff);
        std::memcpy(a + voff, value.data(), value.size());
    }
    void non_resident(std::uint32_t type, const Bytes& pairs, std::uint64_t lowest, std::uint64_t highest,
                      std::uint64_t size, std::uint16_t flags = 0, const std::u16string& name = u"") {
        const std::size_t mp = align8(0x40 + 2 * name.size());
        std::uint8_t* a = header(type, align8(mp + pairs.size()), true, name, 0x40);
        put16(a + 0x0C, flags);
        put64(a + 0x10, lowest);
        put64(a + 0x18, highest);
        put16(a + 0x20, mp);
        put64(a + 0x28, (size + kCluster - 1) / kCluster * kCluster);
        put64(a + 0x30, size);
        put64(a + 0x38, size);
        std::memcpy(a + mp, pairs.data(), pairs.size());
    }
    Bytes finish() {
        put32(&b[pos], 0xFFFFFFFFu);
        put32(&b[0x18], pos + 8);
        protect(b.data(), b.size(), 0x30);
        return b;
    }
};

struct Entry {
    std::uint64_t ref;
    std::string name;
    bool dir;
    std::uint64_t size;
    std::uint8_t ns;  // 1 Win32, 2 DOS
    std::int64_t sub;  // child node VCN, or -1
    bool last;
    Entry(std::uint64_t r, std::string n, bool d = false, std::uint64_t s = 0, std::uint8_t space = 1,
          std::int64_t child = -1, bool is_last = false)
        : ref(r), name(std::move(n)), dir(d), size(s), ns(space), sub(child), last(is_last) {}
};

Bytes index_entries(const std::vector<Entry>& es) {
    Bytes out;
    for (const Entry& e : es) {
        const std::size_t klen = e.last ? 0 : 0x42 + 2 * e.name.size();
        std::size_t len = align8(0x10 + klen) + (e.sub >= 0 ? 8 : 0);
        Bytes x(len, 0);
        put64(&x[0], e.ref | (std::uint64_t(1) << 48));
        put16(&x[8], len);
        put16(&x[0x0A], klen);
        put16(&x[0x0C], (e.sub >= 0 ? 1 : 0) | (e.last ? 2 : 0));
        if (!e.last) {
            std::uint8_t* k = &x[0x10];
            put64(k, 5);
            put64(k + 0x30, e.size);
            put32(k + 0x38, e.dir ? 0x10000000u : 0x20u);
            k[0x40] = static_cast<std::uint8_t>(e.name.size());
            k[0x41] = e.ns;
            for (std::size_t i = 0; i < e.name.size(); ++i) put16(k + 0x42 + 2 * i, static_cast<unsigned char>(e.name[i]));
        }
        if (e.sub >= 0) put64(&x[len - 8], static_cast<std::uint64_t>(e.sub));
        out.insert(out.end(), x.begin(), x.end());
    }
    return out;
}

Bytes index_root(const std::vector<Entry>& es, bool large) {
    const Bytes body = index_entries(es);
    Bytes v(0x20 + body.size(), 0);
    put32(&v[0], 0x30);
    put32(&v[4], 1);
    put32(&v[8], kCluster);
    v[0x0C] = 1;
    put32(&v[0x10], 0x10);
    put32(&v[0x14], 0x10 + body.size());
    put32(&v[0x18], 0x10 + body.size());
    v[0x1C] = large ? 1 : 0;
    std::memcpy(&v[0x20], body.data(), body.size());
    return v;
}

Bytes indx_block(std::uint64_t vcn, const std::vector<Entry>& es) {
    const Bytes body = index_entries(es);
    Bytes b(kCluster, 0);
    std::memcpy(b.data(), "INDX", 4);
    put64(&b[0x10], vcn);
    put32(&b[0x18], 0x28);
    put32(&b[0x1C], 0x28 + body.size());
    put32(&b[0x20], kCluster - 0x18);
    std::memcpy(&b[0x40], body.data(), body.size());
    protect(b.data(), b.size(), 0x28);
    return b;
}

struct Volume {
    std::uint64_t base;
    Bytes disk;
    Bytes big, mid, wbfs;
    static constexpr std::uint64_t kBigSize = 5 * kCluster - 300;

    explicit Volume(std::uint64_t base_lba) : base(base_lba) {
        disk.assign(static_cast<std::size_t>((base + kClusters * kSpc) * 512), 0);
        std::uint8_t* bs = at(0);
        bs[0] = 0xEB; bs[1] = 0x52; bs[2] = 0x90;
        std::memcpy(bs + 3, "NTFS    ", 8);
        put16(bs + 0x0B, 512);
        bs[0x0D] = kSpc;
        bs[0x15] = 0xF8;
        put64(bs + 0x28, kClusters * kSpc - 1);
        put64(bs + 0x30, kMftA);
        put64(bs + 0x38, 2);
        bs[0x40] = 0xF6;  // 2^10-byte records
        bs[0x44] = 1;     // one cluster per index block
        bs[510] = 0x55; bs[511] = 0xAA;

        Record mft(1);
        mft.non_resident(0x80, runs({{kMftA, 4}, {kMftB, 4}}), 0, 7, 32 * kRecord);
        record(0, mft.finish());

        // Root: its index root points below itself into two INDX blocks.
        Record root(3);
        root.resident(0x90, index_root({{19, "mid.bin", false, 1000, 1, 0}, {0, "", false, 0, 1, 1, true}}, true), u"$I30");
        root.non_resident(0xA0, runs({{kRootIndx, 2}}), 0, 1, 2 * kCluster, 0, u"$I30");
        record(5, root.finish());
        cluster(kRootIndx, indx_block(0, {{16, "GAMES~1", true, 0, 2}, {16, "games", true, 0, 1},
                                          {3, "$Secret", false, 0, 3}, {0, "", false, 0, 1, -1, true}}));
        cluster(kRootIndx + 1, indx_block(1, {{17, "wbfs", true}, {18, "zz.txt", false, 5}, {0, "", false, 0, 1, -1, true}}));

        Record games(3);
        games.resident(0x90, index_root({{20, "Big.iso", false, kBigSize}, {21, "comp.iso", false, kCluster},
                                         {22, "sparse.iso", false, 2 * kCluster}, {0, "", false, 0, 1, -1, true}}, false), u"$I30");
        record(16, games.finish());

        Record wbfs_dir(3);
        wbfs_dir.resident(0x90, index_root({{23, "Game [RABC01]", true}, {0, "", false, 0, 1, -1, true}}, false), u"$I30");
        record(17, wbfs_dir.finish());

        Record zz(1);
        zz.resident(0x80, {'h', 'e', 'l', 'l', 'o'});
        record(18, zz.finish());

        mid = pattern(1000, 0x33);
        Record m(1);
        m.non_resident(0x80, runs({{320, 1}}), 0, 0, mid.size());
        record(19, m.finish());
        write(320, mid);

        // Five clusters in three runs, the second jumping backwards.
        big = pattern(kBigSize, 0x51);
        Record b(1);
        b.non_resident(0x80, runs({{100, 2}, {50, 1}, {200, 2}}), 0, 4, big.size());
        record(20, b.finish());
        write(100, Bytes(big.begin(), big.begin() + 2 * kCluster));
        write(50, Bytes(big.begin() + 2 * kCluster, big.begin() + 3 * kCluster));
        write(200, Bytes(big.begin() + 3 * kCluster, big.end()));

        Record comp(1);
        comp.non_resident(0x80, runs({{300, 1}}), 0, 0, kCluster, 0x0001);
        record(21, comp.finish());
        Record sparse(1);
        sparse.non_resident(0x80, runs({{310, 1}, {-1, 1}}), 0, 1, 2 * kCluster);
        record(22, sparse.finish());

        Record game_dir(3);
        game_dir.resident(0x90, index_root({{24, "RABC01.wbfs", false, 4 * kCluster}, {0, "", false, 0, 1, -1, true}}, false), u"$I30");
        record(23, game_dir.finish());

        // $DATA split over records 24 (vcn 0-1) and 25 (vcn 2-3), tied
        // together by an attribute list in the base record.
        wbfs = pattern(4 * kCluster, 0x99);
        Bytes list;
        const auto list_entry = [&](std::uint64_t lowest, std::uint64_t rec) {
            Bytes e(0x20, 0);
            put32(&e[0], 0x80);
            put16(&e[4], 0x20);
            e[7] = 0x1A;
            put64(&e[8], lowest);
            put64(&e[0x10], rec | (std::uint64_t(1) << 48));
            list.insert(list.end(), e.begin(), e.end());
        };
        list_entry(0, 24);
        list_entry(2, 25);
        Record w(1);
        w.resident(0x20, list);
        w.non_resident(0x80, runs({{400, 2}}), 0, 1, wbfs.size());
        record(24, w.finish());
        Record ext(1, 24);
        ext.non_resident(0x80, runs({{500, 2}}), 2, 3, 0);
        record(25, ext.finish());
        write(400, Bytes(wbfs.begin(), wbfs.begin() + 2 * kCluster));
        write(500, Bytes(wbfs.begin() + 2 * kCluster, wbfs.end()));
    }

    std::uint8_t* at(std::uint64_t volume_byte) { return disk.data() + base * 512 + volume_byte; }
    void cluster(std::uint64_t lcn, const Bytes& data) { std::memcpy(at(lcn * kCluster), data.data(), data.size()); }
    void write(std::uint64_t lcn, const Bytes& data) { cluster(lcn, data); }
    void record(std::uint64_t n, const Bytes& data) {
        const std::uint64_t lcn = n < 16 ? kMftA : kMftB;
        std::memcpy(at(lcn * kCluster + (n % 16) * kRecord), data.data(), data.size());
    }
    std::uint64_t lba(std::uint64_t lcn) const { return base + lcn * kSpc; }

    BlockReader reader() const {
        return [this](std::uint64_t lba, std::uint32_t count, std::uint8_t* out) {
            if ((lba + count) * 512 > disk.size()) return false;
            std::memcpy(out, disk.data() + lba * 512, count * 512);
            return true;
        };
    }
};

std::vector<std::string> names(const std::vector<VolumeEntry>& es) {
    std::vector<std::string> out;
    for (const VolumeEntry& e : es) out.push_back(e.name + (e.is_directory ? "/" : ""));
    std::sort(out.begin(), out.end());
    return out;
}

std::string joined(const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& x : v) s += (s.empty() ? "" : ",") + x;
    return s;
}

void test_runs() {
    std::vector<NtfsRun> out;
    std::string error;
    const Bytes r = runs({{100, 2}, {50, 1}, {-1, 3}, {70000, 0x1234}});
    EXPECT_TRUE(riftwii::ntfs_decode_runs(r.data(), r.size(), 10, out, error));
    EXPECT_EQ(out.size(), 4u);
    if (out.size() == 4) {
        EXPECT_EQ(out[0].vcn, 10u); EXPECT_EQ(out[0].lcn, 100u); EXPECT_EQ(out[0].length, 2u);
        EXPECT_EQ(out[1].vcn, 12u); EXPECT_EQ(out[1].lcn, 50u);
        EXPECT_TRUE(out[2].sparse); EXPECT_EQ(out[2].length, 3u);
        EXPECT_EQ(out[3].vcn, 16u); EXPECT_EQ(out[3].lcn, 70000u); EXPECT_EQ(out[3].length, 0x1234u);
    }
    out.clear();
    const Bytes negative = {0x11, 0x01, 0x05, 0x11, 0x01, 0xF0, 0x00};  // 5, then 5 - 16
    EXPECT_FALSE(riftwii::ntfs_decode_runs(negative.data(), negative.size(), 0, out, error));
    EXPECT_CONTAINS(error, "outside");
    out.clear();
    const Bytes truncated = {0x21, 0x01};
    EXPECT_FALSE(riftwii::ntfs_decode_runs(truncated.data(), truncated.size(), 0, out, error));
    EXPECT_CONTAINS(error, "malformed");
    out.clear();
    const Bytes zero_length = {0x11, 0x00, 0x05, 0x00};
    EXPECT_FALSE(riftwii::ntfs_decode_runs(zero_length.data(), zero_length.size(), 0, out, error));
}

void test_walk(std::uint64_t base) {
    Volume v(base);
    NtfsVolume vol;
    std::string error;
    EXPECT_TRUE(NtfsVolume::mount_at(v.reader(), base, vol, error));
    EXPECT_EQ(error, "");
    EXPECT_EQ(vol.geometry().cluster_bytes, kCluster);
    EXPECT_EQ(vol.geometry().mft_record_bytes, kRecord);
    EXPECT_EQ(vol.geometry().mft_lcn, kMftA);

    // The index tree is walked through both INDX blocks; the DOS alias and
    // the metadata file stay hidden.
    std::vector<VolumeEntry> es;
    EXPECT_TRUE(vol.list("/", es, error));
    EXPECT_EQ(joined(names(es)), "games/,mid.bin,wbfs/,zz.txt");
    EXPECT_TRUE(vol.list("/GAMES", es, error));
    EXPECT_EQ(joined(names(es)), "Big.iso,comp.iso,sparse.iso");

    VolumeFile f;
    EXPECT_TRUE(vol.lookup("/games/big.ISO", f, error));
    EXPECT_EQ(f.entry.size, Volume::kBigSize);
    EXPECT_EQ(f.fragments.size(), 3u);
    if (f.fragments.size() == 3) {
        EXPECT_EQ(f.fragments[0].sector, v.lba(100)); EXPECT_EQ(f.fragments[0].sector_count, 2u * kSpc);
        EXPECT_EQ(f.fragments[1].sector, v.lba(50)); EXPECT_EQ(f.fragments[1].sector_count, 1u * kSpc);
        // Trimmed to the file's last 512-byte block.
        EXPECT_EQ(f.fragments[2].sector, v.lba(200));
        EXPECT_EQ(f.fragments[2].sector_count, (Volume::kBigSize + 511) / 512 - 3u * kSpc);
    }
    Bytes got(Volume::kBigSize);
    EXPECT_TRUE(vol.read(f, 0, got.data(), got.size()));
    EXPECT_TRUE(got == v.big);
    Bytes span(5000);
    EXPECT_TRUE(vol.read(f, 2 * kCluster - 1234, span.data(), span.size()));
    EXPECT_TRUE(std::equal(span.begin(), span.end(), v.big.begin() + (2 * kCluster - 1234)));
    EXPECT_FALSE(vol.read(f, Volume::kBigSize - 10, span.data(), 11));

    // $DATA assembled from two records through the attribute list.
    EXPECT_TRUE(vol.lookup("/wbfs/Game [RABC01]/RABC01.wbfs", f, error));
    EXPECT_EQ(error, "");
    EXPECT_EQ(f.entry.size, 4u * kCluster);
    EXPECT_EQ(f.fragments.size(), 2u);
    got.assign(f.entry.size, 0);
    EXPECT_TRUE(vol.read(f, 0, got.data(), got.size()));
    EXPECT_TRUE(got == v.wbfs);

    EXPECT_TRUE(vol.lookup("/mid.bin", f, error));
    got.assign(1000, 0);
    EXPECT_TRUE(vol.read(f, 0, got.data(), got.size()));
    EXPECT_TRUE(got == v.mid);
    EXPECT_EQ(f.fragments.size(), 1u);
    if (!f.fragments.empty()) EXPECT_EQ(f.fragments[0].sector_count, 2u);

    EXPECT_TRUE(vol.lookup("/games", f, error));
    EXPECT_TRUE(f.entry.is_directory);

    EXPECT_FALSE(vol.lookup("/games/comp.iso", f, error));
    EXPECT_CONTAINS(error, "compressed");
    EXPECT_FALSE(vol.lookup("/games/sparse.iso", f, error));
    EXPECT_CONTAINS(error, "sparse");
    EXPECT_FALSE(vol.lookup("/zz.txt", f, error));
    EXPECT_CONTAINS(error, "too small");
    EXPECT_FALSE(vol.lookup("/games/nope.iso", f, error));
    EXPECT_CONTAINS(error, "no such ");
    EXPECT_FALSE(vol.list("/nope", es, error));
    EXPECT_CONTAINS(error, "no such ");
    EXPECT_FALSE(vol.list("/mid.bin", es, error));
    EXPECT_FALSE(vol.lookup("/mid.bin/x", f, error));
}

void test_corruption() {
    std::string error;
    {
        // A torn record (sector written without its neighbour) is refused.
        Volume v(0);
        v.at(kMftB * kCluster + 0 * kRecord + 510)[0] ^= 0xFF;  // record 16's first tail
        NtfsVolume vol;
        EXPECT_TRUE(NtfsVolume::mount_at(v.reader(), 0, vol, error));
        std::vector<VolumeEntry> es;
        EXPECT_FALSE(vol.list("/games", es, error));
        EXPECT_CONTAINS(error, "torn");
    }
    {
        // Both root subnodes naming the same block would loop.
        Volume v(0);
        Record root(3);
        root.resident(0x90, index_root({{19, "mid.bin", false, 1000, 1, 0}, {0, "", false, 0, 1, 0, true}}, true), u"$I30");
        root.non_resident(0xA0, runs({{kRootIndx, 2}}), 0, 1, 2 * kCluster, 0, u"$I30");
        v.record(5, root.finish());
        NtfsVolume vol;
        EXPECT_TRUE(NtfsVolume::mount_at(v.reader(), 0, vol, error));
        std::vector<VolumeEntry> es;
        EXPECT_FALSE(vol.list("/", es, error));
        EXPECT_CONTAINS(error, "loops");
    }
    {
        // An INDX block pointing at itself is caught the same way.
        Volume v(0);
        v.cluster(kRootIndx + 1, indx_block(1, {{17, "wbfs", true, 0, 1, 1}, {0, "", false, 0, 1, -1, true}}));
        NtfsVolume vol;
        EXPECT_TRUE(NtfsVolume::mount_at(v.reader(), 0, vol, error));
        std::vector<VolumeEntry> es;
        EXPECT_FALSE(vol.list("/", es, error));
    }
    {
        Volume v(0);
        v.at(0)[0x0B] = 0x10;  // 16-byte sectors... not
        v.at(0)[0x0C] = 0x00;
        NtfsVolume vol;
        EXPECT_FALSE(NtfsVolume::mount_at(v.reader(), 0, vol, error));
        EXPECT_CONTAINS(error, "sector size");
    }
    {
        Volume v(0);
        put64(v.at(0x30), kClusters + 5);  // MFT past the end
        NtfsVolume vol;
        EXPECT_FALSE(NtfsVolume::mount_at(v.reader(), 0, vol, error));
    }
    {
        Volume v(0);
        v.record(5, Record(1).finish());  // root not a directory
        NtfsVolume vol;
        EXPECT_FALSE(NtfsVolume::mount_at(v.reader(), 0, vol, error));
        EXPECT_CONTAINS(error, "root");
    }
}

void test_partitions() {
    std::string error;
    {
        Volume v(2048);
        std::uint8_t* pe = v.disk.data() + 0x1BE;
        pe[4] = 0x07;
        put32(pe + 8, 2048);
        v.disk[510] = 0x55; v.disk[511] = 0xAA;
        std::unique_ptr<ImageVolume> vol;
        EXPECT_TRUE(riftwii::mount_image_volume(v.reader(), vol, error));
        EXPECT_TRUE(vol && std::string(vol->kind()) == "NTFS");
        VolumeFile f;
        if (vol) {
            EXPECT_TRUE(vol->lookup("/games/Big.iso", f, error));
            if (!f.fragments.empty()) EXPECT_EQ(f.fragments[0].sector, 2048u + 100u * kSpc);
            riftwii::VolumeFileSource src(*vol, f);
            Bytes head(64);
            EXPECT_EQ(src.size(), Volume::kBigSize);
            EXPECT_TRUE(src.read(0, head.data(), head.size()));
            EXPECT_TRUE(std::equal(head.begin(), head.end(), v.big.begin()));
        }
    }
    {
        Volume v(4096);
        std::uint8_t* pe = v.disk.data() + 0x1BE;
        pe[4] = 0xEE;
        put32(pe + 8, 1);
        v.disk[510] = 0x55; v.disk[511] = 0xAA;
        std::uint8_t* h = v.disk.data() + 512;
        std::memcpy(h, "EFI PART", 8);
        put64(h + 0x48, 2);
        put32(h + 0x50, 128);
        put32(h + 0x54, 128);
        std::uint8_t* e0 = v.disk.data() + 2 * 512;       // an unused slot first
        std::uint8_t* e1 = e0 + 128;
        put64(e0 + 0x20, 999);
        e1[0] = 0xA2; e1[1] = 0xA0;                        // any non-zero type GUID
        put64(e1 + 0x20, 4096);
        std::unique_ptr<ImageVolume> vol;
        EXPECT_TRUE(riftwii::mount_image_volume(v.reader(), vol, error));
        EXPECT_TRUE(vol && std::string(vol->kind()) == "NTFS");
    }
    {
        // FAT32 comes back through the same door.
        fatimg::Image img(512, 1, 0);
        img.write_dir({2}, fatimg::short_entry("HELLO   TXT", 0x20, 3, 5));
        img.write_data({3}, {'h', 'e', 'l', 'l', 'o'});
        std::unique_ptr<ImageVolume> vol;
        EXPECT_TRUE(riftwii::mount_image_volume(img.reader(), vol, error));
        EXPECT_TRUE(vol && std::string(vol->kind()) == "FAT32");
        if (vol) {
            std::vector<VolumeEntry> es;
            EXPECT_TRUE(vol->list("/", es, error));
            EXPECT_EQ(joined(names(es)), "HELLO.TXT");
            VolumeFile f;
            EXPECT_TRUE(vol->lookup("/hello.txt", f, error));
            char buf[5] = {};
            EXPECT_TRUE(vol->read(f, 0, reinterpret_cast<std::uint8_t*>(buf), 5));
            EXPECT_EQ(std::string(buf, 5), "hello");
        }
    }
    {
        Bytes blank(64 * 512, 0);
        const BlockReader r = [&](std::uint64_t lba, std::uint32_t count, std::uint8_t* out) {
            if ((lba + count) * 512 > blank.size()) return false;
            std::memcpy(out, blank.data() + lba * 512, count * 512);
            return true;
        };
        std::unique_ptr<ImageVolume> vol;
        EXPECT_FALSE(riftwii::mount_image_volume(r, vol, error));
        EXPECT_CONTAINS(error, "no FAT32 or NTFS");
    }
}

}  // namespace

int main() {
    test_runs();
    test_walk(0);
    test_walk(63);
    test_corruption();
    test_partitions();
    if (g_failures == 0) {
        std::cout << "ALL NTFS TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
