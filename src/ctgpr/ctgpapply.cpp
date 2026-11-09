// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpapply.hpp"

#include <algorithm>

#include <cstdlib>

#include "riftwii/ctgpfields.hpp"

namespace riftwii {
namespace {

// CTGP-R's course table and cup layouts, baked into the build the same way the
// row table is (ctgprowdata.cpp). They are plain CTGP-R data the developers
// agreed RiftWii may carry, not secrets. We bake them rather than derive them
// from the package disk's CTGP.CRS and .cup files: the real course table is an
// install-independent permutation of the 32 base slots (not the custom
// track-ID bytes in CTGP.CRS), and the channel reorders a .cup's body before
// the game sees it, so copying either file verbatim gives the game the wrong
// tables. Because the cup layouts are specific to one CTGP-R version, the
// launch pins kCtgpTestedCtgprVersion (ctgplaunch.hpp); a later step that
// derives the cups correctly will relax that pin.

// The course table at 0x816ffaf8: {8, 1, 2, 4} then the fixed 28-word tail, the
// internal course IDs of Mario Kart Wii's 32 base tracks in the channel's grid
// order (chan_internal writes it at 0x816ffaf8..0x816ffb74).
const std::uint32_t kCourseHead[4] = {8, 1, 2, 4};
const std::uint32_t kCourseTail[28] = {0,    5,    6,    7,    9,    0xf,  0xb,  3,    0xe,  0xa,
                                       0xc,  0xd,  0x10, 0x14, 0x19, 0x1a, 0x1b, 0x1f, 0x17,
                                       0x12, 0x15, 0x1e, 0x1d, 0x11, 0x18, 0x16, 0x13, 0x1c};

// The two cup layouts (cup_course_layout_using 0 and 1; layout 2 is empty): a
// group count of fours, then 216 course-ID bytes each, as the channel's
// settings table at chan_internal 0x80bfe01c holds them.
const std::uint8_t kLayouts[2][216] = {
    {
        111, 112, 77, 75, 82, 95, 86, 84, 105, 113, 79, 114, 115, 90, 102, 116, 101, 108,
        117, 83, 71, 76, 118, 119, 106, 120, 89, 121, 91, 98, 103, 107, 70, 68, 100, 81,
        122, 123, 124, 125, 126, 96, 104, 87, 127, 128, 110, 92, 129, 130, 65, 97, 69, 78,
        72, 85, 131, 99, 132, 64, 80, 73, 146, 139, 94, 135, 109, 136, 144, 140, 138, 141,
        152, 147, 142, 155, 150, 153, 151, 133, 149, 143, 134, 156, 148, 154, 145, 157, 158, 159,
        160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 187, 171, 178, 183, 188, 170, 181, 189,
        185, 179, 182, 186, 172, 173, 180, 210, 174, 175, 190, 192, 184, 176, 191, 177, 229, 193,
        202, 247, 221, 231, 213, 241, 211, 240, 248, 203, 53, 197, 234, 244, 230, 42, 59, 44,
        236, 222, 223, 218, 235, 220, 219, 51, 198, 196, 194, 212, 214, 216, 238, 246, 50, 47,
        195, 237, 232, 239, 45, 243, 204, 205, 207, 206, 200, 201, 217, 227, 46, 199, 226, 225,
        32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 43, 48, 49, 52, 60, 61, 62, 63,
        208, 209, 215, 224, 228, 233, 242, 245, 249, 250, 251, 252, 253, 254, 74, 93, 88, 137,
    },
    {
        195, 201, 196, 197, 200, 77, 125, 114, 202, 243, 98, 141, 33, 160, 121, 130, 189, 204,
        135, 180, 35, 206, 97, 248, 110, 140, 228, 144, 44, 36, 142, 199, 37, 38, 85, 39,
        148, 45, 91, 40, 119, 194, 115, 147, 153, 150, 41, 239, 102, 117, 174, 175, 151, 78,
        87, 240, 42, 211, 166, 233, 101, 105, 112, 184, 187, 43, 93, 48, 152, 149, 212, 111,
        164, 100, 116, 32, 129, 213, 173, 88, 139, 132, 106, 224, 76, 193, 214, 49, 84, 250,
        253, 181, 80, 198, 146, 133, 81, 216, 86, 163, 217, 168, 52, 218, 122, 219, 167, 221,
        96, 46, 169, 157, 108, 205, 75, 64, 186, 156, 104, 103, 192, 161, 179, 47, 183, 177,
        89, 226, 254, 225, 90, 99, 227, 191, 124, 51, 60, 128, 172, 165, 249, 229, 50, 171,
        234, 53, 65, 155, 176, 59, 79, 241, 72, 230, 182, 61, 231, 232, 92, 220, 143, 210,
        62, 127, 236, 68, 222, 154, 223, 237, 113, 63, 94, 242, 251, 235, 245, 71, 120, 69,
        74, 162, 137, 73, 158, 170, 190, 188, 238, 138, 208, 107, 207, 118, 95, 82, 134, 178,
        203, 126, 70, 185, 145, 123, 34, 109, 136, 215, 244, 252, 83, 159, 131, 209, 246, 247,
    },
};
const std::uint32_t kLayoutGroups[3] = {54, 54, 0};

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
        const std::string v = text(key);
        if (v == "yes" || v == "true" || v == "1") return true;
        if (v == "no" || v == "false" || v == "0") return false;
        return def;
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
        for (int i = 0; i < 4; ++i) o.w32(kCtgpCourseTable + 4 * i, kCourseHead[i]);
        for (int i = 0; i < 28; ++i) o.w32(kCtgpCourseTable + 16 + 4 * i, kCourseTail[i]);
        // 0, 1 or 2 (out of range falls back to 0); layout 2 has no groups.
        const int layout = s.cup_course_layout_using >= 0 && s.cup_course_layout_using <= 2 ? s.cup_course_layout_using
                                                                                            : 0;
        const std::uint32_t groups = kLayoutGroups[layout];
        for (std::uint32_t i = 0; i < groups * 4; ++i)
            o.w32(kCtgpLayoutWords + 4 * i, kLayouts[layout][i]);
        o.w32(kCtgpLayoutCount, groups + 8);

        // Online play is off for every launch from RiftWii (the CTGP-R
        // developers' condition): the game's network calls are stubbed to
        // return -76, as the channel does when it has no network settings,
        // and the LAN path (0xFE in the two words) is never taken, whatever
        // config.ini says. Nothing in RiftWii's settings turns this on.
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
    out.push_back({s.ctgpr_enabled ? "/PACKAGES/CTGPR/FST.BIN" : "/PACKAGES/CTGPR/no_ct_24p.bin", "CTGPR"});
    if (s.my_stuff == 1) out.push_back({"/PACKAGES/MYSTUFF/FST.BIN", ""});
    if (s.no_music) out.push_back({"/PACKAGES/NOMUSIC/FST.BIN", ""});
    if (s.font == 1) out.push_back({"/PACKAGES/FONT/fontChalky.bin", ""});
    if (s.font == 2) out.push_back({"/PACKAGES/FONT/fontOutline.bin", ""});
    if (s.font == 3) out.push_back({"/PACKAGES/FONT/fontMVBoli.bin", ""});
    if (s.my_stuff == 2) out.push_back({"/PACKAGES/MYSTUFF/fst_no_tracks.bin", ""});
    if (s.my_stuff == 3) out.push_back({"/PACKAGES/MYSTUFF/fst_brstm.bin", ""});
    out.push_back({"/PACKAGES/CORE/FST.BIN", "CORE"});
    return out;
}

}  // namespace riftwii
