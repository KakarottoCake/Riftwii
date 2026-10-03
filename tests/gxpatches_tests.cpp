// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// USB Loader GX's game fixes (riftwii/gxpatches.hpp) on made-up memory: each
// writes what GX writes, where GX writes it, and nothing outside the loaded
// parts. With the GX source next to the repository (../usbloadergx-riftwii),
// Kirby's table is also checked word for word against kirbypatch.c.
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "riftwii/gxpatches.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

// Game memory from `base`, `size` bytes, as one loaded part.
struct Memory {
    std::uint32_t base;
    std::vector<std::uint8_t> bytes;
    Memory(std::uint32_t b, std::size_t size, std::uint8_t fill = 0) : base(b), bytes(size, fill) {}
    std::vector<CodeSpan> spans() { return {CodeSpan{bytes.data(), bytes.size(), base}}; }
    std::uint32_t get(std::uint32_t a) const {
        const std::uint8_t* p = &bytes[a - base];
        return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
    }
    void set(std::uint32_t a, std::uint32_t v) {
        std::uint8_t* p = &bytes[a - base];
        p[0] = std::uint8_t(v >> 24); p[1] = std::uint8_t(v >> 16); p[2] = std::uint8_t(v >> 8); p[3] = std::uint8_t(v);
    }
};

void test_protected() {
    EXPECT_TRUE(gx_protected_game("SPXE41"));
    EXPECT_TRUE(gx_protected_game("RPWE41"));
    EXPECT_TRUE(gx_protected_game("SDVE41"));
    EXPECT_TRUE(gx_protected_game("STNP41"));
    EXPECT_TRUE(gx_protected_game("SLVP41"));
    EXPECT_FALSE(gx_protected_game("SLVE41"));  // GX names only the PAL We Dare
    EXPECT_FALSE(gx_protected_game("SUKE01"));  // patched instead
    EXPECT_TRUE(gx_kirby_game("SUKE01"));
    EXPECT_FALSE(gx_kirby_game("SUZE01"));
    EXPECT_FALSE(gx_protected_game("RMCE01"));
    EXPECT_TRUE(game_clears_mem1_top("RB4E08"));
    EXPECT_TRUE(game_clears_mem1_top("RB4P08"));
    EXPECT_FALSE(game_clears_mem1_top("RMCE01"));
}

// Kirby: every word kirbypatch.c stores, read back from memory.
void test_kirby_against_gx() {
    // GX_SOURCE: a USB Loader GX checkout; else one beside the repository
    // (the tests run in the build folder, one level down).
    std::ifstream in;
    if (const char* gx = std::getenv("GX_SOURCE")) in.open(std::string(gx) + "/source/patches/kirbypatch.c");
    for (const char* up : {"../", "../../", "../../../"}) {
        if (!in.is_open()) in.open(std::string(up) + "usbloadergx-riftwii/source/patches/kirbypatch.c");
    }
    if (!in) {
        std::cout << "  (kirbypatch.c not found next to the repository: table checked by count only)" << std::endl;
        Memory m(0x80004000, 0x00800000);
        GxReport r;
        DolHeader dol;
        EXPECT_EQ(gx_game_patches("SUKE01", dol, m.spans(), r), 1392u);
        return;
    }
    std::stringstream text;
    text << in.rdbuf();
    const std::string source = text.str();
    const char* ids[] = {"SUKE01", "SUKP01", "SUKJ01", "SUKK01"};
    for (int k = 0; k < 4; ++k) {
        const std::size_t from = source.find(std::string("\"") + ids[k] + "\"");
        const std::size_t to = k < 3 ? source.find(std::string("\"") + ids[k + 1] + "\"") : source.size();
        EXPECT_TRUE(from != std::string::npos);
        const std::string part = source.substr(from, to - from);
        Memory m(0x80004000, 0x00800000, 0xEE);
        GxReport r;
        DolHeader dol;
        const unsigned done = gx_game_patches(ids[k], dol, m.spans(), r);
        std::regex store(R"(\*\(u32 \*\)0x([0-9A-Fa-f]{8}) = 0x([0-9A-Fa-f]{8});)");
        unsigned count = 0, wrong = 0;
        for (auto it = std::sregex_iterator(part.begin(), part.end(), store); it != std::sregex_iterator(); ++it) {
            const std::uint32_t a = std::stoul((*it)[1].str(), nullptr, 16);
            const std::uint32_t v = std::stoul((*it)[2].str(), nullptr, 16);
            ++count;
            if (m.get(a) != v) ++wrong;
        }
        EXPECT_EQ(count, 1392u);
        EXPECT_EQ(done, count);
        EXPECT_EQ(wrong, 0u);
    }
}

