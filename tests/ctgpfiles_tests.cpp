// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// CTGP-R's open-file records and virtual handles (riftwii/ctgpfiles.hpp).
#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "riftwii/ctgpfiles.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

template <typename Bytes>
static std::uint32_t get32(const Bytes& v, std::size_t at) {
    return (std::uint32_t(v[at]) << 24) | (std::uint32_t(v[at + 1]) << 16) | (std::uint32_t(v[at + 2]) << 8) | v[at + 3];
}

// The channel's own arithmetic (FUN_809561d4) for the handle of the record at `a`,
// transcribed from its decompile, one expression a pool.
static std::uint32_t channel_handle(std::size_t pool, std::uint32_t a) {
    switch (pool) {
    case 0: return (std::uint32_t)((((std::uint64_t)(std::uint32_t)(a + 0x7e9d7348u) * 0x51eb851fu) >> 32) & 0x7fffffc0u) * 0x200u - 0x7e000000u;
    case 1: return (std::uint32_t)((((std::uint64_t)(std::uint32_t)(a + 0x7e99ee80u) * 0x51eb851fu) >> 32) & 0x3fffc0u) * 0x400u - 0x74000000u;
    case 2: return ((a + 0x7e9dd748u) / 200 + 0x2000) * 0x40000u;
    case 3: return ((a + 0x7e9a5280u) / 200 + 0x1100) * 0x80000u;
    case 4: return ((a + 0x7e969640u) / 200 + 0xb00) * 0x100000u;
    case 5: return ((a + 0x7e95b540u) / 200 + 0x700) * 0x200000u;
    case 6: return ((a + 0x7e96c840u) / 200 + 0x280) * 0x400000u;
    case 7: return ((a + 0x7e95ce40u) / 200 + 0xc0) * 0x1000000u;
    case 8: return ((a + 0x7e96ce80u) / 200 + 0x48) * 0x2000000u;
    default: return ((a + 0x7e9a5348u) / 200 + 0x21) * 0x4000000u;
    }
}

static void test_pools() {
    std::uint32_t total = 0;
    for (std::size_t p = 0; p < kCtgpPoolCount; ++p) {
        const CtgpRecordPool& pool = kCtgpRecordPools[p];
        total += pool.count;
        for (std::uint32_t i = 0; i < pool.count; ++i) {
            const std::uint32_t a = pool.start + i * kCtgpRecordSize;
            EXPECT_EQ(pool.handle_base + i * pool.handle_stride, channel_handle(p, a));
        }
    }
    EXPECT_EQ(total, kCtgpRecordCount);
    // the pools cover the record block without gaps or overlaps
    std::map<std::uint32_t, std::uint32_t> by_start;
    for (const CtgpRecordPool& pool : kCtgpRecordPools) by_start[pool.start] = pool.count;
    std::uint32_t expect = kCtgpRecordBlock;
    for (const auto& kv : by_start) {
        EXPECT_EQ(kv.first, expect);
        expect = kv.first + kv.second * kCtgpRecordSize;
    }
    EXPECT_EQ(expect, 0x816B12C0u);
    // the handles cover 0x80000000..0xFFFFFFFF
    std::map<std::uint32_t, std::uint64_t> ranges;
    for (const CtgpRecordPool& pool : kCtgpRecordPools)
        ranges[pool.handle_base] = std::uint64_t(pool.handle_base) + std::uint64_t(pool.count) * pool.handle_stride;
    std::uint64_t next = 0x80000000ull;
    for (const auto& kv : ranges) {
        EXPECT_EQ((std::uint64_t)kv.first, next);
        next = kv.second;
    }
    EXPECT_EQ(next, 0x100000000ull);
}

