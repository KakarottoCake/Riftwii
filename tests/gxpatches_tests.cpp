// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// USB Loader GX's game fixes (riftwii/gxpatches.hpp) on made-up memory: each
// writes what GX writes, where GX writes it, and nothing outside the loaded
// parts. With the GX source next to the repository (../usbloadergx-riftwii),
// Kirby's table is also checked word for word against kirbypatch.c.
#include <algorithm>
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
    EXPECT_TRUE(game_clears_mem1_top("RLGE64"));
    EXPECT_TRUE(game_clears_mem1_top("RLIE64"));
    EXPECT_TRUE(game_clears_mem1_top("RL4P64"));
    EXPECT_TRUE(game_clears_mem1_top("RLBEWR"));
    EXPECT_TRUE(game_clears_mem1_top("RGQE70"));  // Ghostbusters: the hotel's PKE tutorial
    EXPECT_TRUE(game_clears_mem1_top("RJ2E52"));  // 007 Quantum of Solace: out of memory
    EXPECT_TRUE(game_clears_mem1_top("RJ2P52"));
    EXPECT_FALSE(game_clears_mem1_top("RJ2E01"));  // another publisher's RJ2
    EXPECT_FALSE(game_runs_no_mem2_code("RJ2E52"));
    EXPECT_TRUE(game_runs_no_mem2_code("RGQE70"));
    EXPECT_FALSE(game_runs_no_mem2_code("RB4E08"));
    EXPECT_TRUE(game_needs_most_of_mem2("SMNE01"));
    EXPECT_TRUE(game_needs_most_of_mem2("SMNP01"));
    EXPECT_FALSE(game_needs_most_of_mem2("RMCE01"));
    // Mario Party 4 to 7 and DDR Mario Mix, in every region, keep their
    // ARAM addresses; Super Mario Sunshine and Melee have them raised.
    EXPECT_TRUE(gc_keeps_aram_addresses("GMPE01"));
    EXPECT_TRUE(gc_keeps_aram_addresses("GMPP01"));
    EXPECT_TRUE(gc_keeps_aram_addresses("GMPJ01"));
    EXPECT_TRUE(gc_keeps_aram_addresses("GP5E01"));
    EXPECT_TRUE(gc_keeps_aram_addresses("GP6P01"));
    EXPECT_TRUE(gc_keeps_aram_addresses("GP7J01"));
    EXPECT_TRUE(gc_keeps_aram_addresses("GWZE01"));
    EXPECT_TRUE(gc_keeps_aram_addresses("GWYE41"));  // Splinter Cell: Double Agent, VMInit's 16 MB minus the base
    EXPECT_FALSE(gc_keeps_aram_addresses("GMSE01"));
    {
        const std::vector<GcCodeFix> fixes = gc_code_fixes("GEDE01");
        EXPECT_EQ(fixes.size(), 1u);
        if (fixes.size() == 1u) {
            EXPECT_EQ(fixes[0].address, 0x801EB0E8u);
            EXPECT_EQ(fixes[0].expected, 0x38A00001u);     // li r5,1
            EXPECT_EQ(fixes[0].replacement, 0x38A00000u);  // li r5,0
        }
        EXPECT_TRUE(gc_code_fixes("GEDP01").empty());  // other regions: their addresses are not known
        EXPECT_TRUE(gc_code_fixes("GMSE01").empty());
    }
    EXPECT_FALSE(gc_keeps_aram_addresses("GALE01"));
    EXPECT_TRUE(gc_loads_use_queued_heap("GLMP01"));
    EXPECT_TRUE(gc_loads_use_queued_heap("GLME01"));
    EXPECT_FALSE(gc_loads_use_queued_heap("GMSE04"));
    EXPECT_TRUE(gc_restart_known_to_work("GMSE01"));
    EXPECT_TRUE(gc_restart_known_to_work("GMSE04"));
    EXPECT_TRUE(gc_restart_known_to_work("GKDP01"));
    EXPECT_FALSE(gc_restart_known_to_work("GKDJ01"));  // no restart run for these regions
    EXPECT_FALSE(gc_restart_known_to_work("GKDE01"));
    EXPECT_FALSE(gc_restart_known_to_work("GLMP01"));
    EXPECT_FALSE(gc_restart_known_to_work("GMSP01"));  // not run with it yet
    EXPECT_FALSE(gc_restart_known_to_work("GMSJ01"));
    EXPECT_FALSE(game_clears_mem1_top("RLIL78"));  // Guitar Hero 2nd Custom
    EXPECT_FALSE(game_clears_mem1_top("SC4E64"));  // LEGO Star Wars III starts
    EXPECT_FALSE(game_clears_mem1_top("R25EWR"));  // LEGO Harry Potter starts
    EXPECT_TRUE(gx_protected_game("SDUX41"));   // The Smurfs: Dance Party
    EXPECT_FALSE(gx_protected_game("SDUEO1"));  // a Super Mario Bros. hack
    EXPECT_EQ(game_start_file("RNFE69"), 1079u);
    EXPECT_EQ(game_start_file("RNFP69"), 1079u);
    EXPECT_EQ(game_start_file("REDE41"), 1957u);
    EXPECT_EQ(game_start_file("RF8X69"), 464u);
    EXPECT_EQ(game_start_file("RMCE01"), 0u);
    EXPECT_EQ(game_start_file("RNF"), 0u);  // a short ID matches nothing
}

