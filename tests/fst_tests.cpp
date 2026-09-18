// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/fst.hpp"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using Bytes = std::vector<std::uint8_t>;

static void Put32(Bytes& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v >> 24));
    b.push_back(static_cast<std::uint8_t>(v >> 16));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
    b.push_back(static_cast<std::uint8_t>(v));
}

static void Entry(Bytes& b, std::uint8_t type, std::uint32_t name_off, std::uint32_t a, std::uint32_t c) {
    b.push_back(type);
    b.push_back(static_cast<std::uint8_t>(name_off >> 16));
    b.push_back(static_cast<std::uint8_t>(name_off >> 8));
    b.push_back(static_cast<std::uint8_t>(name_off));
    Put32(b, a);
    Put32(b, c);
}

static void Name(Bytes& table, const char* s) {
    table.insert(table.end(), s, s + std::strlen(s));
    table.push_back(0);
}

// Root
//  1 opening.bnr  (file, word 0x40 -> byte 0x100, size 0x50)
//  2 Stage/       (dir, next 6)
//  3   01.arc     (file, word 0x80 -> 0x200, size 0x10)
//  4   sub/       (dir, parent 2, next 6)
//  5     deep.bin (file, word 0xC0 -> 0x300, size 0x20)
//  6 sys.bin      (file, word 0x100 -> 0x400, size 0x30)
static Bytes Fixture() {
    Bytes table;
    const std::uint32_t n_opening = static_cast<std::uint32_t>(table.size()); Name(table, "opening.bnr");
    const std::uint32_t n_stage = static_cast<std::uint32_t>(table.size()); Name(table, "Stage");
    const std::uint32_t n_01 = static_cast<std::uint32_t>(table.size()); Name(table, "01.arc");
    const std::uint32_t n_sub = static_cast<std::uint32_t>(table.size()); Name(table, "sub");
    const std::uint32_t n_deep = static_cast<std::uint32_t>(table.size()); Name(table, "deep.bin");
    const std::uint32_t n_sys = static_cast<std::uint32_t>(table.size()); Name(table, "sys.bin");
    Bytes b;
    Entry(b, 1, 0, 0, 7);
    Entry(b, 0, n_opening, 0x40, 0x50);
    Entry(b, 1, n_stage, 0, 6);
    Entry(b, 0, n_01, 0x80, 0x10);
    Entry(b, 1, n_sub, 2, 6);
    Entry(b, 0, n_deep, 0xC0, 0x20);
    Entry(b, 0, n_sys, 0x100, 0x30);
    b.insert(b.end(), table.begin(), table.end());
    return b;
}

