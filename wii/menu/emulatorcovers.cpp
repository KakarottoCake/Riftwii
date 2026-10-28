// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "emulatorcovers.hpp"

#include <ogc/mutex.h>
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <ctime>
#include <utility>

#include "homescan.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "online.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/coverart.hpp"
#include "riftwii/emulatorart.hpp"
#include "riftwii/homebrewmatch.hpp"
#include "riftwii/sha256.hpp"
#include "textfile.hpp"
#include "usbcatalog.hpp"

namespace riftwii::wii {
namespace {

struct CoverGame {
    std::string id, name, system, covers;
    std::string file, wiiflow_covers;  // the game's file name; a WiiFlow plugin's covers folder
    bool downloads = false;
    bool steamgrid = false;
};
std::vector<CoverGame> g_games;
mutex_t g_gamesLock = LWP_MUTEX_NULL;
std::atomic<bool> g_showDownloaded{true};

// Only the network worker uses this list. Switching systems releases the
// old one; lists on the card are shared by every game and last a week.
std::string g_indexSystem;
std::vector<std::string> g_indexNames;
std::time_t g_indexTime = 0;

bool FindGame(const std::string& id, CoverGame& game) {
    if (g_gamesLock == LWP_MUTEX_NULL) return false;
    LWP_MutexLock(g_gamesLock);
    const auto found = std::find_if(g_games.begin(), g_games.end(), [&](const CoverGame& entry) { return entry.id == id; });
    const bool have = found != g_games.end();
    if (have) game = *found;
    LWP_MutexUnlock(g_gamesLock);
    return have;
}

std::string Hash(const std::string& text) {
    const auto digest = sha256(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : digest) {
        result += hex[byte >> 4];
        result += hex[byte & 15];
    }
    return result;
}

std::string CoverPath(const std::string& id) { return "sd:/riftwii/covers/" + id + ".rwc"; }
std::string MissPath(const std::string& id, const char* source) {
    return "sd:/riftwii/covers/" + id + "." + source + ".none";
}

bool WriteAtomic(const std::string& path, const void* data, std::size_t size) {
    const std::string temporary = path + ".part";
    if (!WriteWholeFile(temporary, data, size)) {
        std::remove(temporary.c_str());
        return false;
    }
    std::remove(path.c_str());
    if (std::rename(temporary.c_str(), path.c_str()) == 0) return true;
    std::remove(temporary.c_str());
    return false;
}

bool MissFresh(const std::string& id, const char* source, const std::string& identity) {
    std::string text;
    return ReadTextFile(MissPath(id, source), text, 256) &&
           emulator_art_miss_fresh(text, identity, std::time(nullptr));
}

void RememberMiss(const std::string& id, const char* source, const std::string& identity) {
    const std::string text = emulator_art_miss(identity, std::time(nullptr));
    if (!text.empty()) WriteAtomic(MissPath(id, source), text.data(), text.size());
}

bool ReadKey(std::string& key) {
    key.clear();
    // This reader reports sizes/paths only. Neither the key nor its hash is
    // sent to a log, included in an error or passed to an image server.
    if (!ReadTextFile("sd:/riftwii/steamgriddb.txt", key, 132)) return false;
    if (!key.empty() && key.back() == '\n') key.pop_back();
    if (!key.empty() && key.back() == '\r') key.pop_back();
    if (steamgrid_key_valid(key)) return true;
    key.clear();
    return false;
}

bool ReadPng(const std::string& path, std::vector<std::uint8_t>& png) {
    std::uint64_t bytes = 0;
    if (path.rfind("usb:/", 0) == 0) {
        if (!read_usb_range(path, 0, nullptr, 0, bytes) || !bytes || bytes > kEmulatorCoverMax ||
            !try_resize(png, static_cast<std::size_t>(bytes))) return false;
        return read_usb_range(path, 0, png.data(), png.size(), bytes);
    }
    struct stat info {};
    if (lstat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0 ||
        static_cast<std::uint64_t>(info.st_size) > kEmulatorCoverMax) return false;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    const bool ok = try_resize(png, static_cast<std::size_t>(info.st_size)) &&
                    std::fread(png.data(), 1, png.size(), f) == png.size();
    std::fclose(f);
    return ok;
}

bool MakeCover(const std::vector<std::uint8_t>& png, std::vector<std::uint8_t>& file) {
    if (png.empty() || png.size() > kEmulatorCoverMax) return false;
    std::vector<std::uint8_t> rgba;
    int width = 0, height = 0;
    std::string error;
    // DecodePngRgba checks IHDR's sides (at most 1024) before allocating
    // pixels or inflating. Every decoder allocation is checked.
    if (!DecodePngRgba(png, rgba, width, height, error)) return false;
    file = make_cover_file(rgba.data(), width, height);
    return file.size() == kCoverFileSize;
}

bool LocalCover(const CoverGame& game, std::vector<std::uint8_t>& file) {
    if (emulator_art_component(game.system) && emulator_art_component(game.name + ".png")) {
        const std::string relative = "/riftwii/covers/emulators/" + game.system + "/" + game.name + ".png";
        for (const char* device : {"sd:", "usb:"}) {
            std::vector<std::uint8_t> png;
            if (ReadPng(device + relative, png) && MakeCover(png, file)) return true;
        }
    }
    // A player who set the emulator up for WiiFlow Lite may have its covers
    // already: WiiFlow keeps a plugin game's front at
    // wiiflow/covers/<the plugin's coverFolder>/<file name with extension>.png.
    if (game.wiiflow_covers.empty() || !emulator_art_component(game.wiiflow_covers) ||
        !emulator_art_component(game.file + ".png")) return false;
    const std::string wiiflow = "/wiiflow/covers/" + game.wiiflow_covers + "/" + game.file + ".png";
    for (const char* device : {"sd:", "usb:"}) {
        std::vector<std::uint8_t> png;
        if (ReadPng(device + wiiflow, png) && MakeCover(png, file)) return true;
    }
    return false;
}

enum class Download { Found, Missing, Unanswered };

Download DownloadPng(const std::string& url, std::vector<std::uint8_t>& file, std::string& error) {
    std::vector<std::uint8_t> png;
    if (!HttpGet(url, png, error, kEmulatorCoverMax, 10000, nullptr, HttpRedirects::Never)) {
        const int status = http_answered_status(error);
        return status == 404 || status == 410 ? Download::Missing : Download::Unanswered;
    }
    if (!MakeCover(png, file)) {
        error = "The cover is not a readable PNG of at most 1024 pixels per side.";
        // Invalid replies and low memory are not evidence of a missing game.
        return Download::Unanswered;
    }
    return Download::Found;
}

bool LoadIndex(const std::string& system, std::string& error) {
    const std::time_t now = std::time(nullptr);
    if (g_indexSystem == system && !g_indexNames.empty() && now >= g_indexTime && now - g_indexTime < kEmulatorArtWeek) return true;
    g_indexNames.clear();
    g_indexSystem.clear();
    const std::string path = "sd:/riftwii/covers/emulators/index-" + Hash(system) + ".txt";
    struct stat info {};
    std::string text;
    if (stat(path.c_str(), &info) == 0 && now >= info.st_mtime && now - info.st_mtime < kEmulatorArtWeek &&
        ReadTextFile(path, text, kEmulatorIndexMax) && parse_emulator_index(text, g_indexNames)) {
        g_indexSystem = system;
        g_indexTime = info.st_mtime;
        return true;
    }
    text.clear();
    std::vector<std::uint8_t> body;
    if (!HttpGet(libretro_cover_url(system), body, error, kEmulatorIndexMax, 10000, nullptr, HttpRedirects::Never)) return false;
    if (!try_reserve(text, body.size())) return false;
    text.assign(body.begin(), body.end());
    std::vector<std::uint8_t>().swap(body);
    if (!parse_libretro_index(text, g_indexNames)) {
        error = "Libretro's cover list was invalid or exceeded its limits.";
        return false;
    }
    g_indexSystem = system;
    g_indexTime = now;
    text.clear();
    if (serialize_emulator_index(g_indexNames, text)) WriteAtomic(path, text.data(), text.size());
    logf("Emulator covers: read %u names for %s from libretro.\n", static_cast<unsigned>(g_indexNames.size()), system.c_str());
    return true;
}

Download LibretroCover(const CoverGame& game, std::vector<std::uint8_t>& file, std::string& error) {
    const std::string exact = libretro_cover_name(game.name);
    if (!exact.empty()) {
        const auto got = DownloadPng(libretro_cover_url(game.covers, exact), file, error);
        if (got != Download::Missing) return got;
    }
    if (!LoadIndex(game.covers, error)) return Download::Unanswered;
    const std::string match = match_emulator_cover(game.name, g_indexNames);
    if (match.empty() || match == exact) return Download::Missing;
    return DownloadPng(libretro_cover_url(game.covers, match), file, error);
}

bool ApiJson(const std::string& path, const std::string& key, std::string& json, std::string& error) {
    std::vector<std::uint8_t> body;
    if (!HttpSteamGridGet(path, key, body, error) || !try_reserve(json, body.size())) return false;
    json.assign(body.begin(), body.end());
    return true;
}

Download SteamGridCover(const CoverGame& game, const std::string& key,
                        std::vector<std::uint8_t>& file, std::string& error) {
    std::string json;
    std::vector<SteamGridGame> games;
    // Strip region/revision tags for the search, but retain the original
    // title for matching. The match helper also ignores those tags.
    std::string search = game.name.substr(0, game.name.find_first_of("(["));
    while (!search.empty() && search.back() == ' ') search.pop_back();
    if (search.empty()) return Download::Missing;
    if (!ApiJson("/api/v2/search/autocomplete/" + url_encode(search), key, json, error)) return Download::Unanswered;
    if (!parse_steamgrid_search(json, games)) {
        error = "SteamGridDB's search answer was invalid or exceeded its limits.";
        return Download::Unanswered;
    }
    const auto id = match_steamgrid_game(game.name, games);
    if (!id) return Download::Missing;
    json.clear();
    if (!ApiJson("/api/v2/grids/game/" + std::to_string(id) + "?dimensions=600x900&mimes=image/png", key, json, error)) return Download::Unanswered;
    std::vector<std::string> urls;
    if (!parse_steamgrid_grids(json, urls)) {
        error = "SteamGridDB's cover answer was invalid or exceeded its limits.";
        return Download::Unanswered;
    }
    // A broken first upload need not hide every other grid. Keep the work
    // per game small; the API may return hundreds of alternatives.
    bool unanswered = false;
    for (std::size_t i = 0; i < urls.size() && i < 3; ++i) {
        const auto got = DownloadPng(urls[i], file, error);  // no key on image requests
        if (got == Download::Found) return got;
        unanswered = unanswered || got == Download::Unanswered;
    }
    return unanswered ? Download::Unanswered : Download::Missing;
}

CoverFetch StoreCover(const CoverGame& game, const std::vector<std::uint8_t>& file,
                      const char* source, std::string& error) {
    if (!WriteAtomic(CoverPath(game.id), file.data(), file.size())) {
        error = "The emulator cover could not be saved to the SD card.";
        return CoverFetch::Failed;
    }
    logf("Emulator covers: saved %s from %s as an 80 by 112 cover.\n", game.name.c_str(), source);
    return CoverFetch::Stored;
}

}  // namespace

void ConfigureEmulatorCovers(const EmulatorCatalog& catalog) {
    if (g_gamesLock == LWP_MUTEX_NULL) LWP_MutexInit(&g_gamesLock, false);
    std::vector<CoverGame> ready;
    if (catalog.games.size() > 2048 || !try_reserve(ready, catalog.games.size())) return;
    for (const auto& game : catalog.games) {
        if (game.emulator >= catalog.addons.size() || !emulator_cover_id(game.key)) continue;
        const auto& file = catalog.addons[game.emulator].file;
        ready.push_back({game.key, game.name, file.system, file.covers,
                         game.path.substr(game.path.find_last_of('/') + 1), file.wiiflow_covers,
                         Settings().online && Settings().emulator_covers, Settings().steamgrid_covers});
    }
    LWP_MutexLock(g_gamesLock);
    g_games.swap(ready);
    LWP_MutexUnlock(g_gamesLock);
    g_showDownloaded.store(Settings().emulator_covers);
}

bool EmulatorDownloadedCoversAllowed() { return g_showDownloaded.load(); }

bool SteamGridKeyFound() {
    std::string key;
    return ReadKey(key);
}

bool EmulatorCoverWanted(const std::string& id) {
    CoverGame game;
    if (!FindGame(id, game) || !Settings().online || !Settings().emulator_covers) return false;
    struct stat info {};
    if (stat(CoverPath(id).c_str(), &info) == 0) return false;
    if (!game.covers.empty() && !MissFresh(id, "libretro", game.covers)) return true;
    std::string key;
    return Settings().steamgrid_covers && ReadKey(key) && !MissFresh(id, "steamgrid", Hash(key));
}

void ForgetEmulatorCoverMiss(const std::string& id) {
    if (!emulator_cover_id(id)) return;
    std::remove(MissPath(id, "libretro").c_str());
    std::remove(MissPath(id, "steamgrid").c_str());
}

bool ReadEmulatorLocalCover(const std::string& id, std::vector<std::uint8_t>& file) {
    // The scan owns both drives' volume state until its last part is ready.
    if (!DriveScanDone()) return false;
    CoverGame game;
    return FindGame(id, game) && LocalCover(game, file);
}

CoverFetch FetchEmulatorCover(const std::string& id, std::string& error) {
    CoverGame game;
    if (!FindGame(id, game)) return CoverFetch::NotFound;
    mkdir("sd:/riftwii", 0777);
    mkdir("sd:/riftwii/covers", 0777);
    mkdir("sd:/riftwii/covers/emulators", 0777);
    std::vector<std::uint8_t> file;
    if (LocalCover(game, file)) return StoreCover(game, file, "the card", error);
    if (!game.downloads) return CoverFetch::NotFound;
    if (!game.covers.empty() && !MissFresh(id, "libretro", game.covers)) {
        const auto got = LibretroCover(game, file, error);
        if (got == Download::Found) return StoreCover(game, file, "libretro", error);
        // A service failure is not a miss and does not authorize a fallback
        // request. Try it again after the usual download retry.
        if (got == Download::Unanswered) return CoverFetch::NoAnswer;
        RememberMiss(id, "libretro", game.covers);
    }
    std::string key;
    if (game.steamgrid && ReadKey(key) && !MissFresh(id, "steamgrid", Hash(key))) {
        const auto got = SteamGridCover(game, key, file, error);
        if (got == Download::Found) return StoreCover(game, file, "SteamGridDB", error);
        if (got == Download::Unanswered) return CoverFetch::NoAnswer;
        RememberMiss(id, "steamgrid", Hash(key));
    }
    logf("Emulator covers: no matching cover was found for %s; checked sources will be tried again in a week.\n", game.name.c_str());
    return CoverFetch::NotFound;
}

}  // namespace riftwii::wii
