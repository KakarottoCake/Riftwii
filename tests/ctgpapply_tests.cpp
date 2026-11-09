// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The channel's "apply settings" step (riftwii/ctgpapply.hpp).
#include <cstdint>
#include <iostream>
#include <map>
#include <vector>

#include "riftwii/ctgpapply.hpp"
#include "riftwii/ctgptables.hpp"

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

// Made-up tables (CTGP's own come from tables.bin at run time): course
// words 100.., layout 0 with 54 groups of tracks 1.., layout 1 with 3
// groups of tracks 200...
static CtgpTables made_tables() {
    CtgpTables t;
    for (std::size_t i = 0; i < t.course.size(); ++i) t.course[i] = 100 + static_cast<std::uint32_t>(i);
    t.layouts[0].groups = 54;
    for (std::size_t i = 0; i < kCtgpLayoutBytes; ++i) t.layouts[0].tracks[i] = static_cast<std::uint8_t>(1 + i);
    t.layouts[1].groups = 3;
    for (std::size_t i = 0; i < 12; ++i) t.layouts[1].tracks[i] = static_cast<std::uint8_t>(200 + i);
    return t;
}
static const CtgpTables kTables = made_tables();

static void test_defaults_offline() {
    CtgpSettings s;
    s.cup_course_layout_using = 0;
    const auto ops = ctgp_apply_settings(s, kTables);
    const auto m = stores(ops);
    EXPECT_EQ(m.at(0x816ff518), 1u);
    EXPECT_EQ(m.at(kCtgpCourseTable), 100u);
    EXPECT_EQ(m.at(kCtgpCourseTable + 4), 101u);
    EXPECT_EQ(m.at(0x816ffb74), 131u);            // the 32nd course word
    EXPECT_EQ(kCtgpCourseTable + 4 * 32, kCtgpLayoutWords);
    EXPECT_EQ(m.at(kCtgpLayoutCount), 62u);       // 54 groups + 8
    EXPECT_EQ(m.at(kCtgpLayoutWords), 1u);        // first track of the first layout
    EXPECT_EQ(m.count(kCtgpLayoutWords + 4 * 215), (std::size_t)1);
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
    const auto ops = ctgp_apply_settings(s, kTables);
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
    const auto m = stores(ctgp_apply_settings(s, kTables));
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
    const auto ops = ctgp_apply_settings(s, kTables);
    const auto m = stores(ops);
    EXPECT_EQ(m.at(0x816ff518), 0u);
    EXPECT_EQ(m.at(0x80000a46), 3u);
    EXPECT_EQ(m.at(0x816ffa14), 4u);
    EXPECT_EQ(m.count(kCtgpCourseTable), (std::size_t)0);
    EXPECT_EQ(m.at(kCtgpLayoutCount), 0u);
    EXPECT_TRUE(has_row(ops, kCtgpRowPatchB, 0xe4, 0x60000000));  // the tail still runs
}

static void test_layouts() {
    CtgpSettings s;
    s.cup_course_layout_using = 1;
    auto m = stores(ctgp_apply_settings(s, kTables));
    EXPECT_EQ(m.at(kCtgpLayoutWords), 200u);
    EXPECT_EQ(m.at(kCtgpLayoutWords + 4 * 11), 211u);
    EXPECT_EQ(m.count(kCtgpLayoutWords + 4 * 12), (std::size_t)0);
    EXPECT_EQ(m.at(kCtgpLayoutCount), 11u);       // 3 groups + 8
    s.cup_course_layout_using = 2;
    m = stores(ctgp_apply_settings(s, kTables));
    EXPECT_EQ(m.at(kCtgpLayoutCount), 8u);
    EXPECT_EQ(m.count(kCtgpLayoutWords), (std::size_t)0);
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
    EXPECT_EQ(l.size(), (std::size_t)5);
    EXPECT_TRUE(l[0].path == "/PACKAGES/CTGPR/no_ct_24p.bin");
    EXPECT_TRUE(l[1].path == "/PACKAGES/NOMUSIC/FST.BIN" && l[1].hash_list.empty());
    EXPECT_TRUE(l[2].path == "/PACKAGES/FONT/fontMVBoli.bin");
    EXPECT_TRUE(l[3].path == "/PACKAGES/MYSTUFF/fst_no_tracks.bin");
    EXPECT_TRUE(l[4].path == "/PACKAGES/CORE/FST.BIN");
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

int main() {
    test_package_lists();
    test_defaults_offline();
    test_online_always_off();
    test_options();
    test_disabled();
    test_layouts();
    if (g_failures == 0) std::cout << "ctgpapply tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