void test_patch_policy() {
    // The two system/game-specific exceptions must not grow to include
    // data patches: Prince of Persia also checks its video tables.
    const std::string protected_ids[] = {
        "SPXP41", "SPXE41", "RPWZ41", "RPWE41", "SDVE41", "STNP41", "SLVP41", "SDUX41"
    };
    for (int value = 0; value <= static_cast<int>(GamePatchKind::Debug); ++value) {
        const GamePatchKind kind = static_cast<GamePatchKind>(value);
        const bool safe = kind == GamePatchKind::PerGameFix || kind == GamePatchKind::SystemGlobals;
        for (const std::string& id : protected_ids) {
            EXPECT_EQ(gx_patch_allowed(id, kind), safe);
        }
        // Kirby's GX patch removes its checks; its extras remain available.
        for (const char* id : {"RMCE01", "SUKE01", "SUKP01", "SUKJ01", "SUKK01", "SLVE41", "SDUEO1", ""}) {
            EXPECT_TRUE(gx_patch_allowed(id, kind));
        }
    }
    EXPECT_FALSE(gx_patch_allowed("RPWZ41", static_cast<GamePatchKind>(999)));
    EXPECT_FALSE(gx_patch_allowed("RMCE01", static_cast<GamePatchKind>(999)));
}

