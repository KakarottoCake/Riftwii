// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// WiiWare and Virtual Console titles on the Wii: which titles are games,
// a stored TMD's content records, and the NAND paths of their files.
#include "riftwii/nandchannel.hpp"

#include <iostream>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

std::uint64_t title(std::uint32_t upper, const char* code) {
    std::uint32_t lower = 0;
    for (int i = 0; i < 4; ++i) lower = (lower << 8) | static_cast<std::uint8_t>(code[i]);
    return (std::uint64_t(upper) << 32) | lower;
}

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

// A stored TMD (signature in front, as ES_GetStoredTMD gives it) with the
// given content records: {id, index, type, size}.
std::vector<std::uint8_t> make_tmd(std::uint64_t title_id, std::uint32_t ios, std::uint16_t boot,
                                   const std::vector<TmdContent>& contents) {
    std::vector<std::uint8_t> t(0x1E4 + contents.size() * 36, 0);
    put64(t.data() + 0x184, ios ? (0x100000000ull | ios) : 0);
    put64(t.data() + 0x18C, title_id);
    put16(t.data() + 0x1DE, static_cast<std::uint16_t>(contents.size()));
    put16(t.data() + 0x1E0, boot);
    t[0x198] = '0';
    t[0x199] = '1';
    for (std::size_t i = 0; i < contents.size(); ++i) {
        std::uint8_t* r = t.data() + 0x1E4 + i * 36;
        put32(r, contents[i].id);
        put16(r + 4, contents[i].index);
        put16(r + 6, contents[i].type);
        put64(r + 8, contents[i].size);
    }
    return t;
}

void test_codes() {
    EXPECT_EQ(channel_code(title(kDownloadedChannels, "WBME")), std::string("WBME"));
    EXPECT_EQ(channel_code(title(kDownloadedChannels, "NAKE")), std::string("NAKE"));
    // Not letters and digits: no code.
    EXPECT_EQ(channel_code(title(kDownloadedChannels, "ab12")), std::string(""));
    EXPECT_EQ(channel_code(0x0001000100000002ull), std::string(""));
}

void test_classify() {
    // WiiWare and each Virtual Console letter.
    EXPECT_TRUE(classify_channel(title(kDownloadedChannels, "WBME")).kind == ChannelKind::WiiWare);
    const ChannelClass n64 = classify_channel(title(kDownloadedChannels, "NAKE"));
    EXPECT_TRUE(n64.kind == ChannelKind::VirtualConsole);
    EXPECT_EQ(n64.system, std::string("Nintendo 64"));
    EXPECT_EQ(classify_channel(title(kDownloadedChannels, "FAKE")).system, std::string("NES"));
    EXPECT_EQ(classify_channel(title(kDownloadedChannels, "JADE")).system, std::string("Super NES"));
    EXPECT_EQ(classify_channel(title(kDownloadedChannels, "MAPE")).system, std::string("Mega Drive"));
    EXPECT_TRUE(classify_channel(title(kDownloadedChannels, "WBME")).system.empty());

    // Nintendo's own channels (H), homebrew channels with VC letters and
    // RiftWii's channel are not games.
    EXPECT_TRUE(classify_channel(title(kDownloadedChannels, "HAAA")).kind == ChannelKind::None);
    EXPECT_TRUE(classify_channel(title(kDownloadedChannels, "LULZ")).kind == ChannelKind::None);
    EXPECT_TRUE(classify_channel(title(kDownloadedChannels, "JODI")).kind == ChannelKind::None);
    EXPECT_TRUE(classify_channel(title(kDownloadedChannels, "UFTW")).kind == ChannelKind::None);
    // Not a downloaded channel: a disc's data (00010000) or a system
    // channel (00010002) with a game-like code.
    EXPECT_TRUE(classify_channel(title(0x00010000, "WBME")).kind == ChannelKind::None);
    EXPECT_TRUE(classify_channel(title(0x00010002, "WBME")).kind == ChannelKind::None);
    // An unprintable code.
    EXPECT_TRUE(classify_channel(title(kDownloadedChannels, "w\x01zz")).kind == ChannelKind::None);
}

void test_tmd() {
    const std::uint64_t id = title(kDownloadedChannels, "WBME");
    const std::vector<std::uint8_t> t =
        make_tmd(id, 53, 1, {{0x00000000, 0, 1, 0x40000}, {0x00000003, 1, 1, 0x200000}, {0x00000007, 2, 0x8001, 0x1000}});
    TmdContents parsed;
    std::string error;
    EXPECT_TRUE(parse_tmd_contents(t.data(), t.size(), parsed, error));
    EXPECT_EQ(parsed.title_id, id);
    EXPECT_EQ(parsed.required_ios, 53u);
    EXPECT_EQ(parsed.boot_index, 1);
    EXPECT_EQ(parsed.maker, std::string("01"));
    EXPECT_EQ(channel_game_id("WBME", parsed.maker), std::string("WBME01"));
    EXPECT_EQ(channel_game_id("WBME", ""), std::string("WBME01"));
    EXPECT_EQ(channel_game_id("NAKE", "8P"), std::string("NAKE8P"));
    EXPECT_EQ(parsed.contents.size(), std::size_t(3));
    const TmdContent* boot = tmd_content_by_index(parsed, parsed.boot_index);
    EXPECT_TRUE(boot != nullptr);
    if (boot) {
        EXPECT_EQ(boot->id, 3u);
        EXPECT_EQ(boot->size, 0x200000ull);
    }
    EXPECT_TRUE(tmd_content_by_index(parsed, 9) == nullptr);

    // No IOS named (a sys_version that is not 1:NN).
    const std::vector<std::uint8_t> no_ios = make_tmd(id, 0, 0, {{0, 0, 1, 16}});
    EXPECT_TRUE(parse_tmd_contents(no_ios.data(), no_ios.size(), parsed, error));
    EXPECT_EQ(parsed.required_ios, 0u);
    // A group ID that is not two letters or digits: no maker.
    std::vector<std::uint8_t> odd = no_ios;
    odd[0x198] = 0;
    EXPECT_TRUE(parse_tmd_contents(odd.data(), odd.size(), parsed, error));
    EXPECT_TRUE(parsed.maker.empty());

    // Too small for its own count, and too small for the header.
    std::vector<std::uint8_t> cut = t;
    cut.resize(cut.size() - 1);
    EXPECT_TRUE(!parse_tmd_contents(cut.data(), cut.size(), parsed, error));
    EXPECT_TRUE(!parse_tmd_contents(t.data(), 0x100, parsed, error));
    EXPECT_TRUE(!parse_tmd_contents(nullptr, 0, parsed, error));
}

void test_paths() {
    EXPECT_EQ(nand_content_path(title(kDownloadedChannels, "WBME"), 0),
              std::string("/title/00010001/57424d45/content/00000000.app"));
    EXPECT_EQ(nand_content_path(title(kDownloadedChannels, "NAKE"), 0x1f),
              std::string("/title/00010001/4e414b45/content/0000001f.app"));
}

}  // namespace

int main() {
    test_codes();
    test_classify();
    test_tmd();
    test_paths();
    if (g_failures == 0) std::cout << "nandchannel tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
