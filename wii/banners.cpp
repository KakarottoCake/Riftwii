// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "banners.hpp"

#include <gccore.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
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

bool LoadBannerIcon(const std::string& game_id, std::vector<std::uint8_t>& out) {
    if (!ValidId(game_id)) return false;
    FILE* f = std::fopen(PathOf(game_id).c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    bool found = false;
    if (size > 0x700 && static_cast<std::size_t>(size) <= kMaxBanner) {
        out.assign(static_cast<std::size_t>(size), 0);
        const auto read_at = [&](std::size_t off, std::size_t n) {
            return off <= out.size() && n <= out.size() - off && std::fseek(f, static_cast<long>(off), SEEK_SET) == 0 &&
                   std::fread(out.data() + off, 1, n, f) == n;
        };
        const auto be32 = [&](std::size_t at) {
            return (std::uint32_t(out[at]) << 24) | (std::uint32_t(out[at + 1]) << 16) |
                   (std::uint32_t(out[at + 2]) << 8) | out[at + 3];
        };
        // IMET at 0x40 (a disc's) or 0x80 (a channel's); the archive after its 0x600 bytes.
        std::size_t imet = 0;
        if (read_at(0, 0x84)) {
            if (std::memcmp(out.data() + 0x40, "IMET", 4) == 0) imet = 0x40;
            else if (std::memcmp(out.data() + 0x80, "IMET", 4) == 0) imet = 0x80;
        }
        const std::size_t archive = imet + 0x5C0;
        if (imet != 0 && read_at(0, archive + 0x20) && be32(archive) == 0x55AA382Du) {
            const std::size_t root = be32(archive + 4), header = be32(archive + 8);
            if (root >= 0x20 && header >= 12 && read_at(archive + root, header)) {
                const std::size_t nodes = archive + root, count = be32(nodes + 8);
                if (count >= 1 && count * 12 <= header) {
                    const std::size_t strings = nodes + count * 12, strings_end = nodes + header;
                    for (std::size_t n = 1; n < count && !found; ++n) {
                        const std::size_t e = nodes + n * 12, name = strings + (be32(e) & 0xFFFFFF);
                        if (out[e] != 0 || name + 9 > strings_end || std::memcmp(out.data() + name, "icon.bin", 9) != 0)
                            continue;
                        found = read_at(archive + be32(e + 4), be32(e + 8));
                        if (!found) break;
                    }
                }
            }
        }
    }
    std::fclose(f);
    return found || LoadBanner(game_id, out);
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
