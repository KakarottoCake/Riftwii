// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "banners.hpp"

#include <gccore.h>
#include <sys/stat.h>

#include <cstdio>
#include <set>

#include "log.hpp"
#include "riftwii/bnr.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kDir = "sd:/riftwii/banners";
constexpr std::size_t kMaxBanner = 4u << 20;

std::set<std::string> g_failed;

std::string PathOf(const std::string& id) { return std::string(kDir) + "/" + id + ".bnr"; }

bool ValidId(const std::string& id) {
    if (id.size() != 6) return false;
    for (char c : id)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    return true;
}

}  // namespace

bool BannerWanted(const std::string& game_id) {
    if (!ValidId(game_id) || g_failed.count(game_id)) return false;
    struct stat st;
    return stat(PathOf(game_id).c_str(), &st) != 0;
}

bool StoreBanner(const ImageGame& game, std::string& error) {
    if (!ValidId(game.id)) {
        error = "no game ID";
        return false;
    }
    std::vector<std::uint8_t> bnr;
    OpeningBanner check;
    if (!read_image_disc_file(game, "/opening.bnr", bnr, error) ||
        !parse_opening_bnr(bnr.data(), bnr.size(), check, error, false)) {
        g_failed.insert(game.id);
        logf("Banner of %s: %s\n", game.id.c_str(), error.c_str());
        return false;
    }
    mkdir("sd:/riftwii", 0777);
    mkdir(kDir, 0777);
    const std::string path = PathOf(game.id), temp = path + ".part";
    FILE* f = std::fopen(temp.c_str(), "wb");
    const bool ok = f && std::fwrite(bnr.data(), 1, bnr.size(), f) == bnr.size();
    if (f) std::fclose(f);
    if (!ok || std::rename(temp.c_str(), path.c_str()) != 0) {
        std::remove(temp.c_str());
        g_failed.insert(game.id);
        error = "cannot write " + path + " (card full?)";
        return false;
    }
    logf("Banner of %s stored (%u bytes)\n", game.id.c_str(), static_cast<unsigned>(bnr.size()));
    return true;
}

bool LoadBanner(const std::string& game_id, std::vector<std::uint8_t>& out) {
    if (!ValidId(game_id)) return false;
    FILE* f = std::fopen(PathOf(game_id).c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    bool ok = size > 0 && static_cast<std::size_t>(size) <= kMaxBanner;
    if (ok) {
        out.resize(static_cast<std::size_t>(size));
        ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    }
    std::fclose(f);
    return ok;
}

std::string BannerLanguageCode() {
    static const char* const kCodes[] = {"JPN", "ENG", "GER", "FRA", "SPA", "ITA", "NED", "CHN", "CHN", "KOR"};
    const int l = BannerLanguageIndex();
    return kCodes[l];
}

int BannerLanguageIndex() {
    const int l = CONF_GetLanguage();
    return l >= 0 && l < kBnrLanguages ? l : kBnrEnglish;
}

}  // namespace riftwii::wii