static void test_parse_and_lookup() {
    Bytes img = Fixture();
    riftwii::Fst fst;
    std::string err;
    EXPECT_TRUE(riftwii::Fst::parse(img.data(), img.size(), true, fst, err));
    EXPECT_TRUE(err.empty());
    EXPECT_EQ(fst.count(), std::uint32_t(7));
    EXPECT_TRUE(fst.wii_offsets());
    const auto& e = fst.entries();
    EXPECT_TRUE(e[0].is_directory);
    EXPECT_EQ(e[0].next, std::uint32_t(7));
    EXPECT_EQ(e[1].name, std::string("opening.bnr"));
    EXPECT_EQ(e[1].offset, std::uint64_t(0x100));
    EXPECT_EQ(e[1].size, std::uint32_t(0x50));
    EXPECT_EQ(e[1].parent, std::uint32_t(0));
    EXPECT_TRUE(e[2].is_directory);
    EXPECT_EQ(e[2].next, std::uint32_t(6));
    EXPECT_EQ(e[3].parent, std::uint32_t(2));
    EXPECT_EQ(e[4].parent, std::uint32_t(2));
    EXPECT_EQ(e[5].parent, std::uint32_t(4));
    EXPECT_EQ(e[5].offset, std::uint64_t(0x300));
    EXPECT_EQ(e[6].parent, std::uint32_t(0));

    EXPECT_EQ(fst.find("/"), std::uint32_t(0));
    EXPECT_EQ(fst.find("/Stage/sub/deep.bin"), std::uint32_t(5));
    EXPECT_EQ(fst.find("/Stage"), std::uint32_t(2));
    EXPECT_EQ(fst.find("/Stage/01.arc"), std::uint32_t(3));
    EXPECT_EQ(fst.find("/sys.bin"), std::uint32_t(6));
    EXPECT_EQ(fst.find("/nope"), riftwii::Fst::npos);
    EXPECT_EQ(fst.find("/Stage/01.arc/"), riftwii::Fst::npos);
    EXPECT_EQ(fst.find("/Stage//01.arc"), riftwii::Fst::npos);
    EXPECT_EQ(fst.find("Stage/01.arc"), riftwii::Fst::npos);
    EXPECT_EQ(fst.find("/opening.bnr/x"), riftwii::Fst::npos);
    EXPECT_EQ(fst.find("/STAGE/01.ARC"), riftwii::Fst::npos);
    EXPECT_EQ(fst.find("/STAGE/01.ARC", true), std::uint32_t(3));

    std::vector<std::uint32_t> named = fst.find_files_named("deep.bin");
    EXPECT_EQ(named.size(), std::size_t(1));
    if (!named.empty()) EXPECT_EQ(named[0], std::uint32_t(5));
    EXPECT_TRUE(fst.find_files_named("Stage").empty());  // directories are not files
    EXPECT_EQ(fst.find_files_named("SYS.BIN", true).size(), std::size_t(1));

    std::vector<std::uint32_t> kids;
    EXPECT_TRUE(fst.children(0, kids));
    EXPECT_TRUE(kids == std::vector<std::uint32_t>({1, 2, 6}));
    EXPECT_TRUE(fst.children(2, kids));
    EXPECT_TRUE(kids == std::vector<std::uint32_t>({3, 4}));
    EXPECT_TRUE(fst.children(4, kids));
    EXPECT_TRUE(kids == std::vector<std::uint32_t>({5}));
    EXPECT_FALSE(fst.children(1, kids));
    EXPECT_FALSE(fst.children(99, kids));

    std::string path;
    EXPECT_TRUE(fst.path_of(5, path));
    EXPECT_EQ(path, std::string("/Stage/sub/deep.bin"));
    EXPECT_TRUE(fst.path_of(0, path));
    EXPECT_EQ(path, std::string("/"));
    EXPECT_TRUE(fst.path_of(2, path));
    EXPECT_EQ(path, std::string("/Stage"));
    EXPECT_FALSE(fst.path_of(7, path));

    // GameCube encoding: offsets are plain bytes.
    riftwii::Fst gc;
    EXPECT_TRUE(riftwii::Fst::parse(img.data(), img.size(), false, gc, err));
    EXPECT_EQ(gc.entries()[1].offset, std::uint64_t(0x40));
}

static void test_round_trip() {
    Bytes img = Fixture();
    riftwii::Fst fst;
    std::string err;
    EXPECT_TRUE(riftwii::Fst::parse(img.data(), img.size(), true, fst, err));
    Bytes out;
    EXPECT_TRUE(fst.serialize(out, err));
    EXPECT_TRUE(out == img);
    riftwii::Fst again;
    EXPECT_TRUE(riftwii::Fst::parse(out.data(), out.size(), true, again, err));
    EXPECT_EQ(again.find("/Stage/sub/deep.bin"), std::uint32_t(5));
}

