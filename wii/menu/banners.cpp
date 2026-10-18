// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "banners.hpp"

#include <gccore.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>
#include <set>

#include "gclaunch.hpp"
#include "log.hpp"
#include "memlimits.hpp"
#include "netsock.hpp"
#include "online.hpp"
#include "riftwii/bnr.hpp"
#include "riftwii/gcbanner.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kDir = "sd:/riftwii/banners";
constexpr std::size_t kMaxBanner = 4u << 20;
// A stored file this small is a GameCube disc's own banner (BNR2 is 8 KB),
// not an animated one (hundreds of kilobytes): one to replace by download.
constexpr long kMaxDiscPicture = 16 << 10;
// A game the banner server has no banner for is asked about again after a week.
constexpr std::time_t kRetryAfter = 7 * 24 * 60 * 60;

std::set<std::string> g_failed;
// GameCube games whose download found nothing this session.
std::set<std::string> g_downloadMissed;

std::string PathOf(const std::string& id) { return std::string(kDir) + "/" + id + ".bnr"; }
std::string MissPath(const std::string& id) { return std::string(kDir) + "/" + id + ".none"; }

bool ValidId(const std::string& id) {
    if (id.size() != 6) return false;
    for (char c : id)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    return true;
}

// Through a .part file, so a write cut short never leaves half a banner
// that the menu would try to play; whatever was stored before is replaced.
bool WriteBanner(const std::string& id, const std::vector<std::uint8_t>& bytes, std::string& error) {
    mkdir("sd:/riftwii", 0777);
    mkdir(kDir, 0777);
    const std::string path = PathOf(id), temp = path + ".part";
    FILE* f = std::fopen(temp.c_str(), "wb");
    const bool written = f && std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    if (f) std::fclose(f);
    if (written) std::remove(path.c_str());
    if (!written || std::rename(temp.c_str(), path.c_str()) != 0) {
        std::remove(temp.c_str());
        error = "cannot write " + path + " (card full?)";
        return false;
    }
    return true;
}

// A GameCube game's disc banner: from its image (an ISO, or the disc an
// RVZ holds), or for a patch-only mod (wii/launch/gcpatch.hpp) from the
// original the patch is made from.
bool ReadGameCubeDiscBanner(const ImageGame& game, std::vector<std::uint8_t>& bnr, std::string& error) {
    const std::string& image = game.gc_patch_source.empty() ? game.path : game.gc_patch_source;
    std::unique_ptr<ByteSource> disc;
    if (!OpenGcDisc(image, disc, error)) return false;
    if (!read_gc_disc_file(*disc, "/opening.bnr", bnr, error)) return false;
    GcBanner check;
    return parse_gc_banner(bnr.data(), bnr.size(), false, check, error);
}

}  // namespace

bool BannerWanted(const std::string& game_id) {
    if (!ValidId(game_id) || g_failed.count(game_id)) return false;
    struct stat st;
    return stat(PathOf(game_id).c_str(), &st) != 0;
}

namespace {

// Checks a banner read for `id` and writes it as sd:/riftwii/banners/<ID>.bnr.
// A failure is remembered for the session, so the game is not tried again.
bool StoreBannerBytes(const std::string& id, std::vector<std::uint8_t>& bnr, bool read, std::string& error) {
    OpeningBanner check;
    bool parsed = false;
    try {
        parsed = read && parse_opening_bnr(bnr.data(), bnr.size(), check, error, false);
    } catch (const std::bad_alloc&) {
        error = "out of memory for the banner";
    }
    if (!parsed) {
        g_failed.insert(id);
        logf("Banner of %s: %s\n", id.c_str(), error.c_str());
        return false;
    }
    mkdir("sd:/riftwii", 0777);
    mkdir(kDir, 0777);
    const std::string path = PathOf(id), temp = path + ".part";
    FILE* f = std::fopen(temp.c_str(), "wb");
    const bool ok = f && std::fwrite(bnr.data(), 1, bnr.size(), f) == bnr.size();
    if (f) std::fclose(f);
    if (!ok || std::rename(temp.c_str(), path.c_str()) != 0) {
        std::remove(temp.c_str());
        g_failed.insert(id);
        error = "cannot write " + path + " (card full?)";
        return false;
    }
    logf("Banner of %s stored (%u bytes)\n", id.c_str(), static_cast<unsigned>(bnr.size()));
    return true;
}

}  // namespace

