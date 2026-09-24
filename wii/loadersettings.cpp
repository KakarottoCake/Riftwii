// SPDX-License-Identifier: GPL-3.0-or-later
#include "loadersettings.hpp"

#include <ogc/conf.h>
#include <sys/stat.h>

#include <cstdio>
#include <fstream>
#include <sstream>

namespace riftwii::wii {
namespace {

constexpr const char* kPath = "sd:/riftwii/settings.txt";
bool g_loaded = false;
LoaderSettings g_settings;

}  // namespace

LoaderSettings& Settings() {
    if (!g_loaded) {
        g_loaded = true;
        std::ifstream in(kPath, std::ios::binary);
        if (in) {
            std::stringstream text;
            text << in.rdbuf();
            g_settings.parse(text.str());
        }
    }
    return g_settings;
}

bool SaveSettings() {
    mkdir("sd:/riftwii", 0777);
    FILE* f = std::fopen(kPath, "wb");
    if (!f) return false;
    const std::string text = Settings().serialize();
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    return ok;
}

std::string MenuLanguage() {
    const std::string& chosen = Settings().language;
    if (chosen != "auto") return chosen;
    switch (CONF_GetLanguage()) {
        case CONF_LANG_JAPANESE: return "ja";
        case CONF_LANG_SPANISH: return "es";
        case CONF_LANG_ITALIAN: return "it";
        default: return "en";  // the Wii has no Portuguese; pick it in Settings
    }
}

}  // namespace riftwii::wii
