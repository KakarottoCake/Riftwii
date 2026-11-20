// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "homebrewart.hpp"

#include <ogc/mutex.h>
#include <sys/stat.h>

#include <array>
#include <map>
#include <utility>

#include "boxart.hpp"
#include "cover3d.hpp"
#include "covers.hpp"
#include "riftwii/appmeta.hpp"

namespace riftwii::wii {
namespace {

struct AppArt {
    std::array<std::string, 3> names;
    std::string gametdb_id;
};

std::map<std::string, AppArt> g_art;
mutex_t g_artLock = LWP_MUTEX_NULL;

std::string FindPicture(const char* directory, const std::vector<std::string>& names) {
    for (const std::string& name : names) {
        const std::string path = std::string("sd:/riftwii/") + directory + "/" + name + ".png";
        struct stat info {};
        if (stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0) return name;
    }
    return "";
}

}  // namespace

void ConfigureHomebrewArt(const std::vector<HomebrewApp>& apps, bool rescan) {
    if (g_artLock == LWP_MUTEX_NULL && LWP_MutexInit(&g_artLock, false) != 0) return;
    std::map<std::string, AppArt> next;
    std::vector<std::string> changed;
    for (const HomebrewApp& app : apps) {
        const std::string key = app_tile_key(app.folder);
        if (key.empty()) continue;
        AppArt art;
        LWP_MutexLock(g_artLock);
        const auto previous = g_art.find(key);
        if (previous != g_art.end()) art = previous->second;
        LWP_MutexUnlock(g_artLock);
        if (rescan) {
            const std::string folder = app.folder.substr(app.folder.find_last_of('/') + 1);
            const auto names = app_cover_names(app.meta_name, folder);
            art.names[0] = FindPicture("covers", names);
            art.names[1] = FindPicture("covers3d", names);
            art.names[2] = FindPicture("boxes", names);
        }
        if (rescan || art.gametdb_id != app.gametdb_id) changed.push_back(key);
        art.gametdb_id = app.gametdb_id;
        next.emplace(key, std::move(art));
    }
    // The PNG cutters resolve their source and cache paths more than once
    // during a read. Let that read finish before changing the names. The
    // menu's ResumeGui releases the readers after this scan is published.
    CoverLoaderHold();
    LWP_MutexLock(g_artLock);
    for (const auto& previous : g_art) {
        if (next.count(previous.first) == 0) changed.push_back(previous.first);
    }
    g_art.swap(next);
    LWP_MutexUnlock(g_artLock);
    // Release the registry lock before the cover pool's lock: its reader
    // asks this registry while a scan publishes the next set of apps.
    for (const std::string& key : changed) {
        ForgetCover(key);
        ForgetBox(key);
        ForgetCover3d(key);
    }
}

std::string HomebrewArtName(const std::string& key, HomebrewArt kind) {
    if (g_artLock == LWP_MUTEX_NULL) return "";
    LWP_MutexLock(g_artLock);
    const auto found = g_art.find(key);
    const std::string name = found == g_art.end() ? "" : found->second.names[static_cast<std::size_t>(kind)];
    LWP_MutexUnlock(g_artLock);
    return name;
}

std::string HomebrewCoverId(const std::string& key) {
    if (g_artLock == LWP_MUTEX_NULL) return "";
    LWP_MutexLock(g_artLock);
    const auto found = g_art.find(key);
    const std::string id = found == g_art.end() ? "" : found->second.gametdb_id;
    LWP_MutexUnlock(g_artLock);
    return id;
}

std::vector<std::string> HomebrewCoverKeys(const std::string& gametdb_id) {
    std::vector<std::string> keys;
    if (gametdb_id.empty() || g_artLock == LWP_MUTEX_NULL) return keys;
    LWP_MutexLock(g_artLock);
    for (const auto& app : g_art) {
        if (app.second.gametdb_id == gametdb_id) keys.push_back(app.first);
    }
    LWP_MutexUnlock(g_artLock);
    return keys;
}

}  // namespace riftwii::wii
