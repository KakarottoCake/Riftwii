// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpapply.hpp"

#include <algorithm>

#include <cstdlib>
#include <cstring>

#include "riftwii/ctgpfields.hpp"

namespace riftwii {
namespace {

// CTGP-R's course table is baked into the build, the same way the row table is
// (ctgprowdata.cpp): it is an install-independent permutation of the 32 base
// slots (not the custom track-ID bytes in CTGP.CRS). The cup layouts are not
// baked: the launch reads the chosen one from the player's own install
// (CtgpSettings::layout; wii/launch/ctgpr.cpp).

// The course table at 0x816ffaf8: {8, 1, 2, 4} then the fixed 28-word tail, the
// internal course IDs of Mario Kart Wii's 32 base tracks in the channel's grid
// order (chan_internal writes it at 0x816ffaf8..0x816ffb74).
const std::uint32_t kCourseHead[4] = {8, 1, 2, 4};
const std::uint32_t kCourseTail[28] = {0,    5,    6,    7,    9,    0xf,  0xb,  3,    0xe,  0xa,
                                       0xc,  0xd,  0x10, 0x14, 0x19, 0x1a, 0x1b, 0x1f, 0x17,
                                       0x12, 0x15, 0x1e, 0x1d, 0x11, 0x18, 0x16, 0x13, 0x1c};


class Ops {
public:
    void store(std::uint32_t address, std::uint32_t size, std::uint32_t value) {
        CtgpOp op;
        op.kind = CtgpOpKind::Store;
        op.address = address;
        op.size = size;
        op.value = value;
        ops.push_back(op);
    }
    void w32(std::uint32_t a, std::uint32_t v) { store(a, 4, v); }
    void w8(std::uint32_t a, std::uint32_t v) { store(a, 1, v & 0xFF); }
    void row_store(std::uint32_t row, std::uint32_t offset, std::uint32_t value) {
        CtgpOp op;
        op.kind = CtgpOpKind::RowStore;
        op.row = row;
        op.offset = offset;
        op.value = value;
        ops.push_back(op);
    }
    void row_move(std::uint32_t row, std::uint32_t to, std::uint32_t from, std::uint32_t size) {
        CtgpOp op;
        op.kind = CtgpOpKind::RowMove;
        op.row = row;
        op.offset = to;
        op.from = from;
        op.size = size;
        ops.push_back(op);
    }
    // `count` words to consecutive addresses from `address`. Out of line, so
    // the table's address arrives as a plain pointer: inlined into a loop
    // that also counts the game address, GCC folded the two into one biased
    // pointer (the table's address minus 0x816ffaf8), a relocation far
    // outside the plugin's image that its loader rightly refuses.
    __attribute__((noinline)) void words(std::uint32_t address, const std::uint32_t* from, std::uint32_t count) {
        for (std::uint32_t i = 0; i < count; ++i) w32(address + 4 * i, from[i]);
    }
    std::vector<CtgpOp> ops;
};

constexpr std::uint32_t kLiR3Minus76 = 0x3860FFB4;  // li r3,-76
constexpr std::uint32_t kLiR3Zero = 0x38600000;     // li r3,0
constexpr std::uint32_t kBlr = 0x4E800020;

}  // namespace

CtgpSettings ctgp_settings_from_ini(const std::string& config_ini, const std::vector<CtgpPackage>& packages,
                                    CtgpRegion region) {
    const auto ini = ctgp_parse_ini(config_ini);
    auto text = [&](const char* key) -> std::string {
        const auto it = ini.find(key);
        return it == ini.end() ? std::string() : it->second;
    };
    auto yes = [&](const char* key, bool def) {
        bool value = def;
        ctgp_parse_ini_bool(text(key), value);
        return value;
    };
    auto num = [&](const char* key, std::int32_t def) {
        const std::string v = text(key);
        if (v.empty()) return def;
        char* end = nullptr;
        const long n = std::strtol(v.c_str(), &end, 10);
        return *end == '\0' ? static_cast<std::int32_t>(n) : def;
    };
    CtgpSettings s;
    s.ctgpr_enabled = yes("ctgpr", true);
    // No LAN play either: lanmode is not read (ctgp_apply_settings).
    s.cup_course_layout_using = num("cup_course_layout_using", 0);
    s.language = num("language", 0);
    s.room_race_count = num("room_race_count", 0);
    s.anti_cheat = yes("anti-cheat", false) ? 1 : 0;
    s.anti_cheat_fr_strength = num("anti-cheat_fr_strength", 0);
    s.show_race_rules = num("show_race_rules", 0);
    s.minimap_mii_heads = yes("minimap_mii_heads", false) ? 1 : 0;
    s.room_host_votes = yes("room_host_votes", false) ? 1 : 0;
    s.room_engine_class = num("room_engine_class", 0);
    s.room_vehicle_availability = num("room_vehicle_availability", 0);
    s.room_encryption = yes("room_encryption", false) ? 1 : 0;
    s.room_tc_effect_mega = yes("room_tc_effect_mega", false) ? 1 : 0;
    s.room_disable_tc_autofire = yes("room_disable_tc_autofire", false) ? 1 : 0;
    s.room_track_availability = num("room_track_availability", 0);
    // use_upnp is online only and stays 0.
    s.use_country_id_for_region_id = yes("use_country_id_for_region_id", false) ? 1 : 0;
    s.knockout_type = num("knockout_type", 0);
    s.knockout_bonus_type = num("knockout_bonus_type", 0);
    s.knockout_bonus_stockpile_max = num("knockout_bonus_stockpile_max", 0);
    s.knockout_bonus_point_threshold = num("knockout_bonus_point_threshold", 0);
    s.knockout_players_eliminated_1 = num("knockout_players_eliminated_1", 0);
    s.knockout_phase_2_threshold = num("knockout_phase_2_threshold", 0);
    s.knockout_players_eliminated_2 = num("knockout_players_eliminated_2", 0);
    s.my_stuff = num("my_stuff", 0);
    s.no_music = yes("no_music", false);
    s.font = num("font", 0);
    s.ghost_saving_mode = std::clamp(num("ghost_saving_mode", 0), 0, 2);
    s.ctgpr_package_version = ctgp_package_version(packages, "CTGPR");
    s.core_package_version = ctgp_package_version(packages, "CORE");
    s.region = static_cast<std::uint32_t>(region);
    return s;
}

int ctgp_layout_choice(const CtgpSettings& s) {
    return s.cup_course_layout_using >= 0 && s.cup_course_layout_using <= 2 ? s.cup_course_layout_using : 0;
}

bool ctgp_layout_from_entry(const std::uint8_t* entry, CtgpLayout& out, std::string& error) {
    const std::uint32_t groups = (std::uint32_t(entry[0]) << 24) | (std::uint32_t(entry[1]) << 16) |
                                 (std::uint32_t(entry[2]) << 8) | entry[3];
    // The channel refuses 0x37 groups or more (chan_internal FUN_808862f4).
    if (groups > kCtgpLayoutMaxGroups) {
        error = "a cup layout names " + std::to_string(groups) + " cups; at most 54 fit";
        return false;
    }
    CtgpLayout layout;
    layout.groups = groups;
    std::memcpy(layout.tracks.data(), entry + 4, kCtgpLayoutTracks);
    // 0xff is no track: the course list has 255 entries, 0x00..0xfe.
    for (std::uint32_t i = 0; i < groups * 4; ++i) {
        if (layout.tracks[i] == 0xff) {
            error = "a cup layout names a track that does not exist (0xff)";
            return false;
        }
    }
    out = layout;
    return true;
}

bool ctgp_layout_from_cup(const std::uint8_t* data, std::size_t size, CtgpLayout& out, std::string& error) {
    // The channel's checks (FUN_808862f4): exactly 0xe0 bytes, "CUP2", then a
    // layout entry.
    if (data == nullptr || size != kCtgpCupFileBytes) {
        error = "a .cup file is " + std::to_string(size) + " bytes, not 224";
        return false;
    }
    if (std::memcmp(data, "CUP2", 4) != 0) {
        error = "a .cup file does not start with CUP2";
        return false;
    }
    return ctgp_layout_from_entry(data + 4, out, error);
}

std::vector<CtgpOp> ctgp_apply_settings(const CtgpSettings& s) {
    Ops o;
    o.w32(0x816ff518, s.ctgpr_enabled ? 1 : 0);
    o.w32(kCtgpLayoutCount, 0);
    std::uint32_t fa14 = 0, fa18 = 0;  // 0x816ffa14 / 0x816ffa18
    if (!s.ctgpr_enabled) {
        o.w8(0x80000a46, 3);
        fa14 = fa18 = static_cast<std::uint32_t>(s.opt_754);
        o.w32(0x816ffa18, fa18);
        o.w32(0x816ffa14, fa14);
    } else {
        o.words(kCtgpCourseTable, kCourseHead, 4);
        o.words(kCtgpCourseTable + 16, kCourseTail, 28);
        // The chosen layout, as the channel copies it: one word per track byte
        // of its groups, then the group count plus the eight base cups.
        // ctgp_layout_from_entry/_from_cup bounded both.
        const std::uint32_t groups = std::min<std::uint32_t>(s.layout.groups, kCtgpLayoutMaxGroups);
        for (std::uint32_t i = 0; i < groups * 4; ++i)
            o.w32(kCtgpLayoutWords + 4 * i, s.layout.tracks[i]);
        o.w32(kCtgpLayoutCount, groups + 8);

        if (s.core_package_version > 0x1030041)
            o.w8(0x816ffab4, (s.opt_8b1 && s.my_stuff != 0) ? 1 : 0);
        o.w32(0x80000a40, s.ctgpr_package_version);
        o.w8(0x80000a44, static_cast<std::uint32_t>(s.room_engine_class));
        o.w8(0x80000a46, static_cast<std::uint32_t>(s.room_race_count));
        o.w8(0x80000a47, static_cast<std::uint32_t>(s.room_disable_tc_autofire));
        o.w8(0x80000a48, static_cast<std::uint32_t>(s.room_tc_effect_mega));
        o.w8(0x80000a49, static_cast<std::uint32_t>(s.room_encryption));
        fa14 = static_cast<std::uint32_t>(s.opt_754);
        if (s.room_encryption != 0) fa14 = 2;
        o.w32(0x816ffa14, fa14);
        std::uint32_t t = static_cast<std::uint32_t>(s.room_track_availability) & 0xFF;
        if (t == 0) t = 2; else if (t == 1) t = 0; else if (t == 2) t = 1;
        o.w8(0x80000a4a, t);
        o.w8(0x80000a4b, static_cast<std::uint32_t>(s.room_vehicle_availability));
        o.w8(0x80000a4c, static_cast<std::uint32_t>(s.room_host_votes));
        o.w8(0x80000a80, 1);
        if (s.anti_cheat == 1) {
            o.w8(0x80000a83, s.anti_cheat_fr_strength == 1 ? 2 : 1);
        } else {
            o.w8(0x80000a83, 0);
        }
        o.w8(0x816ffadd, s.minimap_mii_heads == 1 ? 1 : 0);
        fa18 = static_cast<std::uint32_t>(s.opt_754);
        if (s.show_race_rules == 2) { o.w8(0x80000a00, 1); o.w8(0x80000a86, 1); }
        else if (s.show_race_rules == 1) { o.w8(0x80000a86, 1); }
        o.w32(0x816ffa18, fa18);
    }

    // Online play is off for every launch from RiftWii, whether or not CTGP-R's custom tracks are
    // enabled: CTGP's code still runs when ctgpr is off (it just loads
    // no_ct_24p instead of the track package), so the network calls are
    // stubbed to return -76 unconditionally -- as the channel does when it
    // has no network settings -- and the LAN path (0xFE in the two words) is
    // never taken, whatever config.ini says. Nothing in RiftWii's settings
    // turns this on, so these run outside the if/else above.
    o.w8(0x80003183, 0);
    o.w32(0x816b5328, 0);
    o.w32(0x816b532c, 0);
    o.row_store(kCtgpRowStub1, 0x00, kLiR3Minus76);
    o.row_store(kCtgpRowStub1, 0x04, kBlr);
    o.row_store(kCtgpRowStub2, 0x00, kLiR3Minus76);
    o.row_store(kCtgpRowStub2, 0x04, kBlr);
    o.row_store(kCtgpRowStub2, 0x22c, kLiR3Minus76);
    o.row_store(kCtgpRowStub2, 0x230, kBlr);
    o.row_store(kCtgpRowStub2, 0x2a4, kLiR3Zero);
    o.row_store(kCtgpRowStub2, 0x2a8, kBlr);
    o.row_store(kCtgpRowStub2, 0x3e4, kLiR3Minus76);
    o.row_store(kCtgpRowStub2, 0x3e8, kBlr);

    // common tail
    if (fa18 != 0) o.w32(0x816ffa18, static_cast<std::uint32_t>(s.opt_494));
    o.w32(0x816ffaac, static_cast<std::uint32_t>(s.opt_760));
    if (s.anti_cheat != 0) o.w32(0x816ff524, 1);
    if (s.anti_cheat_fr_strength > 0) o.w32(0x816ff528, 1);
    o.w8(0x816ffa38, 0);  // use_upnp: online only, off
    o.w8(0x816ffa28, static_cast<std::uint32_t>(s.opt_8bc));
    o.w32(0x816ffa6c, static_cast<std::uint32_t>(s.use_country_id_for_region_id));
    o.w32(0x816ffa78, static_cast<std::uint32_t>(s.knockout_bonus_type));
    o.w32(0x816ffa7c, static_cast<std::uint32_t>(s.knockout_bonus_stockpile_max));
    o.w32(0x816ffa80, static_cast<std::uint32_t>(s.knockout_bonus_point_threshold));
    o.w32(0x816ffa88, static_cast<std::uint32_t>(s.knockout_phase_2_threshold));
    o.w32(0x816ffa84, static_cast<std::uint32_t>(s.knockout_players_eliminated_1));
    o.w32(0x816ffa8c, static_cast<std::uint32_t>(s.knockout_players_eliminated_2));
    o.w32(0x816ffa74, static_cast<std::uint32_t>(s.knockout_type));
    if (s.opt_7d0 == 0) {
        o.w32(0x816ffa2c, 12);
        o.w32(0x816ffa30, 12);
    }
    // code patches in the game
    // memmove(row + 0x14, row + 0x0c, 0x24): the code shifts forward 8 bytes to
    // make room for the two instructions stored at +0x0c and +0x10.
    o.row_move(kCtgpRowPatchA, 0x14, 0x0c, 0x24);
    o.row_store(kCtgpRowPatchA, 0x0c, 0x28040049);
    o.row_store(kCtgpRowPatchA, 0x10, 0x41810044);
    o.row_store(kCtgpRowPatchA, 0x38, 0x41820018);
    o.row_store(kCtgpRowPatchB, 0xe4, 0x60000000);
    return o.ops;
}

std::vector<CtgpPackageList> ctgp_package_lists(const CtgpSettings& s) {
    std::vector<CtgpPackageList> out;
    out.push_back({s.ctgpr_enabled ? "/PACKAGES/CTGPR/FST.BIN" : "/PACKAGES/CTGPR/no_ct_24p.bin", "CTGPR", false});
    // My Stuff is the player's own files: no signed digest list vouches for
    // them, and the channel serves them unsigned, so this package alone is
    // allowed unsigned files.
    if (s.my_stuff == 1) out.push_back({"/PACKAGES/MYSTUFF/FST.BIN", "", true});
    // The checked CORE/CTGPR release ships no NOMUSIC.HSH or FONT.HSH.
    // CORE.HSH authenticates their FST lists (fsnm/fsfont), not the audio
    // or font payloads. Keep the default files until signed payloads exist.
    if (s.my_stuff == 2) out.push_back({"/PACKAGES/MYSTUFF/fst_no_tracks.bin", "", true});
    if (s.my_stuff == 3) out.push_back({"/PACKAGES/MYSTUFF/fst_brstm.bin", "", true});
    out.push_back({"/PACKAGES/CORE/FST.BIN", "CORE", false});
    return out;
}

}  // namespace riftwii
