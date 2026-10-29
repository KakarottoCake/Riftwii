// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "emupicture.hpp"

#include <cstdio>
#include <cerrno>
#include <sys/stat.h>

#include "log.hpp"
#include "netsock.hpp"
#include "online.hpp"
#include "textfile.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/emulatorart.hpp"

namespace riftwii::wii {
namespace {

bool download_text(const std::string& url, std::string& text, std::string& error) {
    std::vector<std::uint8_t> bytes;
    if (!HttpGet(url, bytes, error, kEmulatorIndexMax, 10000, nullptr, HttpRedirects::Never) ||
        !try_reserve(text, bytes.size())) return false;
    text.assign(bytes.begin(), bytes.end());
    return !NetCancelRequested();
}

bool api_text(const std::string& path, const std::string& key, std::string& text, std::string& error) {
    std::vector<std::uint8_t> bytes;
    if (!HttpSteamGridGet(path, key, bytes, error) || !try_reserve(text, bytes.size())) return false;
    text.assign(bytes.begin(), bytes.end());
    return !NetCancelRequested();
}

bool read_png(const std::string& path, std::vector<std::uint8_t>& png) {
    struct stat info {};
    if (lstat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0 ||
        static_cast<std::uint64_t>(info.st_size) > kEmulatorCoverMax) return false;
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    const bool ok = try_resize(png, static_cast<std::size_t>(info.st_size)) &&
                    std::fread(png.data(), 1, png.size(), file) == png.size();
    std::fclose(file);
    return ok;
}

bool make_parents(const std::string& path) {
    for (auto slash = path.find('/', 4); slash != std::string::npos; slash = path.find('/', slash + 1)) {
        const std::string directory = path.substr(0, slash);
        struct stat info {};
        if (lstat(directory.c_str(), &info) == 0) {
            if (!S_ISDIR(info.st_mode)) return false;
        } else if (mkdir(directory.c_str(), 0777) != 0) {
            return false;
        }
    }
    return true;
}

bool replace_file(const std::string& path, const void* data, std::size_t size) {
    const std::string temporary = path + ".part";
    if (!WriteWholeFile(temporary, data, size)) {
        std::remove(temporary.c_str());
        return false;
    }
    // Keep the previous picture until the completed replacement is on
    // disk. A failed rename restores it instead of losing a hand-made PNG.
    const std::string backup = path + ".old";
    struct stat info {};
    const bool existed = lstat(path.c_str(), &info) == 0;
    if (existed && (!S_ISREG(info.st_mode) || std::rename(path.c_str(), backup.c_str()) != 0)) {
        std::remove(temporary.c_str());
        return false;
    }
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        // Putting the old picture back can fail too: then its .old copy is
        // the only one left, so it stays, and the log says where it is.
        if (existed && std::rename(backup.c_str(), path.c_str()) != 0)
            logf("Picture picker: the new picture could not be saved and the old one could not be put back; it is kept as %s.\n",
                 backup.c_str());
        std::remove(temporary.c_str());
        return false;
    }
    if (existed) std::remove(backup.c_str());
    return true;
}

}  // namespace

bool FindEmulatorPictures(const std::string& system, const std::string& game, bool steamgrid,
                          std::vector<PictureCandidate>& candidates, std::string& error) {
    candidates.clear();
    bool answered = false;
    if (!system.empty()) {
        for (const auto source : {PictureSource::Title, PictureSource::Screenshot, PictureSource::Boxart}) {
            if (NetCancelRequested()) return false;
            const std::string url = picture_libretro_url(source, system);
            if (url.empty()) continue;
            std::string text;
            std::vector<std::string> names;
            if (download_text(url, text, error) && parse_libretro_index(text, names)) {
                if (!picture_add_libretro(candidates, source, system, game, names)) return false;
                answered = true;
            }
        }
    }
    if (steamgrid && !NetCancelRequested()) {
        std::string key;
        if (ReadTextFile("sd:/riftwii/steamgriddb.txt", key, 132)) {
            if (!key.empty() && key.back() == '\n') key.pop_back();
            if (!key.empty() && key.back() == '\r') key.pop_back();
        }
        if (steamgrid_key_valid(key)) {
            std::string search = game.substr(0, game.find_first_of("(["));
            while (!search.empty() && search.back() == ' ') search.pop_back();
            std::string text;
            std::vector<SteamGridGame> games;
            if (!search.empty() && api_text("/api/v2/search/autocomplete/" + url_encode(search), key, text, error) &&
                parse_steamgrid_search(text, games)) {
                const auto id = match_steamgrid_game(game, games);
                answered = true;
                if (id && !NetCancelRequested()) {
                    text.clear();
                    std::vector<std::string> urls;
                    if (api_text("/api/v2/grids/game/" + std::to_string(id) +
                                 "?dimensions=460x215,920x430&mimes=image/png", key, text, error) &&
                        parse_steamgrid_grids(text, urls, true) && !picture_add_steamgrid(candidates, urls)) return false;
                }
            }
        }
    }
    if (NetCancelRequested()) return false;
    logf("Picture picker: found %u candidates; no pictures have been downloaded.\n", static_cast<unsigned>(candidates.size()));
    return answered || !candidates.empty();
}

OwnPicture OwnPictureState(const std::string& path) {
    if (!picture_save_path_valid(path)) return OwnPicture::Missing;
    struct stat info {};
    if (lstat(path.c_str(), &info) != 0) return errno == ENOENT ? OwnPicture::Missing : OwnPicture::Hand;
    std::vector<std::uint8_t> png;
    std::string marker;
    if (read_png(path, png) && ReadTextFile(path + ".pick", marker, 96) && picture_pick_marker_matches(png, marker))
        return OwnPicture::Picker;
    return OwnPicture::Hand;
}

bool SavePickedPicture(const std::string& path, const std::vector<std::uint8_t>& png) {
    if (!picture_save_path_valid(path) || png.empty() || png.size() > kEmulatorCoverMax || !make_parents(path)) return false;
    const std::string marker = picture_pick_marker(png);
    CardWriteHold hold;
    if (!replace_file(path, png.data(), png.size())) return false;
    // A failed note is conservative: Back to automatic will ask first.
    if (!replace_file(path + ".pick", marker.data(), marker.size())) std::remove((path + ".pick").c_str());
    logf("Picture picker: saved the chosen picture to %s.\n", path.c_str());
    return true;
}

bool RemovePickedPicture(const std::string& path, bool allow_hand) {
    if (!picture_save_path_valid(path)) return false;
    CardWriteHold hold;
    const OwnPicture own = OwnPictureState(path);
    if (own == OwnPicture::Hand && !allow_hand) return false;
    if (own != OwnPicture::Missing) {
        struct stat info {};
        if (lstat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || std::remove(path.c_str()) != 0) return false;
    }
    std::remove((path + ".pick").c_str());
    logf("Picture picker: removed the own picture; automatic pictures can be used again.\n");
    return true;
}

}  // namespace riftwii::wii
