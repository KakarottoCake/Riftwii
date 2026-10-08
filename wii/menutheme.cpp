// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "menutheme.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>

#include "covers.hpp"
#include "loadersettings.hpp"
#include "log.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kThemesDir = "sd:/riftwii/themes/";
constexpr long kMaxIni = 64 * 1024;
constexpr long kMaxPng = 2 * 1024 * 1024;

Theme g_theme = default_theme();
std::string g_dir;

template <class Bytes>
bool ReadFile(const std::string& path, long max, Bytes& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    bool ok = size >= 0 && size <= max;
    if (ok) {
        out.resize(static_cast<std::size_t>(size));
        ok = size == 0 || std::fread(&out[0], 1, out.size(), f) == out.size();
    }
    std::fclose(f);
    return ok;
}

bool Exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

}  // namespace

void LoadMenuTheme() {
    g_theme = default_theme();
    g_dir.clear();
    const std::string folder = Settings().theme;
    if (folder.empty() || folder == "default") return;
    const std::string dir = std::string(kThemesDir) + folder + "/";
    std::string text;
    if (!ReadFile(dir + "theme.ini", kMaxIni, text)) {
        logf("Theme: %stheme.ini is missing or unreadable; the default look stays\n", dir.c_str());
        return;
    }
    std::vector<std::string> notes;
    g_theme = parse_theme(text, notes);
    for (const std::string& note : notes) logf("Theme: %s\n", note.c_str());
    g_dir = dir;
    logf("Theme: %s (%s)%s%s\n", g_theme.name.empty() ? folder.c_str() : g_theme.name.c_str(), dir.c_str(),
         g_theme.author.empty() ? "" : ", by ", g_theme.author.c_str());
}

const Theme& MenuTheme() { return g_theme; }
const std::string& MenuThemeDir() { return g_dir; }

std::vector<ThemeEntry> ListMenuThemes() {
    std::vector<ThemeEntry> out;
    DIR* d = opendir("sd:/riftwii/themes");
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        const std::string folder = e->d_name;
        if (folder.empty() || folder[0] == '.' || folder.size() > 64 || folder == "default") continue;
        std::string text;
        if (!ReadFile(std::string(kThemesDir) + folder + "/theme.ini", kMaxIni, text)) continue;
        std::vector<std::string> notes;
        const Theme t = parse_theme(text, notes);
        out.push_back({folder, t.name.empty() ? folder : t.name});
    }
    closedir(d);
    std::sort(out.begin(), out.end(), [](const ThemeEntry& a, const ThemeEntry& b) { return a.name < b.name; });
    return out;
}

bool ThemeImageExists(const char* name) { return !g_dir.empty() && Exists(g_dir + name + ".png"); }

bool LoadThemeImage(const char* name, int w, int h, std::vector<std::uint8_t>& rgba) {
    if (g_dir.empty()) return false;
    const std::string path = g_dir + name + ".png";
    if (!Exists(path)) return false;
    std::vector<std::uint8_t> png;
    if (!ReadFile(path, kMaxPng, png)) {
        logf("Theme: %s.png is unreadable or over 2 MiB; RiftWii paints its own\n", name);
        return false;
    }
    int pw = 0, ph = 0;
    std::string error;
    if (!DecodePngRgba(png, rgba, pw, ph, error)) {
        logf("Theme: %s.png: %s; RiftWii paints its own\n", name, error.c_str());
        rgba.clear();
        return false;
    }
    if (pw != w || ph != h) {
        logf("Theme: %s.png is %dx%d, not %dx%d; RiftWii paints its own\n", name, pw, ph, w, h);
        rgba.clear();
        rgba.shrink_to_fit();
        return false;
    }
    return true;
}

std::string MenuThemeMusic() {
    if (g_dir.empty()) return "";
    const std::string path = g_dir + "music.ogg";
    return Exists(path) ? path : "";
}

}  // namespace riftwii::wii
