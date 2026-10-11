// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "loadersettings.hpp"

#include <ogc/conf.h>
#include "video.h"
#include <sys/stat.h>

#include <cstdio>

#include "textfile.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kPath = "sd:/riftwii/settings.txt";
bool g_loaded = false;
LoaderSettings g_settings;

}  // namespace

LoaderSettings& Settings() {
    if (!g_loaded) {
        g_loaded = true;
        std::string text;
        // A save cut off between its two steps leaves only the new copy.
        if (ReadTextFile(kPath, text) || ReadTextFile(std::string(kPath) + ".part", text)) g_settings.parse(text);
    }
    return g_settings;
}

// Written whole beside it, then put in its place: truncating it first left
// it empty after a power cut or a full card, and every setting reset.
bool SaveSettings() {
    mkdir("sd:/riftwii", 0777);
    const std::string temp = std::string(kPath) + ".part";
    FILE* f = std::fopen(temp.c_str(), "wb");
    if (!f) return false;
    const std::string text = Settings().serialize();
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = std::fclose(f) == 0 && ok;
    if (ok) {
        std::remove(kPath);
        ok = std::rename(temp.c_str(), kPath) == 0;
    }
    if (!ok) std::remove(temp.c_str());
    return ok;
}

std::string MenuLanguage() {
    const std::string& chosen = Settings().language;
    if (chosen != "auto") return chosen;
    switch (CONF_GetLanguage()) {
        case CONF_LANG_JAPANESE: return "ja";
        case CONF_LANG_SPANISH: return "es";
        case CONF_LANG_ITALIAN: return "it";
        case CONF_LANG_KOREAN: return "ko";
        case CONF_LANG_FRENCH: return "fr";
        default: return "en";  // the Wii has no Portuguese; pick it in Settings
    }
}

bool MenuWidescreen() {
    const std::string& w = Settings().menu_widescreen;
    if (w != "auto") return w == "on";
    return CONF_GetAspectRatio() == CONF_ASPECT_16_9;
}

void ApplyMenuDisplay() {
    // 16:9: the 4:3 menu drawn 3/4 as wide, so it keeps its shape; then
    // smaller both ways by the screen size.
    const float size = Settings().screen_size / 100.0f;
    Menu_SetDisplayScale((MenuWidescreen() ? 0.75f : 1.0f) * size, size);
}

}  // namespace riftwii::wii
