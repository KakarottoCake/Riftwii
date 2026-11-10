// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The channel's "apply settings" step (riftwii/ctgpapply.hpp).
#include <cstdint>
#include <cstring>
#include <string>
#include <iostream>
#include <map>
#include <vector>

#include "riftwii/ctgpapply.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

// The last value stored at an address (as the channel's later stores win).
static std::map<std::uint32_t, std::uint32_t> stores(const std::vector<CtgpOp>& ops) {
    std::map<std::uint32_t, std::uint32_t> m;
    for (const auto& op : ops) if (op.kind == CtgpOpKind::Store) m[op.address] = op.value;
    return m;
}
static bool has_row(const std::vector<CtgpOp>& ops, std::uint32_t row, std::uint32_t off, std::uint32_t v) {
    for (const auto& op : ops)
        if (op.kind == CtgpOpKind::RowStore && op.row == row && op.offset == off && op.value == v) return true;
    return false;
}

// The course table is baked into the build (ctgpapply.cpp): {8, 1, 2, 4} then
// the fixed tail. The cup layout comes in the settings (read from the player's
// install); the ones here are small made-up layouts.
static CtgpLayout made_layout(std::uint32_t groups, std::uint8_t first) {
    CtgpLayout l;
    l.groups = groups;
    for (std::uint32_t i = 0; i < kCtgpLayoutTracks; ++i) l.tracks[i] = static_cast<std::uint8_t>(first + i % 200);
    return l;
}

static void test_defaults_offline() {
    CtgpSettings s;
    s.cup_course_layout_using = 0;
    s.layout = made_layout(54, 32);
    const auto ops = ctgp_apply_settings(s);
    const auto m = stores(ops);
    EXPECT_EQ(m.at(0x816ff518), 1u);
    EXPECT_EQ(m.at(kCtgpCourseTable), 8u);        // the fixed {8, 1, 2, 4} head
    EXPECT_EQ(m.at(kCtgpCourseTable + 4), 1u);
    EXPECT_EQ(m.at(kCtgpCourseTable + 8), 2u);
    EXPECT_EQ(m.at(kCtgpCourseTable + 12), 4u);
    EXPECT_EQ(m.at(kCtgpCourseTable + 16), 0u);   // the tail starts at 0
    EXPECT_EQ(m.at(0x816ffb74), 0x1cu);           // the 32nd course word (tail's last)
    EXPECT_EQ(kCtgpCourseTable + 4 * 32, kCtgpLayoutWords);
    EXPECT_EQ(m.at(kCtgpLayoutCount), 62u);       // 54 groups + 8
    EXPECT_EQ(m.at(kCtgpLayoutWords), 32u);       // the layout's first track
    EXPECT_EQ(m.at(kCtgpLayoutWords + 4 * 215), 47u);    // its last (32 + 215 % 200)
    EXPECT_EQ(kCtgpLayoutWords + 4 * 216, kCtgpLayoutCount);   // the words end where the count starts
    // offline: the network is stubbed out
    EXPECT_EQ(m.at(0x816b5328), 0u);
    EXPECT_EQ(m.at(0x80003183), 0u);
    EXPECT_TRUE(has_row(ops, kCtgpRowStub1, 0, 0x3860FFB4));
    EXPECT_TRUE(has_row(ops, kCtgpRowStub2, 0x2a4, 0x38600000));
    // the player counts fall back to 12 while opt_7d0 is 0
    EXPECT_EQ(m.at(0x816ffa2c), 12u);
    EXPECT_TRUE(has_row(ops, kCtgpRowPatchB, 0xe4, 0x60000000));
    EXPECT_EQ(m.at(0x80000a80), 1u);
}

// Nothing turns online play on: whatever config.ini says about LAN mode,
// UPnP or anything else, the network calls stay stubbed and the LAN words 0.
static void test_online_always_off() {
    const CtgpSettings s = ctgp_settings_from_ini("[ctgpr]\nlanmode = yes\nuse_upnp = yes\nroom_encryption = yes\n", {},
                                                  CtgpRegion::P);
    const auto ops = ctgp_apply_settings(s);
    const auto m = stores(ops);
    EXPECT_EQ(m.at(0x816b5328), 0u);
    EXPECT_EQ(m.at(0x816b532c), 0u);
    EXPECT_EQ(m.at(0x80003183), 0u);
    EXPECT_EQ(m.at(0x816ffa38), 0u);              // UPnP off
    EXPECT_TRUE(has_row(ops, kCtgpRowStub1, 0, 0x3860FFB4));
    EXPECT_TRUE(has_row(ops, kCtgpRowStub2, 0x3e4, 0x3860FFB4));
}