static void test_record() {
    CtgpOpenFile f;
    f.size = 0x123456;
    f.first_cluster = 0x2A;
    const auto r = ctgp_build_record(f, ctgp_partition_address(1), 0x816228B8, 0x81628CB8);
    EXPECT_EQ(r.size(), (std::size_t)kCtgpRecordSize);
    EXPECT_EQ(get32(r, 0x00), 0x123456u);
    EXPECT_EQ(get32(r, 0x04), 0x2Au);
    EXPECT_EQ(get32(r, 0x08), 0u);
    EXPECT_EQ(get32(r, 0x0C), 0x2Au);
    EXPECT_EQ(get32(r, 0x18), 0x2Au);
    EXPECT_EQ(get32(r, 0x3C), 0x816CD9A0u);   // efa:
    EXPECT_EQ(get32(r, 0x40), 0x816228B8u);
    EXPECT_EQ(get32(r, 0x44), 0x81628CB8u);
    EXPECT_EQ(r[0x48], 1);
    EXPECT_EQ(r[0x49], 0);
    EXPECT_EQ(r[0x4A], 0);
    EXPECT_EQ(r[0x4B], 1);
    EXPECT_EQ(r[0x4E], 0);
    EXPECT_EQ(get32(r, 0x50), 0u);
    f.hashed = true;
    const auto h = ctgp_build_record(f, ctgp_partition_address(0), 0, 0);
    EXPECT_EQ(h[0x4E], 1);
    EXPECT_EQ(get32(h, 0x50), 0x67452301u);
    EXPECT_EQ(get32(h, 0x60), 0xC3D2E1F0u);
    EXPECT_EQ(get32(h, 0x64), 0u);
    EXPECT_EQ(get32(h, 0xB0), 0u);
    EXPECT_EQ(get32(h, 0xB4), 0u);
    EXPECT_EQ(get32(h, 0x3C), 0x816B5740u);   // sda:
    f.digest[0] = 0x12; f.digest[19] = 0x34;
    const auto hd = ctgp_build_record(f, ctgp_partition_address(1), 0, 0);
    EXPECT_EQ((unsigned)hd[0xB4], 0x12u);       // the expected digest sits at +0xB4..+0xC7
    EXPECT_EQ((unsigned)hd[0xC7], 0x34u);
    f.hashed = false;
    const auto plain = ctgp_build_record(f, ctgp_partition_address(1), 0, 0);
    EXPECT_EQ((unsigned)plain[0xB4], 0u);       // an unsigned file carries none
    EXPECT_EQ(ctgp_device_index("sda"), 0);
    EXPECT_EQ(ctgp_device_index("efb"), 2);
    EXPECT_EQ(ctgp_device_index("usb"), -1);
}

static void test_table() {
    CtgpRecordTable t;
    CtgpRecordTable::Slot a, b, c, again;
    CtgpOpenFile small{1000, 5, false};
    CtgpOpenFile mid{0x30000, 6, false};     // 192 KiB: starts in the second pool
    CtgpOpenFile big{0x500000, 7, true};     // 5 MiB: the pool for files below 8 MiB
    EXPECT_TRUE(t.add("efa:/a", 1, small, a));
    EXPECT_TRUE(t.add("efa:/b", 1, mid, b));
    EXPECT_TRUE(t.add("sda:/c", 0, big, c));
    EXPECT_EQ(a.address, 0x81628CB8u);
    EXPECT_EQ(a.handle, 0x82000000u);
    EXPECT_EQ(b.address, 0x81661180u);
    EXPECT_EQ(b.handle, 0x8C000000u);
    EXPECT_EQ(c.address, 0x816A4AC0u);
    EXPECT_EQ(c.handle, 0xE0000000u);
    // the same source again is the same record
    EXPECT_TRUE(t.add("efa:/a", 1, small, again));
    EXPECT_EQ(again.address, a.address);
    EXPECT_EQ(t.used(), 3u);
    // the open-file lists: newest first, linked both ways
    EXPECT_EQ(t.open_count(1), 2u);
    EXPECT_EQ(t.first_open(1), b.address);
    EXPECT_EQ(t.open_count(0), 1u);
    EXPECT_EQ(t.first_open(0), c.address);
    const auto& img = t.image();
    EXPECT_EQ(img.size(), (std::size_t)kCtgpRecordCount * kCtgpRecordSize);
    EXPECT_EQ(get32(img, a.address - kCtgpRecordBlock + 0x40), b.address);   // a's newer neighbour
    EXPECT_EQ(get32(img, b.address - kCtgpRecordBlock + 0x44), a.address);   // b's older neighbour
    EXPECT_EQ(get32(img, b.address - kCtgpRecordBlock + 0x40), 0u);
    EXPECT_EQ(get32(img, a.address - kCtgpRecordBlock + 0x00), 1000u);
    EXPECT_EQ(img[c.address - kCtgpRecordBlock + 0x4E], 1);
}