void test_outside_untouched() {
    // Only half of Kirby's range is loaded: the rest is skipped, not written.
    Memory m(0x80004000, 0x00300000);
    GxReport r;
    DolHeader dol;
    const unsigned done = gx_game_patches("SUKE01", dol, m.spans(), r);
    EXPECT_TRUE(done > 0 && done < 1392u);
    EXPECT_TRUE(r.notes.size() == 1 && r.notes[0].find("outside the game") != std::string::npos);
}

void test_re4() {
    Memory m(0x80100000, 0x00100000);
    GxReport r;
    DolHeader dol;
    EXPECT_EQ(gx_game_patches("RB4P08", dol, m.spans(), r), 1u);
    EXPECT_EQ(m.get(0x8016B094), 0x38600001u);
    EXPECT_EQ(gx_game_patches("RB4J08", dol, m.spans(), r), 0u);
}

// A pack's own main.dol: no fixed-address patch lands in it.
void test_own_executable() {
    Memory m(0x80004000, 0x00800000, 0x11);
    GxReport r;
    DolHeader dol;
    EXPECT_EQ(gx_game_patches("SUKE01", dol, m.spans(), r, true), 0u);
    EXPECT_EQ(m.get(0x80175194), 0x11111111u);
    EXPECT_TRUE(!r.notes.empty() && r.notes[0].find("own main.dol") != std::string::npos);
    EXPECT_EQ(gx_game_patches("RB4E08", dol, m.spans(), r, true), 0u);
    EXPECT_EQ(m.get(0x8016B260), 0x11111111u);
}

// WIP: file offset 0x100 is the first section's first byte.
void test_wip_nsmb() {
    DolHeader dol;
    dol.sections[0] = DolSection{0x100, 0x80004000, 0x100000, true};
    dol.sections[1] = DolSection{0x100100, 0x80200000, 0x200000, true};
    Memory m(0x80004000, 0x00400000);
    // Offset 0x1AB610 = 0x100 + 0x100000 + 0xAB510: section 1, 0x802AB510.
    m.set(0x802AB510, 0x9421FFD0);
    m.set(0x802CEC50, 0x000000DA);  // 0x1CED53 is 3 bytes in
    GxReport r;
    std::vector<CodeSpan> spans = m.spans();
    const unsigned done = gx_game_patches("SMNE01", dol, spans, r);
    EXPECT_EQ(m.get(0x802AB510), 0x4E800020u);
    EXPECT_EQ(m.get(0x802CEC50) & 0xFFu, 0x71u);
    EXPECT_TRUE(done >= 5);
    EXPECT_TRUE(!r.notes.empty() && r.notes[0].find("not as expected") != std::string::npos);  // third one absent
}

void test_anti_002() {
    Memory m(0x80004000, 0x1000);
    m.set(0x80004100, 0x2C000000);
    m.set(0x80004104, 0x48000214);
    m.set(0x80004108, 0x3C608000);
    GxReport r;
    DolHeader dol;
    EXPECT_EQ(gx_game_patches("RMCE01", dol, m.spans(), r), 1u);
    EXPECT_EQ(m.get(0x80004104), 0x40820214u);
}

void test_sd_card() {
    Memory m(0x80004000, 0x00400000);
    GxReport r;
    EXPECT_EQ(gx_sd_card_patches("REXP01", m.spans(), r), 1u);
    EXPECT_EQ(m.get(0x800ba358), 0x4800014cu);
    EXPECT_EQ(gx_sd_card_patches("SUKJ01", m.spans(), r), 2u);
    EXPECT_EQ(m.get(0x8022c66c), 0x60000000u);
    EXPECT_EQ(m.get(0x8022c6a4), 0x60000000u);
    EXPECT_EQ(gx_sd_card_patches("RMCE01", m.spans(), r), 0u);
}