bool StoreBanner(const ImageGame& game, std::string& error) {
    if (!ValidId(game.id)) {
        error = "no game ID";
        return false;
    }
    std::vector<std::uint8_t> bnr;
    if (game.gamecube) {
        // A GameCube disc's banner is a BNR1/BNR2 picture, not a Wii
        // opening.bnr, so it is checked and stored on its own path.
        bool parsed = false;
        try {
            parsed = ReadGameCubeDiscBanner(game, bnr, error);
        } catch (const std::bad_alloc&) {
            error = "out of memory for the banner";
        }
        if (!parsed) {
            g_failed.insert(game.id);
            logf("Banner of %s: %s\n", game.id.c_str(), error.c_str());
            return false;
        }
        if (!WriteBanner(game.id, bnr, error)) {
            g_failed.insert(game.id);
            return false;
        }
        logf("Banner of %s stored (%u bytes, the GameCube disc's own picture)\n", game.id.c_str(),
             static_cast<unsigned>(bnr.size()));
        return true;
    }
    bool read = false;
    try {
        read = read_image_disc_file(game, "/opening.bnr", bnr, error);
    } catch (const std::bad_alloc&) {
        error = "out of memory for the banner";
    }
    return StoreBannerBytes(game.id, bnr, read, error);
}

bool StoreChannelBanner(const NandChannel& channel, std::string& error) {
    if (!ValidId(channel.game_id)) {
        error = "no game ID";
        return false;
    }
    std::vector<std::uint8_t> bnr;
    bool read = false;
    try {
        read = read_nand_channel_banner(channel, bnr, error);
    } catch (const std::bad_alloc&) {
        error = "out of memory for the banner";
    }
    return StoreBannerBytes(channel.game_id, bnr, read, error);
}

bool GameCubeBannerWanted(const std::string& game_id) {
    if (!ValidId(game_id) || g_downloadMissed.count(game_id)) return false;
    struct stat st;
    const bool animated = stat(PathOf(game_id).c_str(), &st) == 0 && st.st_size > kMaxDiscPicture;
    if (animated) return false;
    const bool missed_lately =
        stat(MissPath(game_id).c_str(), &st) == 0 && std::time(nullptr) - st.st_mtime < kRetryAfter;
    return !missed_lately;
}

namespace {

// The background fetch: written by its thread only between Start and the
// thread's end, read by Take only after it ended.
struct Background {
    std::string id;
    bool regional_fallback = false;
    BannerFetch got = BannerFetch::Failed;
    std::string error;
};
Background g_background;

BannerFetch FetchGameCubeBanner(const std::string& game_id, bool regional_fallback, std::string& error) {
    std::vector<std::uint8_t> bytes;
    BannerDownload answer = BannerDownload::Failed;
    try {
        answer = DownloadGameCubeBanner(game_id, regional_fallback, bytes, error);
    } catch (const std::bad_alloc&) {
        error = "out of memory for the banner";
        answer = BannerDownload::None;
    }
    if (answer == BannerDownload::Failed) return BannerFetch::Failed;
    if (answer == BannerDownload::None) {
        // An empty stamp: not asked again for a week.
        mkdir("sd:/riftwii", 0777);
        mkdir(kDir, 0777);
        if (FILE* f = std::fopen(MissPath(game_id).c_str(), "wb")) std::fclose(f);
        logf("Banners: the banner server has no animated banner for %s (%s); the disc's picture stays\n",
             game_id.c_str(), error.c_str());
        return BannerFetch::NotFound;
    }
    if (!WriteBanner(game_id, bytes, error)) return BannerFetch::Failed;
    std::remove(MissPath(game_id).c_str());
    return BannerFetch::Stored;
}

void RunBackgroundFetch() {
    g_background.got = FetchGameCubeBanner(g_background.id, g_background.regional_fallback, g_background.error);
    // A banner is checked by unpacking it whole: a few megabytes for a moment.
    mem::WatchHeap(("after the banner download of " + g_background.id).c_str());
}

}  // namespace

bool StartGameCubeBannerFetch(const std::string& game_id, bool regional_fallback) {
    if (!ValidId(game_id) || !g_background.id.empty() || NetBackgroundBusy()) return false;
    g_background = Background{};
    g_background.id = game_id;
    g_background.regional_fallback = regional_fallback;
    if (NetRunInBackground(RunBackgroundFetch)) return true;
    g_background.id.clear();
    return false;
}

const std::string& GameCubeBannerFetchGame() { return g_background.id; }

bool TakeGameCubeBannerFetch(std::string& game_id, BannerFetch& got, std::string& error) {
    // Another job may hold the network's thread once this one has ended.
    if (g_background.id.empty() || NetBackgroundBusy()) return false;
    NetWaitForBackground();
    game_id = g_background.id;
    got = g_background.got;
    error = g_background.error;
    if (got != BannerFetch::Stored) g_downloadMissed.insert(game_id);
    g_background.id.clear();
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
        std::size_t icon = 0, icon_bytes = 0;
        found = locate_banner_icon(out, read_at, icon, icon_bytes) && read_at(icon, icon_bytes);
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