static void test_options() {
    CtgpSettings s;
    s.room_race_count = 7;
    s.room_engine_class = 2;
    s.room_encryption = 1;
    s.opt_754 = 5;
    s.opt_494 = 9;
    s.room_track_availability = 0;
    s.anti_cheat = 1;
    s.anti_cheat_fr_strength = 1;
    s.show_race_rules = 2;
    s.opt_7d0 = 1;
    s.ctgpr_package_version = 0x01031040;
    s.core_package_version = 0x01000004 + 0x00040000;
    s.opt_8b1 = true;
    s.my_stuff = 1;
    const auto m = stores(ctgp_apply_settings(s));
    EXPECT_EQ(m.at(0x80000a46), 7u);
    EXPECT_EQ(m.at(0x80000a44), 2u);
    EXPECT_EQ(m.at(0x80000a49), 1u);
    EXPECT_EQ(m.at(0x816ffa14), 2u);             // encryption forces 2
    EXPECT_EQ(m.at(0x816ffa18), 9u);             // opt_754 non-zero: replaced by opt_494
    EXPECT_EQ(m.at(0x80000a4a), 2u);             // track availability 0 -> 2
    EXPECT_EQ(m.at(0x80000a83), 2u);             // anti-cheat with strength 1
    EXPECT_EQ(m.at(0x816ff524), 1u);
    EXPECT_EQ(m.at(0x816ff528), 1u);
    EXPECT_EQ(m.at(0x80000a00), 1u);
    EXPECT_EQ(m.at(0x80000a86), 1u);
    EXPECT_EQ(m.at(0x816ffa38), 0u);             // UPnP stays off
    EXPECT_EQ(m.count(0x816ffa2c), (std::size_t)0);   // opt_7d0 set: the ini value stays
    EXPECT_EQ(m.at(0x80000a40), 0x01031040u);
    EXPECT_EQ(m.at(0x816ffab4), 1u);
}

static void test_disabled() {
    CtgpSettings s;
    s.ctgpr_enabled = false;
    s.opt_754 = 4;
    const auto ops = ctgp_apply_settings(s);
    const auto m = stores(ops);
    EXPECT_EQ(m.at(0x816ff518), 0u);
    EXPECT_EQ(m.at(0x80000a46), 3u);
    EXPECT_EQ(m.at(0x816ffa14), 4u);
    EXPECT_EQ(m.count(kCtgpCourseTable), (std::size_t)0);
    EXPECT_EQ(m.at(kCtgpLayoutCount), 0u);
    EXPECT_TRUE(has_row(ops, kCtgpRowPatchB, 0xe4, 0x60000000));  // the tail still runs
    // Online stays off even with CTGP-R's custom tracks disabled: the network
    // calls and LAN words are stubbed just the same (they moved out of the
    // ctgpr-enabled branch so no config can bring online back).
    EXPECT_EQ(m.at(0x80003183), 0u);
    EXPECT_EQ(m.at(0x816b5328), 0u);
    EXPECT_EQ(m.at(0x816b532c), 0u);
    EXPECT_TRUE(has_row(ops, kCtgpRowStub1, 0x00, 0x3860FFB4));   // li r3,-76
    EXPECT_TRUE(has_row(ops, kCtgpRowStub1, 0x04, 0x4E800020));   // blr
    EXPECT_TRUE(has_row(ops, kCtgpRowStub2, 0x3e4, 0x3860FFB4));
    EXPECT_TRUE(has_row(ops, kCtgpRowStub2, 0x2a4, 0x38600000));  // li r3,0
}

