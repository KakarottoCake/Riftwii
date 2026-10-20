// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// WiiWare and Virtual Console titles on the Wii: which titles are games,
// a stored TMD's content records, and the NAND paths of their files.
#include "riftwii/nandchannel.hpp"

#include <algorithm>
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

void test_cheat_code() {
    EXPECT_EQ(channel_cheat_code("NAAE01"), std::string("NAAE"));
    EXPECT_EQ(channel_cheat_code("WGOE5G"), std::string("WGOE"));
    EXPECT_EQ(channel_cheat_code("FABE01"), std::string("FABE"));
    // Disc games, Nintendo's own channels and homebrew keep their IDs.
    EXPECT_EQ(channel_cheat_code("RSBE01"), std::string(""));
    EXPECT_EQ(channel_cheat_code("GZLE01"), std::string(""));
    EXPECT_EQ(channel_cheat_code("HAAA01"), std::string(""));
    EXPECT_EQ(channel_cheat_code("UFTW01"), std::string(""));
    EXPECT_EQ(channel_cheat_code("NAAE"), std::string(""));
}

void test_paths() {
    EXPECT_EQ(nand_content_path(title(kDownloadedChannels, "WBME"), 0),
              std::string("/title/00010001/57424d45/content/00000000.app"));
    EXPECT_EQ(nand_content_path(title(kDownloadedChannels, "NAKE"), 0x1f),
              std::string("/title/00010001/4e414b45/content/0000001f.app"));
}

// The first 0x100 bytes of a DOL with one text section of `text` bytes
// loading at 0x80004000, its entry at the start.
std::vector<std::uint8_t> dol_head(std::uint32_t text) {
    std::vector<std::uint8_t> h(0x100, 0);
    put32(h.data(), 0x100);
    put32(h.data() + 0x48, 0x80004000);
    put32(h.data() + 0x90, text);
    put32(h.data() + 0xE0, 0x80004000);
    return h;
}

void test_program() {
    TmdContents tmd;
    tmd.boot_index = 1;
    // 0 banner, 1 a small loader, 2 the game, 3 an archive, 4 shared.
    tmd.contents = {{0, 0, 1, 0x40000}, {1, 1, 1, 0x3000}, {2, 2, 1, 0x300000}, {3, 3, 1, 0x800000},
                    {4, 4, 0x8001, 0x900000}};
    std::vector<std::uint8_t> archive(0x100, 0);
    put32(archive.data(), 0x55AA382D);
    std::vector<ContentStart> starts = {
        {0, 0x40000, dol_head(0x1000)},  // the banner is never the program, DOL or not
        {1, 0x3000, dol_head(0x2000)},
        {2, 0x300000, dol_head(0x2FF000)},
        {3, 0x800000, archive},
        {4, 0x900000, dol_head(0x8FF000)},  // shared: not the title's own
    };
    ChannelProgram program;
    std::string error;
    EXPECT_TRUE(choose_channel_program(tmd, starts, program, error));
    EXPECT_EQ(program.index, 2);
    EXPECT_TRUE(program.past_loader);
    EXPECT_EQ(program.dol.entry, 0x80004000u);

    // Only the boot content is a DOL (a Virtual Console emulator).
    starts.erase(starts.begin() + 2);
    EXPECT_TRUE(choose_channel_program(tmd, starts, program, error));
    EXPECT_EQ(program.index, 1);
    EXPECT_TRUE(!program.past_loader);

    // A header whose text runs past its file is not taken.
    starts[1].head = dol_head(0x4000);
    EXPECT_TRUE(!choose_channel_program(tmd, starts, program, error));
    EXPECT_TRUE(!error.empty());

    // The error says what each content was instead.
    std::vector<std::uint8_t> elf(0x100, 0);
    elf[0] = 0x7F;
    elf[1] = 'E';
    elf[2] = 'L';
    elf[3] = 'F';
    EXPECT_TRUE(!choose_channel_program(tmd, {{1, 0x3000, elf}, {3, 0x800000, archive}}, program, error));
    EXPECT_TRUE(error.find("1: an ELF program") != std::string::npos);
    EXPECT_TRUE(error.find("3: a U8 archive") != std::string::npos);

    // A plain DOL wins over a bigger content read only leniently (two of
    // its sections overlap).
    {
        std::vector<std::uint8_t> loose = dol_head(0x7FF000);
        put32(loose.data() + 0x90, 0x7FE000);
        put32(loose.data() + 0x04, 0x100);       // a second text section...
        put32(loose.data() + 0x4C, 0x80005000);  // ...over the first: the strict reading refuses it
        put32(loose.data() + 0x94, 0x2000);
        EXPECT_TRUE(choose_channel_program(tmd, {{1, 0x3000, dol_head(0x2000)}, {3, 0x800000, loose}}, program, error));
        EXPECT_EQ(program.index, 1);
        EXPECT_EQ(program.format, std::string("DOL"));
    }

    // A content the TMD does not list is not taken either.
    EXPECT_TRUE(!choose_channel_program(tmd, {{7, 0x300000, dol_head(0x1000)}}, program, error));
}