static void test_overflow() {
    // eight files of 100 MiB..: the pool for files below 128 MiB holds 8, then the
    // pool for files below 256 MiB takes one more, then nothing is left
    CtgpRecordTable t;
    CtgpRecordTable::Slot s;
    CtgpOpenFile huge{0x06000000, 9, false};
    for (int i = 0; i < 8; ++i) EXPECT_TRUE(t.add("efa:/h" + std::to_string(i), 1, huge, s));
    EXPECT_EQ(s.handle, 0x90000000u + 7 * 0x02000000u);
    EXPECT_TRUE(t.add("efa:/h8", 1, huge, s));
    EXPECT_EQ(s.address, 0x8165ACB8u);
    EXPECT_EQ(s.handle, 0x84000000u);
    EXPECT_TRUE(!t.add("efa:/h9", 1, huge, s));
    // a file of 256 MiB or more has no pool
    CtgpOpenFile impossible{0x10000000, 9, false};
    EXPECT_TRUE(!t.add("efa:/i", 1, impossible, s));
    // a small file whose pools are all taken moves on to the next ones
    CtgpRecordTable u;
    CtgpOpenFile tiny{10, 3, false};
    for (int i = 0; i < 1024; ++i) EXPECT_TRUE(u.add("efa:/t" + std::to_string(i), 1, tiny, s));
    EXPECT_EQ(s.address, 0x81628CB8u + 1023 * 200u);
    EXPECT_TRUE(u.add("efa:/next", 1, tiny, s));
    EXPECT_EQ(s.address, 0x81661180u);      // the second pool
    EXPECT_EQ(s.handle, 0x8C000000u);
}

// A table in the on-disc form: entry 0 the root, then (is_dir, name, a, b).
struct E { bool dir; std::string name; std::uint32_t a, b; };
static std::vector<std::uint8_t> make_fst(const std::vector<E>& es, const std::string& tail = std::string()) {
    std::vector<std::uint8_t> out;
    auto w32 = [&](std::uint32_t x) { for (int s = 24; s >= 0; s -= 8) out.push_back(std::uint8_t(x >> s)); };
    std::string names;
    std::vector<std::uint32_t> name_at;
    for (const E& e : es) { name_at.push_back((std::uint32_t)names.size()); names += e.name; names.push_back(0); }
    for (std::size_t i = 0; i < es.size(); ++i) {
        w32((es[i].dir ? 0x01000000u : 0u) | name_at[i]);
        w32(es[i].a);
        w32(es[i].b);
    }
    out.insert(out.end(), names.begin(), names.end());
    out.insert(out.end(), tail.begin(), tail.end());
    return out;
}