static void test_mutation() {
    Bytes img = Fixture();
    riftwii::Fst fst;
    std::string err;
    EXPECT_TRUE(riftwii::Fst::parse(img.data(), img.size(), true, fst, err));

    EXPECT_TRUE(fst.set_file_extent(3, 0x8000, 0x1234, err));
    EXPECT_EQ(fst.entries()[3].offset, std::uint64_t(0x8000));
    EXPECT_EQ(fst.entries()[3].size, std::uint32_t(0x1234));
    EXPECT_FALSE(fst.set_file_extent(3, 0x8002, 1, err));           // not word aligned
    EXPECT_FALSE(fst.set_file_extent(3, 0x400000000ull, 1, err));   // >> 2 overflows u32
    EXPECT_FALSE(fst.set_file_extent(2, 0, 1, err));                // directory
    EXPECT_TRUE(fst.set_file_extent(3, 0x3FFFFFFFCull, 1, err));    // largest encodable

    // Add into Stage: lands at the end of its subtree (index 6), sys.bin
    // moves to 7, ancestors grow, the sibling subtree "sub" does not.
    std::uint32_t idx = 0;
    EXPECT_TRUE(fst.add_file(2, "new.arc", 0x1000, 0x40, idx, err));
    EXPECT_EQ(idx, std::uint32_t(6));
    EXPECT_EQ(fst.count(), std::uint32_t(8));
    EXPECT_EQ(fst.entries()[0].next, std::uint32_t(8));
    EXPECT_EQ(fst.entries()[2].next, std::uint32_t(7));
    EXPECT_EQ(fst.entries()[4].next, std::uint32_t(6));
    EXPECT_EQ(fst.entries()[6].parent, std::uint32_t(2));
    EXPECT_EQ(fst.entries()[7].name, std::string("sys.bin"));
    EXPECT_EQ(fst.entries()[7].parent, std::uint32_t(0));
    EXPECT_EQ(fst.find("/Stage/new.arc"), std::uint32_t(6));
    EXPECT_EQ(fst.find("/sys.bin"), std::uint32_t(7));
    std::vector<std::uint32_t> kids;
    EXPECT_TRUE(fst.children(2, kids));
    EXPECT_TRUE(kids == std::vector<std::uint32_t>({3, 4, 6}));

    // Add into the nested "sub": everything after shifts, all ancestors grow.
    EXPECT_TRUE(fst.add_file(4, "more.bin", 0x2000, 0x8, idx, err));
    EXPECT_EQ(idx, std::uint32_t(6));
    EXPECT_EQ(fst.entries()[4].next, std::uint32_t(7));
    EXPECT_EQ(fst.entries()[2].next, std::uint32_t(8));
    EXPECT_EQ(fst.entries()[0].next, std::uint32_t(9));
    EXPECT_EQ(fst.find("/Stage/sub/more.bin"), std::uint32_t(6));
    EXPECT_EQ(fst.find("/Stage/new.arc"), std::uint32_t(7));
    EXPECT_EQ(fst.find("/sys.bin"), std::uint32_t(8));
    EXPECT_TRUE(fst.children(4, kids));
    EXPECT_TRUE(kids == std::vector<std::uint32_t>({5, 6}));

    // New directory at the root, then a file inside it.
    std::uint32_t dir = 0;
    EXPECT_TRUE(fst.add_directory(0, "Extra", dir, err));
    EXPECT_EQ(dir, std::uint32_t(9));
    EXPECT_EQ(fst.entries()[9].next, std::uint32_t(10));
    EXPECT_TRUE(fst.add_file(dir, "x.bin", 0x3000, 4, idx, err));
    EXPECT_EQ(idx, std::uint32_t(10));
    EXPECT_EQ(fst.entries()[9].next, std::uint32_t(11));
    EXPECT_EQ(fst.entries()[0].next, std::uint32_t(11));
    EXPECT_EQ(fst.find("/Extra/x.bin"), std::uint32_t(10));
    std::string path;
    EXPECT_TRUE(fst.path_of(10, path));
    EXPECT_EQ(path, std::string("/Extra/x.bin"));

    // Invalid inserts.
    EXPECT_FALSE(fst.add_file(1, "a", 0, 0, idx, err));         // into a file
    EXPECT_FALSE(fst.add_file(0, "a/b", 0, 0, idx, err));       // slash in name
    EXPECT_FALSE(fst.add_file(0, "", 0, 0, idx, err));
    EXPECT_FALSE(fst.add_file(0, "..", 0, 0, idx, err));
    EXPECT_FALSE(fst.add_file(0, "a", 2, 0, idx, err));         // unaligned offset

    // The rebuilt table survives a serialise/parse cycle with the same tree.
    Bytes out;
    EXPECT_TRUE(fst.serialize(out, err));
    riftwii::Fst again;
    EXPECT_TRUE(riftwii::Fst::parse(out.data(), out.size(), true, again, err));
    EXPECT_EQ(again.count(), std::uint32_t(11));
    EXPECT_EQ(again.find("/Stage/sub/more.bin"), std::uint32_t(6));
    EXPECT_EQ(again.find("/Extra/x.bin"), std::uint32_t(10));
    EXPECT_EQ(again.entries()[3].offset, std::uint64_t(0x3FFFFFFFCull));
    EXPECT_TRUE(again.path_of(6, path));
    EXPECT_EQ(path, std::string("/Stage/sub/more.bin"));
}

