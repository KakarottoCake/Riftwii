// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// TGC images inside a GameCube disc (riftwii/gctgc.hpp): the header as a
// bonus disc's menu finds it, and the packed program's FST made to point
// at the outer disc. The headers and tables here are made up field by
// field in the format's layout.
#include "riftwii/gctgc.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 24);
    b[at + 1] = static_cast<std::uint8_t>(v >> 16);
    b[at + 2] = static_cast<std::uint8_t>(v >> 8);
    b[at + 3] = static_cast<std::uint8_t>(v);
}

std::uint32_t get32(const std::vector<std::uint8_t>& b, std::size_t at) {
    return std::uint32_t(b[at]) << 24 | std::uint32_t(b[at + 1]) << 16 | std::uint32_t(b[at + 2]) << 8 | b[at + 3];
}

// A TGC header with the layout of the ones on Nintendo's bonus discs: a
// 0x8000-byte header, the packed disc's header area right after it, the
// FST, the program, then the files, which the FST counts from a virtual
// start of its own.
constexpr std::uint32_t kTgcBytes = 0x200000;
constexpr std::uint32_t kFileAreaReal = 0x100000;
constexpr std::uint32_t kFileAreaVirtual = 0x0C000000;

std::vector<std::uint8_t> make_header() {
    std::vector<std::uint8_t> h(riftwii::kGcTgcHeaderBytes, 0);
    put32(h, 0x00, riftwii::kGcTgcMagic);
    put32(h, 0x08, 0x8000);      // the header's size
    put32(h, 0x0C, 0x10000);     // the packed disc header area
    put32(h, 0x10, 0x18000);     // the FST
    put32(h, 0x14, 0x40);
    put32(h, 0x18, 0x80);
    put32(h, 0x1C, 0x20000);     // the program
    put32(h, 0x20, 0x8000);
    put32(h, 0x24, kFileAreaReal);
    put32(h, 0x34, kFileAreaVirtual);
    return h;
}

// An FST: the root, then the given files (name, offset, size), then the
// names' table.
struct FileSpec {
    std::string name;
    std::uint32_t offset;
    std::uint32_t size;
};
std::vector<std::uint8_t> make_fst(const std::vector<FileSpec>& files) {
    const std::uint32_t count = static_cast<std::uint32_t>(files.size()) + 1;
    std::vector<std::uint8_t> fst(12 * count, 0);
    fst[0] = 1;
    put32(fst, 8, count);
    std::vector<std::uint8_t> names{0};  // the root's name, empty
    for (std::size_t i = 0; i < files.size(); ++i) {
        const std::size_t at = 12 * (i + 1);
        put32(fst, at, static_cast<std::uint32_t>(names.size()));  // a file: kind 0, then the name's offset
        put32(fst, at + 4, files[i].offset);
        put32(fst, at + 8, files[i].size);
        names.insert(names.end(), files[i].name.begin(), files[i].name.end());
        names.push_back(0);
    }
    fst.insert(fst.end(), names.begin(), names.end());
    return fst;
}