static void test_package_and_merge() {
    // package list: /Scene/a.szs -> efa:/p/a, /Scene/b.szs -> efa:/p/b (optional),
    // /c.bin -> sda:/c (required, signed)
    const std::string tail = std::string("efa:/p/a") + '\0' + "efa:/p/b" + '\0' + "sda:/c" + '\0';
    // the strings sit after the name table; their positions are filled in below
    std::vector<E> es = {{true, "", 0, 4}, {true, "Scene", 0, 3}, {false, "a.szs", 0, 2}, {false, "b.szs", 0, 0}, {false, "c.bin", 0, 3}};
    es[0].b = 5;
    es[1].b = 4;
    std::vector<std::uint8_t> probe = make_fst(es);
    const std::uint32_t base = (std::uint32_t)probe.size();
    es[2].a = base;
    es[3].a = base + 9;
    es[4].a = base + 18;
    const std::vector<std::uint8_t> bytes = make_fst(es, tail);
    std::vector<CtgpPackageFile> files;
    std::string error;
    EXPECT_TRUE(ctgp_parse_package_fst(bytes.data(), bytes.size(), files, error));
    EXPECT_EQ(files.size(), (std::size_t)3);
    if (files.size() == 3) {
        EXPECT_EQ(files[0].game_path, std::string("/Scene/a.szs"));
        EXPECT_EQ(files[0].source, std::string("efa:/p/a"));
        EXPECT_EQ(files[0].flags, 2u);
        EXPECT_EQ(files[1].game_path, std::string("/Scene/b.szs"));
        EXPECT_EQ(files[1].flags, 0u);
        EXPECT_EQ(files[2].game_path, std::string("/c.bin"));
        EXPECT_EQ(files[2].source, std::string("sda:/c"));
        EXPECT_EQ(files[2].flags, 3u);
    }
    // a source path that runs off the end of the file is refused
    std::vector<std::uint8_t> cut(bytes.begin(), bytes.end() - 2);
    EXPECT_TRUE(!ctgp_parse_package_fst(cut.data(), cut.size(), files, error));

    // resolve: a and c exist, b does not
    auto resolver = [](const std::string& device, const std::string& path, CtgpSourceFile& out) {
        if (device == "efa" && path == "/p/a") { out.size = 4000; out.first_cluster = 10; return true; }
        if (device == "sda" && path == "/c") { out.size = 0x50000; out.first_cluster = 20; return true; }
        return false;
    };
    CtgpRecordTable table;
    std::vector<CtgpMergedFile> merged;
    EXPECT_TRUE(ctgp_parse_package_fst(bytes.data(), bytes.size(), files, error));
    std::array<std::uint8_t, 20> da{}, dc{};
    for (int i = 0; i < 20; ++i) { da[i] = std::uint8_t(0xA0 + i); dc[i] = std::uint8_t(0xC0 + i); }
    const CtgpHashList hashes = CtgpHashList::unverified_for_tests({{"/Scene/a.szs", da}, {"/c.bin", dc}});
    EXPECT_TRUE(ctgp_add_package(table, files, resolver, &hashes, merged, error));
    EXPECT_EQ(merged.size(), (std::size_t)2);   // b is optional and missing: left out
    if (merged.size() == 2) {
        EXPECT_EQ(merged[0].game_path, std::string("/Scene/a.szs"));
        EXPECT_EQ(merged[0].handle, 0x82000000u);
        EXPECT_EQ(merged[0].size, 4000u);
        EXPECT_EQ(merged[1].handle, 0x80000000u);   // 0x50000 bytes: the pool for files below 1 MiB
    }
    // a required file that is missing fails the package
    files[0].flags |= 1;
    files[0].source = "efa:/p/zzz";
    CtgpRecordTable table2;
    std::vector<CtgpMergedFile> none;
    EXPECT_TRUE(!ctgp_add_package(table2, files, resolver, &hashes, none, error));
    // a signed file needs a hash list that names it
    files[0].source = "efa:/p/a";
    files[0].flags = 2;
    CtgpRecordTable table3;
    EXPECT_TRUE(!ctgp_add_package(table3, files, resolver, nullptr, none, error));
    const CtgpHashList partial = CtgpHashList::unverified_for_tests({{"/c.bin", dc}});
    EXPECT_TRUE(!ctgp_add_package(table3, files, resolver, &partial, none, error));
    EXPECT_TRUE(error.find("/Scene/a.szs") != std::string::npos);
    EXPECT_TRUE(none.empty());
    EXPECT_TRUE(none.empty());

    // the game's table: /Scene/a.szs exists (a disc extent), /Scene/new/x.szs and /c.bin do not
    std::vector<E> game = {{true, "", 0, 4}, {true, "Scene", 0, 4}, {false, "a.szs", 0x1000, 77}, {false, "z.szs", 0x2000, 88}};
    game[0].b = 4;
    game[1].b = 4;
    const auto game_bytes = make_fst(game);
    Fst fst;
    EXPECT_TRUE(Fst::parse(game_bytes.data(), game_bytes.size(), true, fst, error));
    merged.push_back({"/Scene/new/x.szs", 0x8C000000u, 9});
    merged.push_back({"/Scene/new/x.szs", 0x8C010000u, 10});   // the last one wins
    EXPECT_TRUE(ctgp_merge_files(fst, merged, error));
    const std::uint32_t ia = fst.find("/Scene/a.szs");
    EXPECT_TRUE(ia != Fst::npos);
    EXPECT_EQ(fst.entries()[ia].offset, (std::uint64_t)0x82000000u << 2);
    EXPECT_EQ(fst.entries()[ia].size, 4000u);
    const std::uint32_t ic = fst.find("/c.bin");
    EXPECT_TRUE(ic != Fst::npos);
    EXPECT_EQ(fst.entries()[ic].size, 0x50000u);
    const std::uint32_t ix = fst.find("/Scene/new/x.szs");
    EXPECT_TRUE(ix != Fst::npos);
    EXPECT_EQ(fst.entries()[ix].size, 10u);
    // the table still serialises, and the raw offset field comes out as the handle
    std::vector<std::uint8_t> out;
    EXPECT_TRUE(fst.serialize(out, error));
    Fst back;
    EXPECT_TRUE(Fst::parse(out.data(), out.size(), true, back, error));
    const std::uint32_t ib = back.find("/Scene/a.szs");
    EXPECT_TRUE(ib != Fst::npos);
    EXPECT_EQ(back.entries()[ib].offset, (std::uint64_t)0x82000000u << 2);
    // a directory in the game where the package has a file
    std::vector<CtgpMergedFile> clash = {{"/Scene", 0x80000000u, 1}};
    EXPECT_TRUE(!ctgp_merge_files(fst, clash, error));
}

