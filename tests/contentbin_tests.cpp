// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// content.bin, a channel the Wii Menu moved to the SD card: finding the
// "Bk" header past the encrypted banner parts, which contents follow, each
// decrypted with the console's key, and a BootMii keys.bin. The keys are
// made up here: RiftWii carries no Nintendo key, and these tests need none.
#include "riftwii/contentbin.hpp"

#include <cstring>
#include <iostream>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

void put16(std::uint8_t* p, std::uint16_t v) {
    p[0] = std::uint8_t(v >> 8);
    p[1] = std::uint8_t(v);
}
void put32(std::uint8_t* p, std::uint32_t v) {
    put16(p, std::uint16_t(v >> 16));
    put16(p + 2, std::uint16_t(v));
}
void put64(std::uint8_t* p, std::uint64_t v) {
    put32(p, std::uint32_t(v >> 32));
    put32(p + 4, std::uint32_t(v));
}
void pad_to_64(std::vector<std::uint8_t>& v) { v.resize((v.size() + 63) & ~std::size_t(63), 0); }

const std::uint8_t kPrngKey[16] = {0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
                                   0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F};
constexpr std::uint32_t kConsoleId = 0x0403AC68;
constexpr std::uint64_t kTitle = 0x0001000157424D45ull;  // WBME

struct Built {
    std::vector<std::uint8_t> file;
    std::vector<std::vector<std::uint8_t>> plain;  // each content's own bytes
};

// A content.bin of three content records: a banner, a shared content left
// out (as the Wii Menu leaves /shared1 on the Wii) and a program.
// `icon_bytes` is part B's size, which only the encrypted part A says.
Built build_content_bin(std::size_t icon_bytes) {
    Built b;
    b.plain.push_back(std::vector<std::uint8_t>(0x30, 0x11));
    b.plain.push_back(std::vector<std::uint8_t>(0x50, 0x22));
    std::vector<std::uint8_t> program(0x123);
    for (std::size_t i = 0; i < program.size(); ++i) program[i] = static_cast<std::uint8_t>(i * 7);
    b.plain.push_back(program);
    const std::uint16_t types[3] = {1, 0x8001, 1};

    std::vector<std::uint8_t> tmd(0x1E4 + 3 * 36, 0);
    put64(tmd.data() + 0x18C, kTitle);
    put16(tmd.data() + 0x1DE, 3);
    put16(tmd.data() + 0x1E0, 2);
    for (std::size_t i = 0; i < 3; ++i) {
        std::uint8_t* r = tmd.data() + 0x1E4 + i * 36;
        put32(r, static_cast<std::uint32_t>(0x10 + i));
        put16(r + 4, static_cast<std::uint16_t>(i));
        put16(r + 6, types[i]);
        put64(r + 8, b.plain[i].size());
        const Sha1Digest h = sha1(b.plain[i].data(), b.plain[i].size());
        std::memcpy(r + 16, h.data(), 20);
    }

    std::vector<std::uint8_t> data;
    for (std::size_t i = 0; i < 3; ++i) {
        if (types[i] & 0x8000) continue;
        std::vector<std::uint8_t> c = b.plain[i];
        c.resize((c.size() + 15) & ~std::size_t(15), 0);
        std::uint8_t iv[16] = {};
        put16(iv, static_cast<std::uint16_t>(i));
        Aes128(kPrngKey).cbc_encrypt(iv, c.data(), c.data(), c.size());
        data.insert(data.end(), c.begin(), c.end());
        pad_to_64(data);
    }

    // Parts A and B: noise, as they would read without the SD key. One
    // stretch looks like the "Bk" magic, to show the size check is needed.
    std::vector<std::uint8_t> parts_ab(0x640 + icon_bytes);
    for (std::size_t i = 0; i < parts_ab.size(); ++i) parts_ab[i] = static_cast<std::uint8_t>(i * 13 + 5);
    const std::uint8_t fake_magic[8] = {0, 0, 0, 0x70, 'B', 'k', 0, 1};
    std::memcpy(parts_ab.data() + 0x640, fake_magic, 8);
    pad_to_64(parts_ab);

    std::vector<std::uint8_t> certs(0x340, 0xCE);
    std::vector<std::uint8_t> bk(0x80, 0);
    put32(bk.data(), 0x70);
    bk[4] = 'B';
    bk[5] = 'k';
    put16(bk.data() + 6, 1);
    put32(bk.data() + 0x08, kConsoleId);
    put32(bk.data() + 0x14, static_cast<std::uint32_t>(tmd.size()));
    put32(bk.data() + 0x18, static_cast<std::uint32_t>(data.size()));
    // Records 0 and 2 are in the file; the shared one (1) is not.
    bk[0x20] = 0x05;
    std::vector<std::uint8_t> padded_tmd = tmd;
    pad_to_64(padded_tmd);
    put32(bk.data() + 0x1C, static_cast<std::uint32_t>(bk.size() + padded_tmd.size() + data.size() + certs.size()));

    for (const auto* part : {&parts_ab, &bk, &padded_tmd, &data, &certs})
        b.file.insert(b.file.end(), part->begin(), part->end());
    return b;
}

