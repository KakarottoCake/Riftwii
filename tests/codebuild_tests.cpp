// SPDX-License-Identifier: GPL-3.0-or-later
// Code builds: gameconfig.txt, the code handler's hooks and relocated code
// list, joined code lists, and code builds in the launch model.
#include "riftwii/cheats.hpp"
#include "riftwii/codehook.hpp"
#include "riftwii/gameconfig.hpp"
#include "riftwii/launch.hpp"

#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;
using Bytes = std::vector<std::uint8_t>;

namespace {

void put(Bytes& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v >> 24));
    b.push_back(static_cast<std::uint8_t>(v >> 16));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
    b.push_back(static_cast<std::uint8_t>(v));
}

std::uint32_t get(const Bytes& b, std::size_t at) {
    return (std::uint32_t(b[at]) << 24) | (std::uint32_t(b[at + 1]) << 16) | (std::uint32_t(b[at + 2]) << 8) | b[at + 3];
}

Bytes gct(std::initializer_list<std::uint32_t> words) {
    Bytes b;
    put(b, 0x00D0C0DE);
    put(b, 0x00D0C0DE);
    for (std::uint32_t w : words) put(b, w);
    put(b, 0xF0000000);
    put(b, 0);
    return b;
}

// A Project+ build's gameconfig.txt, as it ships.
const char* kProjectPlusConfig =
    "RSBE01:\n"
    "codeliststart = 80566528\n"
    "codelistend = 80597800\n"
    "hooktype = 7\n"
    "poke(800042B8, 60000000)\n"
    "pokeifequal(803E9930, 4BFECA1D, 803E9930, 60000000)\n";

void test_gameconfig() {
    GameConfig c = parse_gameconfig(kProjectPlusConfig, "RSBE01");
    EXPECT_TRUE(c.found);
    EXPECT_EQ(c.codelist_start, 0x80566528u);
    EXPECT_EQ(c.codelist_end, 0x80597800u);
    EXPECT_EQ(c.hooktype, 7);
    EXPECT_EQ(c.pokes.size(), 2u);
    EXPECT_FALSE(c.pokes[0].conditional);
    EXPECT_EQ(c.pokes[0].address, 0x800042B8u);
    EXPECT_EQ(c.pokes[0].value, 0x60000000u);
    EXPECT_TRUE(c.pokes[1].conditional);
    EXPECT_EQ(c.pokes[1].check_address, 0x803E9930u);
    EXPECT_EQ(c.pokes[1].check_value, 0x4BFECA1Du);
    EXPECT_TRUE(c.ignored.empty());
    // Another game: nothing.
    c = parse_gameconfig(kProjectPlusConfig, "RMCE01");
    EXPECT_FALSE(c.found);
    EXPECT_TRUE(c.pokes.empty());
    // Sections by prefix, comments, 0x, CRLF, unknown lines kept aside.
    c = parse_gameconfig("# hello\r\nRSB:\r\nhooktype = 1 # retrace\r\ncodeliststart = 0x80570000\r\nfoo = bar\r\n"
                         "RMCE01:\r\npoke(80001000, 1)\r\n",
                         "RSBE01");
    EXPECT_TRUE(c.found);
    EXPECT_EQ(c.hooktype, 1);
    EXPECT_EQ(c.codelist_start, 0x80570000u);
    EXPECT_TRUE(c.pokes.empty());
    EXPECT_EQ(c.ignored.size(), 1u);
    // Project+'s gc.txt: '?' matches any character.
    c = parse_gameconfig("RSBE??:\ncodeliststart = 80566528\ncodelistend = 80580000\nhooktype = 7\n", "RSBE01");
    EXPECT_TRUE(c.found);
    EXPECT_EQ(c.codelist_end, 0x80580000u);
    EXPECT_FALSE(parse_gameconfig("RSBE??:\nhooktype = 7\n", "RSBP01").found);
    EXPECT_FALSE(parse_gameconfig("RSBE??:\nhooktype = 7\n", "RSBE").found);
    // A bad poke is set aside, not half read.
    c = parse_gameconfig("RSBE01:\npoke(80001000)\npoke(8000100G, 1)\n", "RSBE01");
    EXPECT_TRUE(c.pokes.empty());
    EXPECT_EQ(c.ignored.size(), 2u);
}

void test_hooks() {
    // A made-up text: the audio frame pattern, then a few words, then blr.
    Bytes text;
    put(text, 0x60000000);
    for (std::uint32_t w : {0x3800000Eu, 0x7FE3FB78u, 0xB0050000u, 0x38800080u, 0x60000000u, 0x4E800020u}) put(text, w);
    put(text, 0x7CE33B78);  // the retrace pattern's first word only
    std::vector<CodeRange> ranges{{0x80200000, text.data(), text.size()}};
    EXPECT_EQ(find_code_hook(ranges, CodeHook::AudioFrame), 0x80200018u);
    EXPECT_EQ(find_code_hook(ranges, CodeHook::Retrace), 0u);
    EXPECT_EQ(find_cheat_hook(ranges), 0u);
    Bytes vi;
    for (std::uint32_t w : {0x7CE33B78u, 0x38870034u, 0x38A70038u, 0x38C7004Cu, 0x4E800020u}) put(vi, w);
    std::vector<CodeRange> vr{{0x801E99F4, vi.data(), vi.size()}};
    EXPECT_EQ(find_cheat_hook(vr), 0x801E9A04u);
    EXPECT_EQ(find_code_hook(vr, CodeHook::AudioFrame), 0u);
}