// A DOL header whose one text section loads at `address`, the entry at its start.
std::vector<std::uint8_t> dol_at(std::uint32_t address, std::uint32_t text_size = 0x1000) {
    std::vector<std::uint8_t> h(0x100, 0);
    put32(h.data(), 0x100);
    put32(h.data() + 0x48, address);
    put32(h.data() + 0x90, text_size);
    put32(h.data() + 0xE0, address);
    return h;
}

// A channel with a banner (content 0) and one program (content 1, the boot
// index) of `program_size` bytes.
TmdContents one_program_channel(std::uint64_t program_size) {
    TmdContents tmd;
    tmd.boot_index = 1;
    tmd.contents = {{0, 0, 1, 0x40000}, {1, 1, 1, program_size}};
    return tmd;
}

// Where the extras' blobs may start in a channel game: above RiftWii,
// and above any of the program that loads past it.
void test_extras_floor() {
    constexpr std::uint32_t kLoaderEnd = 0x81200000;
    DolHeader dol;
    // The usual program, low in MEM1 (Bit.Trip Void's starts at 0x80003400):
    // the floor is RiftWii's end.
    dol.sections[0] = DolSection{0x100, 0x80003400, 0x200000, true};
    dol.sections[7] = DolSection{0x200100, 0x80203400, 0x40000, false};
    dol.bss_address = 0x80243400;
    dol.bss_size = 0x100000;
    EXPECT_EQ(channel_extras_floor(dol, kLoaderEnd), kLoaderEnd);

    // A data section above RiftWii: the blobs go above it, 32-byte aligned.
    dol.sections[8] = DolSection{0x240100, 0x81300000, 0x1001, false};
    EXPECT_EQ(channel_extras_floor(dol, kLoaderEnd), 0x81301020u);

    // A BSS that reaches higher still.
    dol.bss_address = 0x81301020;
    dol.bss_size = 0x20000;
    EXPECT_EQ(channel_extras_floor(dol, kLoaderEnd), 0x81321020u);

    // A program up to the top of MEM1 leaves no room at all.
    dol.bss_size = 0x81800000 - 0x81301020;
    EXPECT_EQ(channel_extras_floor(dol, kLoaderEnd), 0x81800000u);

    // MEM2 parts (refused by the loader anyway) and empty sections do not count.
    DolHeader mem2;
    mem2.sections[0] = DolSection{0x100, 0x80004000, 0x1000, true};
    mem2.sections[8] = DolSection{0x1100, 0x90000000, 0x100000, false};
    mem2.sections[9] = DolSection{0, 0x81700000, 0, false};
    EXPECT_EQ(channel_extras_floor(mem2, kLoaderEnd), kLoaderEnd);

    // An unaligned end of RiftWii's memory is rounded up too.
    EXPECT_EQ(channel_extras_floor(mem2, 0x81200004), 0x81200020u);
}

}  // namespace