static void test_layouts() {
    // The words are the layout's track bytes, one each, for its groups only.
    CtgpSettings s;
    s.cup_course_layout_using = 1;
    s.layout = made_layout(3, 100);
    auto m = stores(ctgp_apply_settings(s));
    EXPECT_EQ(m.at(kCtgpLayoutWords), 100u);
    EXPECT_EQ(m.at(kCtgpLayoutWords + 4 * 11), 111u);     // the third cup's last track
    EXPECT_EQ(m.count(kCtgpLayoutWords + 4 * 12), (std::size_t)0);
    EXPECT_EQ(m.at(kCtgpLayoutCount), 11u);       // 3 groups + 8
    s.layout = CtgpLayout();                      // an empty layout: no cups
    m = stores(ctgp_apply_settings(s));
    EXPECT_EQ(m.at(kCtgpLayoutCount), 8u);
    EXPECT_EQ(m.count(kCtgpLayoutWords), (std::size_t)0);
    // The choice: 0, 1 or 2, anything else 0.
    for (int v : {0, 1, 2}) {
        s.cup_course_layout_using = v;
        EXPECT_EQ(ctgp_layout_choice(s), v);
    }
    for (int v : {-1, 3, 4, 99}) {
        s.cup_course_layout_using = v;
        EXPECT_EQ(ctgp_layout_choice(s), 0);
    }
    // config.ini's language picks the alphabetical file (6 is Japanese).
    EXPECT_EQ(ctgp_settings_from_ini("[ctgpr]\nlanguage = 6\ncup_course_layout_using = 1\n", {}, CtgpRegion::P).language, 6);
    EXPECT_EQ(ctgp_settings_from_ini("[ctgpr]\n", {}, CtgpRegion::P).language, 0);
}

// A .cup file as the channel saves it: "CUP2", a big-endian group count, then
// 216 track bytes (made up here).
static std::vector<std::uint8_t> made_cup(std::uint32_t groups) {
    std::vector<std::uint8_t> b(kCtgpCupFileBytes, 0xff);
    std::memcpy(b.data(), "CUP2", 4);
    b[4] = static_cast<std::uint8_t>(groups >> 24);
    b[5] = static_cast<std::uint8_t>(groups >> 16);
    b[6] = static_cast<std::uint8_t>(groups >> 8);
    b[7] = static_cast<std::uint8_t>(groups);
    for (std::uint32_t i = 0; i < groups * 4 && i < kCtgpLayoutTracks; ++i) b[8 + i] = static_cast<std::uint8_t>(0x20 + (i * 7) % 0xdf);
    return b;
}

static void test_cup_files() {
    std::string error;
    CtgpLayout l;
    // A hand-built file is copied as it is: no reordering.
    auto cup = made_cup(5);
    EXPECT_TRUE(ctgp_layout_from_cup(cup.data(), cup.size(), l, error));
    EXPECT_EQ(l.groups, 5u);
    for (std::uint32_t i = 0; i < 20; ++i) EXPECT_EQ(l.tracks[i], cup[8 + i]);
    // and reaches the game word for word
    CtgpSettings s;
    s.layout = l;
    const auto m = stores(ctgp_apply_settings(s));
    for (std::uint32_t i = 0; i < 20; ++i) EXPECT_EQ(m.at(kCtgpLayoutWords + 4 * i), static_cast<std::uint32_t>(cup[8 + i]));
    EXPECT_EQ(m.at(kCtgpLayoutCount), 13u);
    // The most cups a layout holds; bytes past its groups are not looked at.
    cup = made_cup(54);
    EXPECT_TRUE(ctgp_layout_from_cup(cup.data(), cup.size(), l, error) && l.groups == 54);
    cup = made_cup(0);
    EXPECT_TRUE(ctgp_layout_from_cup(cup.data(), cup.size(), l, error) && l.groups == 0);

    // Refused: the channel's checks, and a track that does not exist.
    const CtgpLayout before = made_layout(2, 40);
    auto refused = [&](const std::vector<std::uint8_t>& bytes) {
        CtgpLayout out = before;
        std::string why;
        const bool ok = ctgp_layout_from_cup(bytes.data(), bytes.size(), out, why);
        return !ok && !why.empty() && out.groups == before.groups && out.tracks == before.tracks;
    };
    EXPECT_TRUE(refused(made_cup(55)));                       // more cups than fit
    EXPECT_TRUE(refused(made_cup(0xffffffffu)));
    auto bad = made_cup(5);
    bad[0] = 'X';                                             // not CUP2
    EXPECT_TRUE(refused(bad));
    bad = made_cup(5);
    bad.pop_back();                                           // short
    EXPECT_TRUE(refused(bad));
    bad = made_cup(5);
    bad.push_back(0);                                         // long
    EXPECT_TRUE(refused(bad));
    bad = made_cup(5);
    bad[8 + 19] = 0xff;                                       // no such track
    EXPECT_TRUE(refused(bad));
    EXPECT_TRUE(refused({}));
    CtgpLayout out;
    EXPECT_TRUE(!ctgp_layout_from_cup(nullptr, kCtgpCupFileBytes, out, error));

    // A settings-table entry (the channel program's layout) is the same
    // without the magic.
    const auto entry = made_cup(7);
    EXPECT_TRUE(ctgp_layout_from_entry(entry.data() + 4, l, error) && l.groups == 7 && l.tracks[27] == entry[8 + 27]);
    EXPECT_TRUE(!ctgp_layout_from_entry(made_cup(60).data() + 4, l, error));
}