void test_dol_jumps() {
    Bytes text;
    put(text, 0x60000000);
    for (std::uint32_t w : {0x7C0004ACu, 0x4C00012Cu, 0x7FE903A6u, 0x4E800420u}) put(text, w);
    for (std::uint32_t w : {0x7C0004ACu, 0x4C00012Cu, 0x7FE903A6u, 0x4E800421u}) put(text, w);  // bctrl: not it
    std::vector<CodeRange> ranges{{0x80300000, text.data(), text.size()}};
    const std::vector<std::uint32_t> jumps = find_dol_jumps(ranges);
    EXPECT_EQ(jumps.size(), 1u);
    if (!jumps.empty()) EXPECT_EQ(jumps[0], 0x80300010u);
    EXPECT_EQ(encode_b(0x80300010, kDolSwitchStub), 0x48000000u | ((kDolSwitchStub - 0x80300010u) & 0x03FFFFFCu));
    EXPECT_EQ(code_hook_pattern(CodeHook::Retrace)[0], 0x7CE33B78u);
    EXPECT_EQ(code_hook_pattern(CodeHook::AudioFrame)[0], 0x3800000Eu);
}

void test_relocate() {
    Bytes handler(0x200, 0);
    handler[0x104] = 0x3D; handler[0x105] = 0xE0; handler[0x106] = 0x80; handler[0x107] = 0x00;
    handler[0x108] = 0x61; handler[0x109] = 0xEF; handler[0x10A] = 0x22; handler[0x10B] = 0xA8;
    EXPECT_TRUE(relocate_code_list(handler.data(), handler.size(), 0x80566528));
    EXPECT_EQ(get(handler, 0x104), 0x3DE08056u);
    EXPECT_EQ(get(handler, 0x108), 0x61EF6528u);
    // Not the expected instructions any more: refused, untouched.
    const Bytes before = handler;
    EXPECT_FALSE(relocate_code_list(handler.data(), handler.size(), 0x80570000));
    EXPECT_TRUE(handler == before);
    EXPECT_FALSE(relocate_code_list(handler.data(), 0x100, 0x80570000));
}

void test_join() {
    const Bytes a = gct({0x04000000, 0x1}), b = gct({0x04000004, 0x2, 0x04000008, 0x3});
    EXPECT_TRUE(valid_gct(a));
    EXPECT_TRUE(valid_gct(b));
    const Bytes j = join_gct(a, b);
    EXPECT_TRUE(valid_gct(j));
    EXPECT_EQ(j.size(), a.size() + b.size() - 16);
    EXPECT_EQ(get(j, 8), 0x04000000u);
    EXPECT_EQ(get(j, 16), 0x04000004u);
    EXPECT_TRUE(join_gct(a, {}) == a);
    EXPECT_TRUE(join_gct({}, b) == b);
    Bytes bad = a;
    bad.pop_back();
    EXPECT_FALSE(valid_gct(bad));
    EXPECT_FALSE(valid_gct(Bytes{0, 0xD0, 0xC0, 0xDE}));
}

void test_model() {
    DiscIdentity brawl;
    brawl.id = "RSBE01";
    LaunchModel m;
    m.add_code_build("rex_/RSBE01.GCT", "sd:/rex_", "sd:/rex_/RSBE01.GCT", "RSBE01", &brawl);
    m.add_code_build("pm/RMCE01.GCT", "sd:/pm", "sd:/pm/RMCE01.GCT", "RMCE01", &brawl);
    EXPECT_EQ(m.packages.size(), 2u);
    EXPECT_TRUE(m.packages[0].code_build());
    EXPECT_TRUE(show_package(m.packages[0]));
    EXPECT_FALSE(show_package(m.packages[1]));  // another game's
    EXPECT_TRUE(m.code_builds().empty());
    EXPECT_TRUE(m.set_enabled(0, true));
    EXPECT_FALSE(m.set_enabled(1, true));
    EXPECT_EQ(m.code_builds().size(), 1u);
    EXPECT_EQ(m.code_builds()[0]->gct_path, std::string("sd:/rex_/RSBE01.GCT"));
    // Never compiled: no Riivolution work, so a plain boot.
    EXPECT_TRUE(m.selections().empty());
    EXPECT_FALSE(needs_launch_pipeline(!m.selections().empty(), m.save_mode));
    // Remembered like a pack.
    const std::string saved = m.save();
    LaunchModel again;
    again.add_code_build("rex_/RSBE01.GCT", "sd:/rex_", "sd:/rex_/RSBE01.GCT", "RSBE01", &brawl);
    again.restore(saved);
    EXPECT_EQ(again.code_builds().size(), 1u);

    PackIndex index;
    EXPECT_FALSE(index.has_packs("RSBE01"));
    index.add_game("RSBE01");
    EXPECT_TRUE(index.has_packs("RSBE01"));
    EXPECT_FALSE(index.has_packs("RMCE01"));
}

}  // namespace

int main() {
    test_gameconfig();
    test_hooks();
    test_dol_jumps();
    test_relocate();
    test_join();
    test_model();
    if (g_failures == 0) std::cout << "codebuild tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