void test_protected_game_fix() {
    // The five checked WIP writes still apply, but a matching generic
    // anti-002 instruction beside them must stay byte-for-byte unchanged.
    const std::uint32_t offsets[] = {0x6A, 0x75, 0x82, 0x92, 0x9D};
    const std::uint32_t originals[] = {0x7A6B6F6A, 0x7C7A6939, 0x7376686B, 0x80717570, 0x82806F3F};
    const std::uint32_t replacements[] = {0x6F6A7A6B, 0x69397C7A, 0x686B7376, 0x75708071, 0x6F3F8280};
    for (const char* id : {"SPXP41", "RPWZ41"}) {
        Memory memory(0x80004000, 0x200, 0x11);
        DolHeader dol;
        // WIP positions count loaded section sizes after the 0x100-byte
        // header, rather than seeking by each section's file offset.
        dol.sections[0] = DolSection{0x100, 0x90004000, 0x007AAB00, true};
        dol.sections[1] = DolSection{0x007AAC00, memory.base, 0x200, true};
        for (unsigned i = 0; i < 5; ++i) memory.set(memory.base + offsets[i], originals[i]);
        memory.set(memory.base + 0x100, 0x2C000000);
        memory.set(memory.base + 0x104, 0x48000214);
        memory.set(memory.base + 0x108, 0x3C608000);
        std::vector<std::uint8_t> expected = memory.bytes;
        for (unsigned i = 0; i < 5; ++i) {
            for (unsigned byte = 0; byte < 4; ++byte)
                expected[offsets[i] + byte] = static_cast<std::uint8_t>(replacements[i] >> (24 - byte * 8));
        }
        GxReport report;
        EXPECT_EQ(gx_game_patches(id, dol, memory.spans(), report), 20u);
        EXPECT_TRUE(memory.bytes == expected);
        EXPECT_TRUE(std::any_of(report.notes.begin(), report.notes.end(), [](const std::string& note) {
            return note.find("Error #002 code fix: not applied") != std::string::npos;
        }));
    }
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

// The four per-game preferences, each at the first match of its part.
void test_preferences() {
    Memory m(0x80004000, 0x1000);
    const std::uint32_t speaker = 0x80004100;
    const std::uint32_t kSpeaker[4] = {0x9421FA00, 0x7C0802A6, 0x90010604, 0x39610600};
    for (int i = 0; i < 4; ++i) m.set(speaker + 4 * i, kSpeaker[i]);
    for (int i = 0; i < 4; ++i) m.set(speaker + 0x40 + 4 * i, kSpeaker[i]);  // a second: GX stops at the first
    GxReport r;
    EXPECT_EQ(gx_speaker_off(m.spans(), r), 1u);
    EXPECT_EQ(m.get(speaker), 0x4E800020u);
    EXPECT_EQ(m.get(speaker + 0x40), 0x9421FA00u);

    const std::uint32_t motor = 0x80004400;
    m.set(motor, 0x9421FFF0);
    m.set(motor + 4, 0x7C0802A6);
    const std::uint32_t b[4] = {0x2C000000, 0x40820020, 0x2C1E0000, 0x40820010};
    const std::uint32_t c[5] = {0x48000020, 0x7C9E00D0, 0x38000001, 0x7C84F378, 0x54840FFE};
    for (int i = 0; i < 4; ++i) m.set(motor + 68 + 4 * i, b[i]);
    EXPECT_EQ(gx_rumble_off(m.spans(), r), 0u);  // the third part is missing
    for (int i = 0; i < 5; ++i) m.set(motor + 148 + 4 * i, c[i]);
    EXPECT_EQ(gx_rumble_off(m.spans(), r), 1u);
    EXPECT_EQ(m.get(motor), 0x4E800020u);

    const std::uint32_t aspect = 0x80004800;
    const std::uint32_t first[5] = {0x9421FFF0, 0x7C0802A6, 0x38800001, 0x90010014, 0x38610008};
    const std::uint32_t second[15] = {0x2C030000, 0x40820010, 0x38000000, 0x98010008, 0x48000018,
                                      0x88010008, 0x28000001, 0x4182000C, 0x38000000, 0x98010008,
                                      0x80010014, 0x88610008, 0x7C0803A6, 0x38210010, 0x4E800020};
    for (int i = 0; i < 5; ++i) m.set(aspect + 4 * i, first[i]);
    m.set(aspect + 20, 0x4BFFFFFF);  // the call between them, any word
    for (int i = 0; i < 15; ++i) m.set(aspect + 24 + 4 * i, second[i]);
    EXPECT_EQ(gx_force_aspect(true, m.spans(), r), 1u);
    EXPECT_EQ(m.get(aspect + 0x44), 0x38600001u);
    EXPECT_EQ(gx_force_aspect(false, m.spans(), r), 0u);  // its pattern is changed now
    m.set(aspect + 0x44, second[11]);
    EXPECT_EQ(gx_force_aspect(false, m.spans(), r), 1u);
    EXPECT_EQ(m.get(aspect + 0x44), 0x38600000u);

    // Country strings: a US console, a European game.
    Memory t(0x80008000, 0x40);
    t.set(0x80008000, 0x01555300);  // "\1US\0": changed
    t.set(0x80008008, 0x01555300);
    t.set(0x80008010, 0x02555300);  // another number: left
    EXPECT_EQ(gx_country_strings(1, 'P', t.spans(), r), 2u);
    EXPECT_EQ(t.get(0x80008000), 0x01455500u);
    EXPECT_EQ(t.get(0x80008008), 0x01455500u);
    EXPECT_EQ(t.get(0x80008010), 0x02555300u);
    EXPECT_EQ(gx_country_strings(2, 'D', t.spans(), r), 0u);  // a German game on a PAL Wii
    Memory j(0x80008000, 0x10);
    j.set(0x80008004, 0x004A5000);  // a Japanese console's "\0JP\0", a US game
    EXPECT_EQ(gx_country_strings(0, 'E', j.spans(), r), 1u);
    EXPECT_EQ(j.get(0x80008004), 0x00555300u);
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

void test_keyboard() {
    EXPECT_TRUE(game_uses_usb_keyboard("RUUE01"));
    EXPECT_TRUE(game_uses_usb_keyboard("RMHP08"));
    EXPECT_TRUE(!game_uses_usb_keyboard("RMCE01"));
    EXPECT_TRUE(ios_has_usb_keyboard(38));
    EXPECT_TRUE(ios_has_usb_keyboard(56));
    EXPECT_TRUE(!ios_has_usb_keyboard(57));
    EXPECT_TRUE(!ios_has_usb_keyboard(58));
    EXPECT_TRUE(ios_has_usb_keyboard(61));
    EXPECT_TRUE(!ios_has_usb_keyboard(0));
    // The tester's Wii: 249 (56) first, as GX picks it; then base 38 before
    // 57 and 58 without the SD card.
    const std::vector<D2xSlot> slots = {{248, 38}, {249, 56}, {250, 57}, {251, 58}};
    const std::vector<int> order = {249, 250, 251};
    EXPECT_TRUE((keyboard_slots_first(order, slots, false) == std::vector<int>{249, 248, 250, 251}));
    // With the SD card: base 56 first, then 57 and 58, base 38 last.
    EXPECT_TRUE((keyboard_slots_first(order, slots, true) == std::vector<int>{249, 250, 251, 248}));
    // A slot with an unknown base keeps its place among the others.
    EXPECT_TRUE((keyboard_slots_first({251, 249, 252}, slots, false) == std::vector<int>{249, 248, 251, 252}));
    // Only new-stack bases: the order as it was.
    EXPECT_TRUE((keyboard_slots_first({250, 251}, {{250, 57}, {251, 58}}, true) == std::vector<int>{250, 251}));
}

void test_d2x_fallbacks() {
    const std::vector<int> conventional = {249, 250, 251};
    const std::vector<D2xSlot> standard = {{249, 56}, {250, 57}, {251, 58}};
    EXPECT_TRUE(append_d2x_fallbacks(conventional, standard, 56, false) == conventional);
    EXPECT_TRUE((append_d2x_fallbacks({251, 249, 250}, standard, 58, false) == std::vector<int>{251, 249, 250}));

    // The preferred base may be in an unusual slot already; the conventional
    // slots keep their places even when an extra base is an exact match.
    const std::vector<D2xSlot> extras = {
        {255, 38}, {254, 59}, {252, 58}, {248, 56}, {253, 57}, {249, 56}, {250, 57}, {251, 58}
    };
    EXPECT_TRUE((append_d2x_fallbacks(conventional, extras, 57, false) ==
                 std::vector<int>{249, 250, 251, 253, 248, 252, 254, 255}));
    EXPECT_TRUE((append_d2x_fallbacks({252, 249, 250, 251}, extras, 58, false) ==
                 std::vector<int>{252, 249, 250, 251, 253, 254, 248, 255}));
    // Inventory traversal cannot change the result, including ties.
    std::vector<D2xSlot> reversed = extras;
    std::reverse(reversed.begin(), reversed.end());
    EXPECT_TRUE(append_d2x_fallbacks(conventional, reversed, 57, false) ==
                append_d2x_fallbacks(conventional, extras, 57, false));
    EXPECT_TRUE((append_d2x_fallbacks(conventional, extras, 0, false) ==
                 std::vector<int>{249, 250, 251, 248, 252, 253, 254, 255}));

    // Each slot is tried once, including two inventory entries for one slot.
    EXPECT_TRUE((append_d2x_fallbacks({249, 250, 249, 251}, {{249, 56}, {252, 58}, {252, 58}}, 58, false) ==
                 std::vector<int>{249, 250, 251, 252}));
    // No AHBPROT: unknown bases neither add slots nor disturb the old order.
    EXPECT_TRUE(append_d2x_fallbacks(conventional, {}, 56, false) == conventional);
    EXPECT_TRUE(append_d2x_fallbacks(conventional, {{248, 0}, {252, 0}, {253, -1}}, 56, false) == conventional);
    // An SD game falls back only to bases that read the SD card (56-60):
    // base 38 in 255 is left out, the others keep their order.
    EXPECT_TRUE((append_d2x_fallbacks(conventional, extras, 57, true) ==
                 std::vector<int>{249, 250, 251, 253, 248, 252, 254}));
    EXPECT_TRUE(append_d2x_fallbacks(conventional, {{248, 38}, {252, 70}}, 57, true) == conventional);
    EXPECT_TRUE(d2x_base_reads_sd(56) && d2x_base_reads_sd(60));
    EXPECT_TRUE(!d2x_base_reads_sd(55) && !d2x_base_reads_sd(61) && !d2x_base_reads_sd(38));

    // Preserve the existing vWii adapter and keyboard policies verbatim,
    // including unusual keyboard slots already promoted by that policy.
    const std::vector<D2xSlot> devices = {{248, 38}, {249, 56}, {250, 57}, {251, 58}, {252, 58}, {253, 59}};
    const std::vector<int> adapter = {252, 249, 250, 251};
    EXPECT_TRUE((append_d2x_fallbacks(adapter, devices, 56, false) ==
                 std::vector<int>{252, 249, 250, 251, 253, 248}));
    const std::vector<int> keyboard_usb = keyboard_slots_first(adapter, devices, false);
    EXPECT_TRUE((keyboard_usb == std::vector<int>{249, 248, 252, 250, 251}));
    EXPECT_TRUE((append_d2x_fallbacks(keyboard_usb, devices, 56, false) ==
                 std::vector<int>{249, 248, 252, 250, 251, 253}));
    const std::vector<int> keyboard_sd = keyboard_slots_first(adapter, devices, true);
    EXPECT_TRUE((keyboard_sd == std::vector<int>{249, 252, 250, 251, 248}));
    EXPECT_TRUE((append_d2x_fallbacks(keyboard_sd, devices, 56, false) ==
                 std::vector<int>{249, 252, 250, 251, 248, 253}));

    // The game lists still choose the first slot; appending extras never
    // undoes Brawl's base 56 or Boating Bash's base 58/38 exception.
    std::string why;
    const int brawl = gx_pick_cios("RSBE01", 36, devices, false, why);
    EXPECT_EQ(brawl, 249);
    EXPECT_TRUE((append_d2x_fallbacks({brawl, 250, 251}, devices, 36, false) ==
                 std::vector<int>{249, 250, 251, 248, 252, 253}));
    const int boating = gx_pick_cios("SBVE78", 53, devices, false, why);
    EXPECT_EQ(boating, 251);
    EXPECT_TRUE((append_d2x_fallbacks({boating, 249, 250}, devices, 53, false) ==
                 std::vector<int>{251, 249, 250, 252, 253, 248}));
}

}  // namespace

int main() {
    test_protected();
    test_patch_policy();
    test_protected_game_fix();
    test_kirby_against_gx();
    test_outside_untouched();
    test_re4();
    test_own_executable();
    test_wip_nsmb();
    test_anti_002();
    test_sd_card();
    test_480p();
    test_region_video_fix();
    test_preferences();
    test_pick_cios();
    test_keyboard();
    test_d2x_fallbacks();
    if (g_failures == 0) {
        std::cout << "ALL GXPATCHES TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