void test_parse() {
    const std::uint32_t disc_offset = 0x40000000;
    const std::uint64_t image_bytes = 0x57058000;  // a full GameCube disc
    std::vector<std::uint8_t> h = make_header();
    riftwii::GcTgc tgc;
    std::string why;
    EXPECT_TRUE(riftwii::parse_gc_tgc(h.data(), h.size(), disc_offset, kTgcBytes, image_bytes, tgc, why));
    EXPECT_EQ(tgc.header_bytes, 0x8000u);
    EXPECT_EQ(tgc.boot_at(), disc_offset + 0x8000u);
    EXPECT_EQ(tgc.bi2_at(), disc_offset + 0x8440u);
    EXPECT_EQ(tgc.fst_at(), disc_offset + 0x18000u);
    EXPECT_EQ(tgc.fst_max, 0x80u);
    EXPECT_EQ(tgc.dol_at(), disc_offset + 0x20000u);
    EXPECT_EQ(tgc.dol_end(), disc_offset + 0x28000u);

    // A largest FST size below its size is taken as its size.
    put32(h, 0x18, 0);
    EXPECT_TRUE(riftwii::parse_gc_tgc(h.data(), h.size(), disc_offset, kTgcBytes, image_bytes, tgc, why));
    EXPECT_EQ(tgc.fst_max, 0x40u);

    auto refused = [&](std::size_t field, std::uint32_t value) {
        std::vector<std::uint8_t> bad = make_header();
        put32(bad, field, value);
        riftwii::GcTgc out;
        out.dol_size = 1234;
        std::string reason;
        const bool ok = riftwii::parse_gc_tgc(bad.data(), bad.size(), disc_offset, kTgcBytes, image_bytes, out, reason);
        return !ok && !reason.empty() && out.dol_size == 1234u;  // left as it was
    };
    EXPECT_TRUE(refused(0x00, 0x12345678));       // no magic
    EXPECT_TRUE(refused(0x08, 0));                // no header
    EXPECT_TRUE(refused(0x08, 0x8002));           // not word-aligned
    EXPECT_TRUE(refused(0x08, kTgcBytes));        // the packed disc past the end
    EXPECT_TRUE(refused(0x0C, 0x100));            // too small for boot.bin and BI2
    EXPECT_TRUE(refused(0x0C, 0xFFFFFFFF));       // and wrapping round
    EXPECT_TRUE(refused(0x10, kTgcBytes - 0x20)); // the FST past the end
    EXPECT_TRUE(refused(0x10, 0xFFFFFFF0));       // wrapping round
    EXPECT_TRUE(refused(0x14, 4));                // smaller than its root
    EXPECT_TRUE(refused(0x18, 0x01000000));       // a largest size no game has room for
    EXPECT_TRUE(refused(0x1C, kTgcBytes - 0x100)); // the program past the end
    EXPECT_TRUE(refused(0x20, 0x10));             // smaller than a DOL's header
    EXPECT_TRUE(refused(0x24, kTgcBytes + 1));    // the file area past the end

    // A short header, the TGC past the image's end, and past 4 GB.
    EXPECT_TRUE(!riftwii::parse_gc_tgc(h.data(), 0x30, disc_offset, kTgcBytes, image_bytes, tgc, why));
    EXPECT_TRUE(!riftwii::parse_gc_tgc(h.data(), h.size(), 0x57000000, kTgcBytes, image_bytes, tgc, why));
    EXPECT_TRUE(!riftwii::parse_gc_tgc(h.data(), h.size(), 0xFFF00000, kTgcBytes, 0x200000000ull, tgc, why));
}

void test_dol_fits() {
    std::vector<std::uint8_t> dol(0x100, 0);
    put32(dol, 0x00, 0x100);      // text 0 at 0x100
    put32(dol, 0x90, 0x1000);     // 0x1000 bytes
    put32(dol, 0x1C, 0x1100);     // data 0 at 0x1100
    put32(dol, 0xAC, 0x6F00);     // up to 0x8000
    std::string why;
    EXPECT_TRUE(riftwii::gc_tgc_dol_fits(dol.data(), dol.size(), 0x8000, why));
    EXPECT_TRUE(!riftwii::gc_tgc_dol_fits(dol.data(), dol.size(), 0x7FFF, why));
    put32(dol, 0x1C, 0xFFFFFF00);  // wrapping round
    EXPECT_TRUE(!riftwii::gc_tgc_dol_fits(dol.data(), dol.size(), 0x8000, why));
    EXPECT_TRUE(!riftwii::gc_tgc_dol_fits(dol.data(), 0x80, 0x8000, why));
}