static void test_untrusted_flags_and_duplicates() {
    const std::array<std::uint8_t, 20> digest{};
    const auto hashes = CtgpHashList::unverified_for_tests({{"/a", digest}, {"/alias", digest}});
    auto resolver = [](const std::string&, const std::string&, CtgpSourceFile& file) {
        file.size = 4;
        file.first_cluster = 9;
        return true;
    };
    CtgpRecordTable table;
    std::vector<CtgpMergedFile> merged;
    std::string error;
    // Clearing flag 2 still produces a hashed record.
    EXPECT_TRUE(ctgp_add_package(table, {{"/a", "efa:/a", 0}}, resolver, &hashes, merged, error));
    EXPECT_EQ(table.image()[kCtgpRecordPools[0].start - kCtgpRecordBlock + 0x4E], 1);
    CtgpRecordTable missing;
    EXPECT_TRUE(!ctgp_add_package(missing, {{"/unsigned", "efa:/unsigned", 0}}, resolver, &hashes, merged, error));
    EXPECT_TRUE(!ctgp_add_package(missing, {{"/a", "efa:/a", 0}}, resolver, nullptr, merged, error));
    // Duplicate game paths are rejected even if the first record cleared all flags.
    EXPECT_TRUE(!ctgp_add_package(missing, {{"/a", "efa:/x", 0}, {"/a", "efa:/a", 2}},
                                 resolver, &hashes, merged, error));
    EXPECT_EQ(missing.used(), 0u);
    // Shared sources are safe only when the signed digests agree.
    EXPECT_TRUE(ctgp_add_package(table, {{"/alias", "efa:/a", 0}}, resolver, &hashes, merged, error));
    auto changed = digest;
    changed[0] = 1;
    const auto conflicting = CtgpHashList::unverified_for_tests({{"/alias", changed}});
    EXPECT_TRUE(!ctgp_add_package(table, {{"/alias", "efa:/a", 0}}, resolver, &conflicting, merged, error));
}

