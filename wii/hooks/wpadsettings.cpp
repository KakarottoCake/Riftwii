// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "wpadhook.hpp"

#include "hidpads.hpp"
#include "loadersettings.hpp"

namespace riftwii::wii {
namespace {

bool g_wiiu_pro_seen = false;

}  // namespace

void note_wiiu_pro_seen() { g_wiiu_pro_seen = true; }

bool pro_translator_wanted() {
    const auto it = Settings().other.find("pro_controller");
    return it == Settings().other.end() || it->second != "off";
}

bool pro_translator_every_link() {
    const auto it = Settings().other.find("pro_controller");
    return it != Settings().other.end() && it->second == "every";
}

bool wpad_log_wanted(std::size_t pro_pairings, bool pad_hook_active, std::string& why) {
    const auto it = Settings().other.find("wpad_log");
    const std::string mode = it == Settings().other.end() ? "auto" : it->second;
    if (mode == "on") {
        why = "wpad_log = on";
        return true;
    }
    if (mode == "off") {
        why = "wpad_log = off";
        return false;
    }
    if (g_wiiu_pro_seen) {
        why = "a Wii U Pro Controller was connected in the menu";
        return true;
    }
    if (pad_hook_active && HidPadsConnected()) {
        why = "a mapped USB controller is plugged in at launch; recording its diagnostics";
        return true;
    }
    // A pairing alone is not enough: the log takes 43 KB of the game's MEM2
    // and the blob 10 KB of MEM1, and Newer Super Mario Bros. Wii from an
    // RVZ ran out of MEM2 on a Wii with a Pro paired but not in use.
    why = pro_pairings > 0 ? std::to_string(pro_pairings) +
                                 " Wii U Pro Controller pairing(s), but none was connected in the menu"
                           : "no Wii U Pro Controller connected in the menu";
    return false;
}

}  // namespace riftwii::wii
