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
// The last line SaveSettings writes: a .part without it was cut off.
constexpr const char* kEndLine = "# end\n";
bool g_loaded = false;
LoaderSettings g_settings;

bool EndsWith(const std::string& text, const std::string& ending) {
    return text.size() >= ending.size() && text.compare(text.size() - ending.size(), ending.size(), ending) == 0;
}

// settings.txt, else a whole .part (a save cut off after it was written),
// else .bak (a save cut off before the new copy was in place).
bool ReadSavedSettings(std::string& text) {
    if (ReadTextFile(kPath, text)) return true;
    if (ReadTextFile(std::string(kPath) + ".part", text) && EndsWith(text, kEndLine)) return true;
    return ReadTextFile(std::string(kPath) + ".bak", text);
}

}  // namespace

LoaderSettings& Settings() {
    if (!g_loaded) {
        g_loaded = true;
        std::string text;
        if (ReadSavedSettings(text)) g_settings.parse(text);
    }
    return g_settings;
}

void ReloadSettings() {
    g_settings = LoaderSettings{};
    g_loaded = true;
    std::string text;
    if (ReadSavedSettings(text)) g_settings.parse(text);
}

// Written whole beside it, then put in its place (ReplaceWholeFile):
// truncating it first left it empty after a power cut or a full card, and
// every setting reset. kEndLine marks a whole copy, so a .part left by a
// failed last rename is read at the next start (ReadSavedSettings).
bool SaveSettings() {
    mkdir("sd:/riftwii", 0777);
    const std::string text = Settings().serialize() + kEndLine;
    return ReplaceWholeFile(kPath, text.data(), text.size());
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