void test_layout_and_contents() {
    for (std::size_t icon : {std::size_t(0x40), std::size_t(0x1234), std::size_t(0x9000)}) {
        const Built b = build_content_bin(icon);
        ContentBinLayout layout;
        std::string error;
        EXPECT_TRUE(find_content_bin_layout(b.file.data(), b.file.size(), b.file.size(), layout, error));
        EXPECT_EQ(layout.bk_offset, (0x640 + icon + 63) & ~std::uint64_t(63));
        EXPECT_EQ(layout.console_id, kConsoleId);
        EXPECT_TRUE(content_bin_includes(layout, 0));
        EXPECT_TRUE(!content_bin_includes(layout, 1));
        EXPECT_TRUE(content_bin_includes(layout, 2));
        EXPECT_TRUE(!content_bin_includes(layout, 999));

        TmdContents tmd;
        EXPECT_TRUE(parse_tmd_contents(b.file.data() + layout.tmd_offset, layout.tmd_size, tmd, error));
        EXPECT_EQ(tmd.title_id, kTitle);
        std::vector<WadContentPlace> places;
        EXPECT_TRUE(content_bin_places(layout, tmd, places, error));
        EXPECT_EQ(places.size(), std::size_t(2));
        const std::size_t plain_of[2] = {0, 2};
        for (std::size_t i = 0; i < places.size() && i < 2; ++i) {
            std::vector<std::uint8_t> bytes(b.file.begin() + static_cast<long>(places[i].offset),
                                            b.file.begin() + static_cast<long>(places[i].offset + places[i].stored));
            WadContentDecrypter decrypter(kPrngKey, places[i].content);
            const std::size_t own = decrypter.decrypt(bytes.data(), bytes.size());
            const std::vector<std::uint8_t>& want = b.plain[plain_of[i]];
            EXPECT_EQ(own, want.size());
            EXPECT_TRUE(std::memcmp(bytes.data(), want.data(), want.size()) == 0);
            EXPECT_TRUE(decrypter.matches());
        }
    }
}

void test_bad_files() {
    const Built b = build_content_bin(0x200);
    ContentBinLayout layout;
    std::string error;
    // Cut short: the backup size no longer reaches the end.
    EXPECT_TRUE(!find_content_bin_layout(b.file.data(), b.file.size() - 64, b.file.size() - 64, layout, error));
    // The "Bk" header past the part of the file searched.
    EXPECT_TRUE(!find_content_bin_layout(b.file.data(), 0x700, b.file.size(), layout, error));
    // Not a content.bin at all.
    const std::vector<std::uint8_t> zeros(0x2000, 0);
    EXPECT_TRUE(!find_content_bin_layout(zeros.data(), zeros.size(), zeros.size(), layout, error));
    EXPECT_TRUE(!find_content_bin_layout(nullptr, 0, 0, layout, error));

    // A TMD whose program is bigger than the content data.
    EXPECT_TRUE(find_content_bin_layout(b.file.data(), b.file.size(), b.file.size(), layout, error));
    TmdContents tmd;
    EXPECT_TRUE(parse_tmd_contents(b.file.data() + layout.tmd_offset, layout.tmd_size, tmd, error));
    tmd.contents[2].size = 0x100000;
    std::vector<WadContentPlace> places;
    EXPECT_TRUE(!content_bin_places(layout, tmd, places, error));
    tmd.contents[2].size = 0xFFFFFFFFFFFFFFF0ull;
    EXPECT_TRUE(!content_bin_places(layout, tmd, places, error));

    // Another Wii's key: the hashes do not match.
    EXPECT_TRUE(parse_tmd_contents(b.file.data() + layout.tmd_offset, layout.tmd_size, tmd, error));
    EXPECT_TRUE(content_bin_places(layout, tmd, places, error));
    const std::uint8_t other[16] = {9};
    std::vector<std::uint8_t> bytes(b.file.begin() + static_cast<long>(places[1].offset),
                                    b.file.begin() + static_cast<long>(places[1].offset + places[1].stored));
    WadContentDecrypter bad(other, places[1].content);
    bad.decrypt(bytes.data(), bytes.size());
    EXPECT_TRUE(!bad.matches());
}