static void test_package_lists() {
    CtgpSettings s;  // ctgpr yes, nothing else
    auto l = ctgp_package_lists(s);
    EXPECT_EQ(l.size(), (std::size_t)2);
    EXPECT_TRUE(l[0].path == "/PACKAGES/CTGPR/FST.BIN" && l[0].hash_list == "CTGPR");
    EXPECT_TRUE(l[1].path == "/PACKAGES/CORE/FST.BIN" && l[1].hash_list == "CORE");
    s.ctgpr_enabled = false;
    s.my_stuff = 2;
    s.no_music = true;
    s.font = 3;
    l = ctgp_package_lists(s);
    EXPECT_EQ(l.size(), (std::size_t)3);
    EXPECT_TRUE(l[0].path == "/PACKAGES/CTGPR/no_ct_24p.bin");
    EXPECT_TRUE(l[1].path == "/PACKAGES/MYSTUFF/fst_no_tracks.bin");
    EXPECT_TRUE(l[2].path == "/PACKAGES/CORE/FST.BIN");
    EXPECT_TRUE(l[1].allow_unsigned);
    EXPECT_TRUE(!l[0].allow_unsigned && !l[2].allow_unsigned);
    s.my_stuff = 1;
    s.font = 0;
    s.no_music = false;
    l = ctgp_package_lists(s);
    EXPECT_TRUE(l.size() == 3 && l[1].path == "/PACKAGES/MYSTUFF/FST.BIN");
    const CtgpSettings ini = ctgp_settings_from_ini("[ctgpr]\nno_music = yes\nfont = 2\nmy_stuff = 3\n", {}, CtgpRegion::P);
    EXPECT_TRUE(ini.no_music);
    EXPECT_EQ(ini.font, 2);
    EXPECT_EQ(ini.my_stuff, 3);
}

// The ini's yes/no is read in any letter case, as the field table reads it:
// "No" must turn CTGPR off and "Yes" must turn an option on.
static void test_boolean_case() {
    const CtgpSettings off = ctgp_settings_from_ini("[ctgpr]\nctgpr = No\nno_music = Yes\nanti-cheat = TRUE\n", {},
                                                    CtgpRegion::P);
    EXPECT_TRUE(!off.ctgpr_enabled);
    EXPECT_TRUE(off.no_music);
    EXPECT_EQ(off.anti_cheat, 1);
    const CtgpSettings junk = ctgp_settings_from_ini("[ctgpr]\nctgpr = maybe\nno_music = 2\n", {}, CtgpRegion::P);
    EXPECT_TRUE(junk.ctgpr_enabled);  // not a yes/no: the default stays
    EXPECT_TRUE(!junk.no_music);
}

int main() {
    test_boolean_case();
    test_package_lists();
    test_defaults_offline();
    test_online_always_off();
    test_options();
    test_disabled();
    test_layouts();
    test_cup_files();
    if (g_failures == 0) std::cout << "ctgpapply tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