void test_rebase() {
    const std::uint32_t disc_offset = 0x40000000;
    std::vector<std::uint8_t> h = make_header();
    riftwii::GcTgc tgc;
    std::string why;
    EXPECT_TRUE(riftwii::parse_gc_tgc(h.data(), h.size(), disc_offset, kTgcBytes, 0x57058000, tgc, why));

    // Two files counted from the virtual start, and one of no bytes.
    std::vector<std::uint8_t> fst = make_fst({{"opening.bnr", kFileAreaVirtual, 0x1960},
                                              {"Stage/a.arc", kFileAreaVirtual + 0x2000, 0x8000},
                                              {"empty.bin", 0, 0}});
    // And a directory, whose words are indices and stay.
    std::vector<std::uint8_t> with_dir = fst;
    EXPECT_TRUE(riftwii::gc_tgc_rebase_fst(fst, tgc, why));
    EXPECT_EQ(get32(fst, 12 + 4), disc_offset + kFileAreaReal);
    EXPECT_EQ(get32(fst, 12 + 8), 0x1960u);
    EXPECT_EQ(get32(fst, 24 + 4), disc_offset + kFileAreaReal + 0x2000);
    EXPECT_EQ(get32(fst, 36 + 4), disc_offset);  // never read: the TGC's start
    // The names' table is left as it was.
    EXPECT_EQ(fst.size(), with_dir.size());
    EXPECT_EQ(int(fst[48]), 0);

    with_dir[24] = 1;  // entry 2 a directory: parent 0, next 3
    put32(with_dir, 24 + 4, 0);
    put32(with_dir, 24 + 8, 3);
    EXPECT_TRUE(riftwii::gc_tgc_rebase_fst(with_dir, tgc, why));
    EXPECT_EQ(get32(with_dir, 24 + 8), 3u);

    // A file past the TGC's end: refused, the table unchanged.
    std::vector<std::uint8_t> past = make_fst({{"a", kFileAreaVirtual, 0x10},
                                               {"b", kFileAreaVirtual + kTgcBytes - kFileAreaReal - 0x10, 0x20}});
    const std::vector<std::uint8_t> past_before = past;
    EXPECT_TRUE(!riftwii::gc_tgc_rebase_fst(past, tgc, why));
    EXPECT_TRUE(past == past_before);
    // One that ends exactly at the end is fine.
    std::vector<std::uint8_t> at_end = make_fst({{"b", kFileAreaVirtual + kTgcBytes - kFileAreaReal - 0x20, 0x20}});
    EXPECT_TRUE(riftwii::gc_tgc_rebase_fst(at_end, tgc, why));
    // Before the TGC's start (an offset below the virtual start by more
    // than the real file area).
    std::vector<std::uint8_t> before = make_fst({{"c", kFileAreaVirtual - kFileAreaReal - 4, 0x10}});
    EXPECT_TRUE(!riftwii::gc_tgc_rebase_fst(before, tgc, why));
    // A kind that is neither.
    std::vector<std::uint8_t> odd = make_fst({{"d", kFileAreaVirtual, 0x10}});
    odd[12] = 2;
    EXPECT_TRUE(!riftwii::gc_tgc_rebase_fst(odd, tgc, why));
    // A count larger than the table, and no table.
    std::vector<std::uint8_t> counted = make_fst({{"e", kFileAreaVirtual, 0x10}});
    put32(counted, 8, 1000);
    EXPECT_TRUE(!riftwii::gc_tgc_rebase_fst(counted, tgc, why));
    std::vector<std::uint8_t> none;
    EXPECT_TRUE(!riftwii::gc_tgc_rebase_fst(none, tgc, why));
    // A root that is not a directory.
    std::vector<std::uint8_t> no_root = make_fst({{"f", kFileAreaVirtual, 0x10}});
    no_root[0] = 0;
    EXPECT_TRUE(!riftwii::gc_tgc_rebase_fst(no_root, tgc, why));
}

void test_find_files() {
    std::vector<std::uint8_t> fst = make_fst({{"menu.dol", 0x100000, 0x40000},
                                              {"zlj_f.tgc", 0x200000, 0x100000},
                                              {"opening.bnr", 0x300000, 0x1960},
                                              {"ZELDA.TGC", 0x400000, 0x200000},
                                              {".tgc", 0x600000, 0x10},
                                              {"tgc", 0x700000, 0x10}});
    std::vector<riftwii::GcFstFile> found = riftwii::find_gc_tgc_files(fst, 8);
    EXPECT_EQ(found.size(), static_cast<std::size_t>(3));
    if (found.size() == 3) {
        EXPECT_EQ(found[0].offset, 0x200000u);
        EXPECT_EQ(found[0].size, 0x100000u);
        EXPECT_EQ(found[1].offset, 0x400000u);
        EXPECT_EQ(found[2].offset, 0x600000u);
    }
    EXPECT_EQ(riftwii::find_gc_tgc_files(fst, 1).size(), static_cast<std::size_t>(1));

    // A name past the table, or not ended inside it: passed over.
    std::vector<std::uint8_t> bad_name = make_fst({{"x.tgc", 0x200000, 0x10}});
    put32(bad_name, 12, 0x00FFFFFF);
    EXPECT_TRUE(riftwii::find_gc_tgc_files(bad_name, 8).empty());
    std::vector<std::uint8_t> unended = make_fst({{"x.tgc", 0x200000, 0x10}});
    unended.pop_back();
    EXPECT_TRUE(riftwii::find_gc_tgc_files(unended, 8).empty());
    std::vector<std::uint8_t> none;
    EXPECT_TRUE(riftwii::find_gc_tgc_files(none, 8).empty());

    riftwii::GcFstFile file;
    EXPECT_TRUE(riftwii::find_gc_fst_file_at(fst, 0x400000, file));
    EXPECT_EQ(file.size, 0x200000u);
    EXPECT_TRUE(!riftwii::find_gc_fst_file_at(fst, 0x400004, file));
}

}  // namespace

int main() {
    test_parse();
    test_dol_fits();
    test_rebase();
    test_find_files();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "gctgc tests passed" << std::endl;
    return 0;
}
