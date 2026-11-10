// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpfields.hpp"

#include <cctype>
#include <cstdlib>

namespace riftwii {
namespace {

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

#define B(key, addr, def) {key, addr, true, 0, 0, def}
#define I(key, addr, lo, hi, def) {key, addr, false, lo, hi, def}

// Order and addresses are the channel's table (chan_internal 0x80bfe57c,
// 32 bytes a record), restricted to the records whose variable lies in the
// block the game reads. Defaults: the channel's settings reset (0x8094e690).
const std::vector<CtgpField> kFields = {
    B("lanmode", 0x816ff9f4, 0),
    B("item_rain_offline", 0x816ff9fc, 0),
    B("items_land_offline", 0x816ffa00, 0),
    B("room_item_rain", 0x816ffa04, 0),
    B("room_items_land", 0x816ffa08, 0),
    B("room_high_data_rate", 0x816ffa0c, 0),
    B("room_disable_item_boxes", 0x816ffa10, 0),
    B("200cc_offline", 0x816ffa1c, 0),
    B("consistent_framerate", 0x816ffa20, 0),
    I("player_count_offline", 0x816ffa2c, 2, 24, 12),
    I("max_player_count_online", 0x816ffa30, 2, 24, 12),
    B("long_distance_names_battle_bug", 0x816ffa70, 0),
    B("room_acceptable_mii_heads", 0x816ffa90, 1),
    B("room_acceptable_draggable_blues", 0x816ffa94, 1),
    I("room_repeat_vote_prevention", 0x816ffa98, 0, 2, 1),
    B("room_shroom_glitch", 0x816ffa9c, 1),
    B("room_proximity_thunderclouds", 0x816ffaa0, 0),
    B("room_disable_final_lap_clouds", 0x816ffaa4, 0),
    B("room_ultra_shortcuts", 0x816ffaa8, 0),
    I("max_spectators", 0x816ffab0, 0, 4, 0),
    B("frameskip_monitor", 0x816ffabc, 0),
    I("menu_transition_speed", 0x816ffae0, 0, 1, 0),
    B("anti_tas_opt_out", 0x816ffae8, 0),
    B("force_exc_test", 0x816ffaec, 0),
    B("remove_glow_blur", 0x816ffaf0, 0),
    B("remove_deflicker_480p", 0x816ffaf4, 0),
};

#undef B
#undef I

bool parse_int(const std::string& v, std::int32_t& out) {
    if (v.empty()) return false;
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (*end != '\0' || n < -2147483647L || n > 2147483647L) return false;
    out = static_cast<std::int32_t>(n);
    return true;
}

}  // namespace

const std::vector<CtgpField>& ctgp_fields() { return kFields; }

bool ctgp_parse_ini_bool(const std::string& value, bool& out) {
    const std::string t = lower(value);
    if (t == "yes" || t == "true" || t == "1") { out = true; return true; }
    if (t == "no" || t == "false" || t == "0") { out = false; return true; }
    return false;
}

std::map<std::string, std::string> ctgp_parse_ini(const std::string& text) {
    std::map<std::string, std::string> out;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        const std::string line = trim(text.substr(at, end - at));
        at = end + 1;
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        out[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return out;
}

std::vector<CtgpWordWrite> ctgp_field_writes(const std::string& config_ini) {
    std::vector<std::int32_t> value;
    for (const auto& f : kFields) value.push_back(f.def);
    std::size_t at = 0;
    while (at < config_ini.size()) {
        std::size_t end = config_ini.find('\n', at);
        if (end == std::string::npos) end = config_ini.size();
        const std::string line = trim(config_ini.substr(at, end - at));
        at = end + 1;
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));
        // LAN play is multiplayer: it stays off for a launch from RiftWii,
        // whatever config.ini says.
        if (key == "lanmode") continue;
        for (std::size_t i = 0; i < kFields.size(); ++i) {
            if (key != kFields[i].key) continue;
            std::int32_t n = 0;
            if (kFields[i].is_bool) {
                bool flag = false;
                if (ctgp_parse_ini_bool(val, flag)) value[i] = flag ? 1 : 0;
            } else if (parse_int(val, n) && n >= kFields[i].min && n <= kFields[i].max) {
                value[i] = n;
            }
            break;
        }
    }
    std::vector<CtgpWordWrite> out;
    for (std::size_t i = 0; i < kFields.size(); ++i)
        out.push_back({kFields[i].address, static_cast<std::uint32_t>(value[i])});
    return out;
}

}  // namespace riftwii