void test_unlisted_verdict() {
    // A forwarder: small, with an app path inside.
    std::vector<std::uint8_t> forwarder(20000, 0);
    const char path[] = "sd:/apps/Nintendont/boot.dol";
    std::copy(path, path + sizeof path - 1, forwarder.begin() + 4000);
    const TmdContents small = one_program_channel(forwarder.size());
    const ChannelVerdict fwd = judge_unlisted_channel("WWND", small, {{1, forwarder.size(), forwarder}});
    EXPECT_TRUE(fwd.left_out);
    EXPECT_TRUE(fwd.reason.find("sd:/apps/Nintendont/boot.dol") != std::string::npos);

    // A missing program: nothing read for the boot content, a boot index the
    // TMD does not list, or a shared one. Left out, whatever the code.
    EXPECT_TRUE(judge_unlisted_channel("NZLX", small, {}).left_out);
    EXPECT_TRUE(judge_unlisted_channel("NZLX", small, {}).reason.find("not on the Wii") != std::string::npos);
    EXPECT_TRUE(judge_unlisted_channel("NZLX", small, {}).rule == 2);
    EXPECT_TRUE(judge_unlisted_channel("NZLX", small, {}).unread_content_id != 0);
    EXPECT_TRUE(judge_unlisted_channel("NZLX", small, {{1, 0, {}}}).left_out);  // read, but empty
    EXPECT_TRUE(judge_unlisted_channel("NZLX", small, {{0, 0x40000, dol_at(0x80004000)}}).left_out);  // only the banner
    TmdContents no_boot = small;
    no_boot.boot_index = 5;
    EXPECT_TRUE(judge_unlisted_channel("NZLX", no_boot, {{1, forwarder.size(), forwarder}}).left_out);
    TmdContents shared = small;
    shared.contents[1].type = 0x8001;
    EXPECT_TRUE(judge_unlisted_channel("NZLX", shared, {{1, forwarder.size(), forwarder}}).left_out);

    // Retro Rewind's channel: a 3 MB DOL loading at 0x81000000. Too big to be
    // a forwarder, but homebrew by where it loads.
    const TmdContents big = one_program_channel(3u << 20);
    const ChannelVerdict loader = judge_unlisted_channel("WRET", big, {{1, 3u << 20, dol_at(0x81000000)}});
    EXPECT_TRUE(loader.left_out);
    EXPECT_TRUE(loader.reason.find("0x81000000") != std::string::npos);
    // Any section counts, a data one too (the third data slot, at 0x48 + 7*4 + ...).
    std::vector<std::uint8_t> data_high = dol_at(0x80004000);
    put32(data_high.data() + 0x1C, 0x2000);   // a data section's file offset
    put32(data_high.data() + 0x64, 0x80A00000);
    put32(data_high.data() + 0xAC, 0x100);
    EXPECT_TRUE(judge_unlisted_channel("WRET", big, {{1, 3u << 20, data_high}}).left_out);
    // The loader a past-loader program sits behind is what is judged: a small
    // boot DOL low, the real program (the biggest DOL) high.
    TmdContents behind;
    behind.boot_index = 1;
    behind.contents = {{0, 0, 1, 0x40000}, {1, 1, 1, 0x3000}, {2, 2, 1, 3u << 20}};
    EXPECT_TRUE(judge_unlisted_channel("WRET", behind, {{1, 0x3000, dol_at(0x80004000, 0x1000)},
                                                         {2, 3u << 20, dol_at(0x81000000)}})
                    .left_out);

    // A Virtual Console inject: a 3 MB DOL at 0x80004000, no path. A game.
    EXPECT_TRUE(!judge_unlisted_channel("NZLX", big, {{1, 3u << 20, dol_at(0x80004000)}}).left_out);
    // Just under the line is still a game; a bigger emulator reaching 0x809FFFFF too.
    EXPECT_TRUE(!judge_unlisted_channel("NZLX", big, {{1, 3u << 20, dol_at(0x80900000)}}).left_out);
    // Even a program holding the text of a path is a game when it is big.
    std::vector<std::uint8_t> with_path = dol_at(0x80004000);
    with_path.resize(0x1000);
    std::copy(path, path + sizeof path - 1, with_path.begin() + 0x200);
    EXPECT_TRUE(!judge_unlisted_channel("NZLX", big, {{1, 3u << 20, with_path}}).left_out);

    // 0x80003100 is libogc's default start for homebrew, but nothing shows
    // retail never uses it, so it counts as a game.
    EXPECT_TRUE(!judge_unlisted_channel("NZLX", big, {{1, 3u << 20, dol_at(0x80003100)}}).left_out);

    // A small program without any path is a game too (not a DOL, too).
    std::vector<std::uint8_t> plain(20000, 0x41);
    EXPECT_TRUE(!judge_unlisted_channel("NZLX", small, {{1, plain.size(), plain}}).left_out);

    // An ELF is judged by the path rule only.
    std::vector<std::uint8_t> elf(20000, 0);
    elf[0] = 0x7F;
    elf[1] = 'E';
    elf[2] = 'L';
    elf[3] = 'F';
    EXPECT_TRUE(!judge_unlisted_channel("NZLX", small, {{1, elf.size(), elf}}).left_out);
    std::copy(path, path + sizeof path - 1, elf.begin() + 3000);
    EXPECT_TRUE(judge_unlisted_channel("NZLX", small, {{1, elf.size(), elf}}).left_out);

    // Known homebrew codes, whatever the program.
    EXPECT_TRUE(judge_unlisted_channel("LULZ", big, {{1, 3u << 20, dol_at(0x80004000)}}).left_out);
    EXPECT_TRUE(is_homebrew_code("UFTW"));
    // The Discross channel: a small, plain program that no section rule catches.
    EXPECT_TRUE(is_homebrew_code("CORD"));
    {
        const ChannelVerdict cord = judge_unlisted_channel("CORD", small, {{1, plain.size(), plain}});
        EXPECT_TRUE(cord.left_out);
        EXPECT_TRUE(cord.rule == 1);
    }
    // A GameTDB-listed game never reaches the verdict, and its code is no homebrew's.
    EXPECT_TRUE(!is_homebrew_code("WBME"));

    // The path shown is bounded.
    std::vector<std::uint8_t> endless(5000, 'a');
    std::copy(path, path + sizeof path - 1, endless.begin() + 2500);
    EXPECT_TRUE(forwarder_app_path(endless.data(), endless.size()).size() <= 60);
    EXPECT_EQ(forwarder_app_path(nullptr, 0), std::string());
}

int main() {
    test_extras_floor();
    test_codes();
    test_classify();
    test_tmd();
    test_paths();
    test_program();
    test_cheat_code();
    test_unlisted_verdict();
    if (g_failures == 0) std::cout << "nandchannel tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