void test_480p() {
    Memory m(0x80004000, 0x2000);
    // The MKW block: bl, the six words, then a bl two words after.
    const std::uint32_t at = 0x80004400;
    m.set(at - 4, 0x4bffe30d);
    const std::uint32_t block[6] = {0x38000065, 0x9b810019, 0x38810018, 0x386000e0, 0x98010018, 0x38a00002};
    for (int i = 0; i < 6; ++i) m.set(at + 4 * i, block[i]);
    m.set(at + 32, 0x4bffe73d);
    // The spare room: lwz r0,0(r30); lis r3,0x8000; lwz..., li r0,1 at +36.
    const std::uint32_t safe = 0x80005000;
    m.set(safe, 0x801E0000);
    m.set(safe + 4, 0x3C608000);
    m.set(safe + 8, 0x83000000);
    m.set(safe + 36, 0x38000001);
    GxReport r;
    EXPECT_TRUE(gx_fix_480p(m.spans(), r));
    const std::uint32_t patch = safe + 36 + 32;
    EXPECT_EQ(m.get(patch), 0x38600003u);
    EXPECT_EQ(m.get(patch + 4), 0x98610019u);
    EXPECT_EQ(m.get(at + 4), 0x48000000u + ((patch - (at + 4)) & 0x3ffffffu));
    EXPECT_EQ(m.get(patch + 8), 0x48000000u + (((at + 8) - (patch + 8)) & 0x3ffffffu));
    Memory none(0x80004000, 0x1000);
    EXPECT_FALSE(gx_fix_480p(none.spans(), r));
}

void test_region_video_fix() {
    Memory m(0x80004000, 0x1000);
    const std::uint32_t at = 0x80004100;
    m.set(at, 0x4182000C);
    m.set(at + 4, 0x4180001C);
    m.set(at + 8, 0x48000018);
    m.set(at + 40, 0x5400FFFE);
    m.set(at + 80, 0x5400FFFE);  // only the first read after the switch
    GxReport r;
    EXPECT_EQ(gx_region_video_fix('E', m.spans(), r), 1u);
    EXPECT_EQ(m.get(at + 40), 0x38000000u);
    EXPECT_EQ(m.get(at + 80), 0x5400FFFEu);
    m.set(at + 40, 0x5400FFFE);
    EXPECT_EQ(gx_region_video_fix('J', m.spans(), r), 1u);
    EXPECT_EQ(m.get(at + 40), 0x38000001u);
    m.set(at + 40, 0x5400FFFE);
    EXPECT_EQ(gx_region_video_fix('P', m.spans(), r), 0u);  // PAL: left alone, as GX does
    EXPECT_EQ(m.get(at + 40), 0x5400FFFEu);
    Memory none(0x80004000, 0x100);
    none.set(0x80004000 + 0x40, 0x5400FFFE);  // the read without the switch
    EXPECT_EQ(gx_region_video_fix('E', none.spans(), r), 0u);
    EXPECT_EQ(none.get(0x80004040), 0x5400FFFEu);
}

void test_pick_cios() {
    std::string why;
    const std::vector<D2xSlot> slots = {{249, 56}, {250, 57}, {251, 58}};
    EXPECT_EQ(gx_pick_cios("RMCE01", 36, slots, false, why), 249);  // closest above 36
    why.clear();
    EXPECT_EQ(gx_pick_cios("SUKE01", 56, slots, false, why), 249);
    EXPECT_EQ(gx_pick_cios("SOUE01", 58, slots, false, why), 251);
    EXPECT_EQ(gx_pick_cios("XXXE01", 61, slots, false, why), 251);  // above all: the highest
    const std::vector<D2xSlot> with38 = {{248, 38}, {249, 56}, {250, 57}};
    why.clear();
    EXPECT_EQ(gx_pick_cios("RSBE01", 36, with38, false, why), 249);  // Brawl: 56, not 38
    EXPECT_EQ(gx_pick_cios("RMCE01", 36, with38, false, why), 248);
    why.clear();
    EXPECT_EQ(gx_pick_cios("RMCE01", 36, with38, true, why), 249);  // SD: 56-60 only
    EXPECT_TRUE(why.find("56-60") != std::string::npos);
    why.clear();
    EXPECT_EQ(gx_pick_cios("RMCE01", 36, {{248, 38}}, true, why), 0);
    why.clear();
    EXPECT_EQ(gx_pick_cios("SBVE78", 53, with38, false, why), 248);  // no 53, no 58: 38
    EXPECT_EQ(gx_pick_cios("SBVE78", 53, slots, false, why), 251);   // 58
    EXPECT_EQ(gx_pick_cios("RMCE01", 36, {{249, 56}, {250, 56}}, false, why), 249);  // duplicate base
    EXPECT_EQ(gx_pick_cios("RMCE01", 0, slots, false, why), 0);  // nothing asked
}

}  // namespace

int main() {
    test_protected();
    test_kirby_against_gx();
    test_outside_untouched();
    test_re4();
    test_own_executable();
    test_wip_nsmb();
    test_anti_002();
    test_sd_card();
    test_480p();
    test_region_video_fix();
    test_pick_cios();
    if (g_failures == 0) {
        std::cout << "ALL GXPATCHES TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