// My Stuff's unsigned files are allowed only for the package that permits them.
static void test_my_stuff_unsigned() {
    auto resolver = [](const std::string&, const std::string&, CtgpSourceFile& file) {
        file.size = 4;
        file.first_cluster = 9;
        return true;
    };
    std::string error;
    // A My Stuff file with no signed digest (flag 2 clear) is allowed when the
    // package permits unsigned files; its record is left unhashed.
    CtgpRecordTable mine;
    std::vector<CtgpMergedFile> merged;
    EXPECT_TRUE(ctgp_add_package(mine, {{"/replace.szs", "SdA:/CTGPR/My Stuff/replace.szs", 0}},
                                 resolver, nullptr, merged, error, /*allow_unsigned=*/true));
    EXPECT_EQ(merged.size(), (std::size_t)1);
    EXPECT_EQ(mine.image()[kCtgpRecordPools[0].start - kCtgpRecordBlock + 0x4E], 0);  // not hashed
    for (const char* source : {"sda:/elsewhere/payload.bin", "efa:/ctgpr/My Stuff/a",
                               "sda:/ctgpr/My Stuff/../a", "sda:/ctgpr/My Stuff/sub/../../a",
                               "sda:/ctgpr/My Stuffish/a", "sda:/ctgpr/My Stuff/"}) {
        CtgpRecordTable outside;
        std::vector<CtgpMergedFile> refused;
        EXPECT_TRUE(!ctgp_add_package(outside, {{"/a", source, 0}}, resolver, nullptr, refused, error, true));
        EXPECT_TRUE(refused.empty());
        EXPECT_EQ(outside.used(), 0u);
    }
    // The same unsigned file in a strict (non-My-Stuff) package is refused.
    CtgpRecordTable strict;
    std::vector<CtgpMergedFile> none;
    EXPECT_TRUE(!ctgp_add_package(strict, {{"/replace.szs", "sda:/replace.szs", 0}},
                                  resolver, nullptr, none, error, /*allow_unsigned=*/false));
    EXPECT_TRUE(error.find("no signed digest") != std::string::npos);
    // Even in My Stuff, a file the list marks signed (flag 2) still needs its
    // digest, as in the channel: with no list it is refused.
    CtgpRecordTable mine2;
    EXPECT_TRUE(!ctgp_add_package(mine2, {{"/signed.szs", "SdA:/CTGPR/My Stuff/signed.szs", 2}},
                                  resolver, nullptr, none, error, /*allow_unsigned=*/true));
}

static void test_package_limits() {
    std::vector<CtgpPackageFile> files;
    std::string error;
    auto parse = [&](std::vector<E> es, const std::string& source) {
        const auto prefix = make_fst(es);
        for (E& e : es) if (!e.dir) e.a = prefix.size();
        const auto bytes = make_fst(es, source + '\0');
        return ctgp_parse_package_fst(bytes.data(), bytes.size(), files, error);
    };
    EXPECT_TRUE(parse({{true, "", 0, 2}, {false, "a", 0, 0}}, std::string(kCtgpPackageMaxPath, 'a')));
    EXPECT_TRUE(!parse({{true, "", 0, 2}, {false, "a", 0, 0}}, std::string(kCtgpPackageMaxPath + 1, 'a')));
    std::vector<E> many = {{true, "", 0, kCtgpPackageMaxEntries + 1}};
    for (std::size_t i = 0; i < kCtgpPackageMaxEntries; ++i) many.push_back({false, "a", 0, 0});
    EXPECT_TRUE(!parse(many, "efa:/a"));
    many.resize(kCtgpPackageMaxFiles + 2);
    many[0].b = many.size();
    EXPECT_TRUE(!parse(many, "efa:/a"));
    // Repeated offsets still count every copied string against the budget.
    many.resize(1100);
    many[0].b = many.size();
    EXPECT_TRUE(!parse(many, std::string(1024, 'a')));
    EXPECT_TRUE(error.find("copied-path limit") != std::string::npos);
}

int main() {
    test_package_limits();
    test_pools();
    test_untrusted_flags_and_duplicates();
    test_my_stuff_unsigned();
    test_record();
    test_table();
    test_overflow();
    test_package_and_merge();
    if (g_failures) {
        std::cerr << g_failures << " failures" << std::endl;
        return 1;
    }
    std::cout << "ctgpfiles tests passed" << std::endl;
    return 0;
}