static void test_malformed() {
    std::string err;
    riftwii::Fst fst;
    auto rejects = [&](Bytes img, const char* what) {
        riftwii::Fst out;
        std::string e;
        if (riftwii::Fst::parse(img.data(), img.size(), true, out, e)) {
            std::cerr << "FAILED: accepted malformed fst: " << what << std::endl;
            g_failures++;
        } else if (e.empty()) {
            std::cerr << "FAILED: no message for malformed fst: " << what << std::endl;
            g_failures++;
        }
    };
    Bytes good = Fixture();
    {
        Bytes b = good; b[0] = 0; rejects(b, "root is a file");
    }
    {
        Bytes b = good; b[8] = b[9] = b[10] = b[11] = 0; rejects(b, "zero count");
    }
    {
        Bytes b = good; b[11] = 200; rejects(b, "count larger than image");
    }
    {
        Bytes b = good; b[12 * 1 + 3] = 0xFF; b[12 * 1 + 2] = 0xFF; rejects(b, "name offset past table");
    }
    {
        Bytes b = good; b.pop_back(); rejects(b, "last name unterminated");
    }
    {
        Bytes b = good; b[12 * 2 + 11] = 2; rejects(b, "directory next <= own index");
    }
    {
        Bytes b = good; b[12 * 4 + 11] = 7; rejects(b, "nested directory escapes parent range");
    }
    {
        Bytes b = good; b[12 * 2 + 11] = 8; rejects(b, "directory next past count");
    }
    {
        Bytes b = good; b[12 * 3] = 2; rejects(b, "invalid type flag");
    }
    {
        Bytes b = good; b[12 * 7] = '/'; rejects(b, "slash in name");
    }
    {
        Bytes b = good; b[12 * 1 + 3] = static_cast<std::uint8_t>(std::strlen("opening.bnr")); rejects(b, "empty name");
    }
    {
        Bytes tiny(8, 0); rejects(tiny, "too small");
    }
    {
        riftwii::FstLimits limits;
        limits.max_depth = 1;
        riftwii::Fst out;
        EXPECT_FALSE(riftwii::Fst::parse(good.data(), good.size(), true, out, err, limits));
        limits.max_depth = 2;
        EXPECT_TRUE(riftwii::Fst::parse(good.data(), good.size(), true, out, err, limits));
        limits.max_entries = 3;
        EXPECT_FALSE(riftwii::Fst::parse(good.data(), good.size(), true, out, err, limits));
    }
    // Output untouched on failure.
    EXPECT_TRUE(riftwii::Fst::parse(good.data(), good.size(), true, fst, err));
    Bytes bad = good; bad[0] = 0;
    EXPECT_FALSE(riftwii::Fst::parse(bad.data(), bad.size(), true, fst, err));
    EXPECT_EQ(fst.count(), std::uint32_t(7));
}

int main() {
    test_parse_and_lookup();
    test_round_trip();
    test_mutation();
    test_malformed();
    if (g_failures == 0) {
        std::cout << "ALL FST TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
