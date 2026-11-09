// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpapply.hpp"

#include <algorithm>

#include <cstdlib>

#include "riftwii/ctgpfields.hpp"
#include "riftwii/ctgptables.hpp"

namespace riftwii {
namespace {

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

std::vector<CtgpOp> ctgp_apply_settings(const CtgpSettings& s, const CtgpTables& tables) {
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
        for (std::size_t i = 0; i < kCtgpCourseWords; ++i)
            o.w32(kCtgpCourseTable + 4 * static_cast<std::uint32_t>(i), tables.course[i]);
        // Layout 2 has no groups.
        const int layout = s.cup_course_layout_using == 0 || s.cup_course_layout_using == 1 ? s.cup_course_layout_using
                                                                                            : -1;
        const std::uint32_t groups = layout >= 0 ? tables.layouts[layout].groups : 0;
        for (std::uint32_t i = 0; i < groups * 4; ++i)
            o.w32(kCtgpLayoutWords + 4 * i, tables.layouts[layout].tracks[i]);
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