void test_bootmii_keys() {
    std::vector<std::uint8_t> keys(0x400, 0);
    const char header[] = "BackupMii v1, ConsoleID: 0403ac68\n";
    std::memcpy(keys.data(), header, sizeof header - 1);
    put32(keys.data() + 0x100 + 0x24, kConsoleId);
    std::memcpy(keys.data() + 0x100 + 0x68, kPrngKey, 16);
    BackupKey key;
    EXPECT_TRUE(parse_bootmii_keys(keys.data(), keys.size(), key));
    EXPECT_EQ(key.console_id, kConsoleId);
    EXPECT_TRUE(std::memcmp(key.prng, kPrngKey, 16) == 0);
    EXPECT_TRUE(!parse_bootmii_keys(keys.data(), 0x150, key));
    std::vector<std::uint8_t> blank(0x400, 0);
    EXPECT_TRUE(!parse_bootmii_keys(blank.data(), blank.size(), key));

    // Dolphin's default console: a real console ID with an all-zero PRNG key.
    std::vector<std::uint8_t> dolphin(0x400, 0);
    std::memcpy(dolphin.data(), header, sizeof header - 1);
    put32(dolphin.data() + 0x100 + 0x24, kConsoleId);
    BackupKey zero_key;
    EXPECT_TRUE(parse_bootmii_keys(dolphin.data(), dolphin.size(), zero_key));
    EXPECT_EQ(zero_key.console_id, kConsoleId);
    const std::uint8_t zeros[16] = {};
    EXPECT_TRUE(std::memcmp(zero_key.prng, zeros, 16) == 0);
}

void test_matches_tmd() {
    const Built b = build_content_bin(0x200);
    ContentBinLayout layout;
    std::string error;
    EXPECT_TRUE(find_content_bin_layout(b.file.data(), b.file.size(), b.file.size(), layout, error));
    TmdContents moved;
    EXPECT_TRUE(parse_tmd_contents(b.file.data() + layout.tmd_offset, layout.tmd_size, moved, error));
    TmdContents installed = moved;
    EXPECT_TRUE(content_bin_matches_tmd(moved, installed, error));
    // The channel was updated on the Wii after it was moved.
    installed.contents[2].sha1[0] ^= 1;
    EXPECT_TRUE(!content_bin_matches_tmd(moved, installed, error));
    installed = moved;
    installed.contents[2].size += 0x10;
    EXPECT_TRUE(!content_bin_matches_tmd(moved, installed, error));
    installed = moved;
    installed.contents[0].id = 0x99;
    EXPECT_TRUE(!content_bin_matches_tmd(moved, installed, error));
    installed = moved;
    installed.contents[1].type = 1;
    EXPECT_TRUE(!content_bin_matches_tmd(moved, installed, error));
    installed = moved;
    installed.contents.pop_back();
    EXPECT_TRUE(!content_bin_matches_tmd(moved, installed, error));
    installed = moved;
    installed.title_id ^= 1;
    EXPECT_TRUE(!content_bin_matches_tmd(moved, installed, error));
}

void test_staged_paths() {
    std::uint64_t title = 0;
    std::uint32_t content = 0;
    EXPECT_TRUE(parse_staged_content_path("/title/00010001/57424d45/content/0000001a.app", title, content));
    EXPECT_EQ(title, kTitle);
    EXPECT_EQ(content, 0x1Au);
    EXPECT_TRUE(!parse_staged_content_path("/title/00010001/57424d45/content/title.tmd", title, content));
    EXPECT_TRUE(!parse_staged_content_path("/title/00010001/57424d45/content/00000002.app.bak", title, content));
    EXPECT_TRUE(!parse_staged_content_path("/title/00010001/57424d45/content/2.app", title, content));
    EXPECT_TRUE(!parse_staged_content_path("/title/00010001/57424D45/content/00000002.app", title, content));
    EXPECT_TRUE(!parse_staged_content_path("/title/00010001/57424d45/data/00000002.app", title, content));
    // Not a channel: a system title (IOS 58) or a disc game's.
    EXPECT_TRUE(!parse_staged_content_path("/title/00000001/0000003a/content/00000002.app", title, content));
    EXPECT_TRUE(!parse_staged_content_path("/title/00010000/52534245/content/00000002.app", title, content));
    EXPECT_TRUE(!parse_staged_content_path("", title, content));
}

void test_path() {
    EXPECT_EQ(content_bin_path("WBME"), std::string("/private/wii/title/WBME/content.bin"));
}

}  // namespace

int main() {
    test_layout_and_contents();
    test_bad_files();
    test_bootmii_keys();
    test_matches_tmd();
    test_staged_paths();
    test_path();
    if (g_failures == 0) std::cout << "contentbin tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
