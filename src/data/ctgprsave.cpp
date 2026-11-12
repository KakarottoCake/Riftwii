// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgprsave.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace riftwii {
namespace {

bool same_name(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

bool has(const std::vector<std::string>& names, const std::string& name) {
    for (const std::string& n : names)
        if (same_name(n, name)) return true;
    return false;
}

constexpr const char* kTempEnding = ".rwtmp";

}  // namespace

char ctgpr_save_region(const std::string& game_id) {
    if (game_id.size() < 4 || game_id.compare(0, 3, "RMC") != 0) return 0;
    const char r = game_id[3];
    return (r == 'E' || r == 'J' || r == 'K' || r == 'P') ? r : 0;
}

std::string ctgpr_sd_save_folder(char region) {
    return std::string("sd:/riivolution/save/RMC") + region;
}

std::string ctgpr_nand_data_dir(char region) {
    char text[48] = {};
    std::snprintf(text, sizeof(text), "/title/00010004/524d43%02x/data", static_cast<unsigned>(region) & 0xFFu);
    return text;
}

std::string ctgpr_save_folder_label(const std::string& game_id) {
    const char r = ctgpr_save_region(game_id);
    return r == 0 ? std::string() : std::string("RMC") + r;
}

bool ctgpr_seed_skipped(const std::vector<std::string>& on_card) {
    return has(on_card, "rksys.dat");
}

std::string ctgpr_seed_temp_name(const std::string& name) {
    return name + kTempEnding;
}

std::vector<std::string> ctgpr_seed_plan(const std::vector<std::string>& nand_names,
                                         const std::vector<std::string>& on_card) {
    std::vector<std::string> plan;
    bool rksys = false;
    for (const std::string& n : nand_names) {
        if (n.empty() || n == "." || n == ".." || n.find('/') != std::string::npos ||
            n.find('\\') != std::string::npos || n.find(':') != std::string::npos)
            continue;
        // A leftover of an earlier copy that stopped: never taken for a save.
        if (n.size() > std::strlen(kTempEnding) &&
            same_name(n.substr(n.size() - std::strlen(kTempEnding)), kTempEnding))
            continue;
        if (has(on_card, n) || has(plan, n)) continue;
        if (same_name(n, "rksys.dat")) {
            rksys = true;
            continue;
        }
        plan.push_back(n);
    }
    if (rksys) plan.push_back("rksys.dat");
    return plan;
}

}  // namespace riftwii
