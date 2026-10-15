// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "cover3d.hpp"

#include <ogc/cache.h>
#include <sys/stat.h>

#include <cstdio>
#include <ctime>
#include <new>
#include <set>
#include <unordered_map>
#include <vector>

#include "loadersettings.hpp"
#include "log.hpp"
#include "memlimits.hpp"
#include "netsock.hpp"
#include "online.hpp"
#include "riftwii/coverart.hpp"
#include "skin.hpp"
#include "video.h"

namespace riftwii::wii {
namespace {

constexpr const char* kDir = "sd:/riftwii/covers3d";
constexpr std::time_t kRetryAfter = 7 * 24 * 60 * 60;
// The view draws 13 at most (wii/menu/gui_shelf.cpp, kRingReach each side
// of the focus): room for those and the next few either way, about 2 MB of
// MEM2, taken slot by slot as the view first needs them.
constexpr int kSlots = 22;
// GameTDB's 3D covers are some 60 KB; a larger answer is not one.
constexpr std::size_t kMaxPng = 1u << 20;

std::string StorePath(const std::string& id) { return std::string(kDir) + "/" + id + ".rw3"; }
std::string MissPath(const std::string& id) { return std::string(kDir) + "/" + id + ".none"; }

bool ValidId(const std::string& id) {
    if (id.size() != 6) return false;
    for (char c : id)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    return true;
}

bool WriteAll(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    const std::string temp = path + ".part";
    FILE* f = std::fopen(temp.c_str(), "wb");
    if (!f) return false;
    const bool ok = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    std::fclose(f);
    std::remove(path.c_str());
    if (!ok || std::rename(temp.c_str(), path.c_str()) != 0) {
        std::remove(temp.c_str());
        return false;
    }
    return true;
}

CoverFetch FetchNow(const std::string& game_id, std::string& error) {
    if (!ValidId(game_id)) return CoverFetch::NotFound;
    mkdir("sd:/riftwii", 0777);
    mkdir(kDir, 0777);
    for (const std::string& region : cover_regions(game_id, MenuLanguage())) {
        std::vector<std::uint8_t> png;
        std::string why;
        if (!HttpGet(cover3d_url(region, game_id), png, why, kMaxPng, 15000)) {
            static const std::string kMissing = " answered 404";
            if (why.size() >= kMissing.size() && why.compare(why.size() - kMissing.size(), kMissing.size(), kMissing) == 0) continue;
            error = why;
            return CoverFetch::Failed;
        }
        std::vector<std::uint8_t> rgba;
        int w = 0, h = 0;
        if (!DecodePngRgba(png, rgba, w, h, why)) {
            logf("3D covers: GameTDB's %s 3D cover of %s: %s\n", region.c_str(), game_id.c_str(), why.c_str());
            continue;
        }
        const std::vector<std::uint8_t> file = make_cover3d_file(rgba.data(), w, h);
        if (file.empty()) {
            logf("3D covers: GameTDB's %s 3D cover of %s is an unusable %dx%d\n", region.c_str(), game_id.c_str(), w, h);
            continue;
        }
        if (!WriteAll(StorePath(game_id), file)) {
            error = "cannot write " + StorePath(game_id) + " (card full?)";
            return CoverFetch::Failed;
        }
        std::remove(MissPath(game_id).c_str());
        logf("3D covers: %s stored (%s, %dx%d)\n", game_id.c_str(), region.c_str(), w, h);
        return CoverFetch::Stored;
    }
    WriteAll(MissPath(game_id), {});
    logf("3D covers: GameTDB has no 3D cover of %s\n", game_id.c_str());
    return CoverFetch::NotFound;
}

struct Background {
    std::string id;
    CoverFetch got = CoverFetch::Failed;
    std::string error;
};
Background g_background;

void RunBackgroundFetch() {
    try {
        g_background.got = FetchNow(g_background.id, g_background.error);
    } catch (const std::bad_alloc&) {
        g_background.got = CoverFetch::NotFound;
        g_background.error = "out of memory";
        logf("3D covers: no room to read the 3D cover of %s\n", g_background.id.c_str());
    }
    mem::WatchHeap(("after the 3D cover download of " + g_background.id).c_str());
}

struct Slot {
    std::string id;
    u8* data = nullptr;
    unsigned used = 0;
    u32 frame = 0;  // the frame it was last drawn in
};
Slot g_slots[kSlots];
std::unordered_map<std::string, Slot*> g_index;  // the filled slots by game
unsigned g_clock = 0;
u32 g_readFrame = ~0u;           // the frame a picture was last read in
std::set<std::string> g_absent;  // not on the card (or unreadable)

}  // namespace

bool Cover3dWanted(const std::string& game_id) {
    if (!ValidId(game_id)) return false;
    struct stat st;
    if (stat(StorePath(game_id).c_str(), &st) == 0) return false;
    if (stat(MissPath(game_id).c_str(), &st) == 0 && std::time(nullptr) - st.st_mtime < kRetryAfter) return false;
    return true;
}

bool StartCover3dFetch(const std::string& game_id) {
    if (!g_background.id.empty() || NetBackgroundBusy()) return false;
    g_background = Background{};
    g_background.id = game_id;
    if (NetRunInBackground(RunBackgroundFetch)) return true;
    g_background.id.clear();
    return false;
}

const std::string& Cover3dFetchGame() { return g_background.id; }

bool TakeCover3dFetch(std::string& game_id, CoverFetch& got, std::string& error) {
    if (g_background.id.empty() || NetBackgroundBusy()) return false;
    NetWaitForBackground();
    game_id = g_background.id;
    got = g_background.got;
    error = g_background.error;
    g_background.id.clear();
    return true;
}

const u8* Cover3dTexture(const std::string& game_id) {
    if (game_id.empty()) return nullptr;
    const auto found = g_index.find(game_id);
    if (found != g_index.end()) {
        found->second->used = ++g_clock;
        found->second->frame = FrameTimer;
        return found->second->data;
    }
    if (g_readFrame == FrameTimer || g_absent.count(game_id)) return nullptr;
    // The slot drawn longest ago makes room, never one drawn in this frame
    // or the last (still on screen).
    Slot* victim = nullptr;
    for (Slot& s : g_slots) {
        if (!s.id.empty() && s.frame + 1 >= FrameTimer) continue;
        if (!victim || s.used < victim->used) victim = &s;
    }
    if (!victim) return nullptr;
    if (!victim->id.empty()) g_index.erase(victim->id);
    victim->id.clear();
    g_readFrame = FrameTimer;
    FILE* f = std::fopen(StorePath(game_id).c_str(), "rb");
    if (!f) {
        g_absent.insert(game_id);
        return nullptr;
    }
    if (!victim->data) victim->data = skin::Mem2Alloc(kCover3dPixelBytes);
    std::uint8_t header[kCover3dHeaderSize];
    const bool ok = victim->data && std::fread(header, 1, sizeof header, f) == sizeof header &&
                    cover3d_header_valid(header) && std::fread(victim->data, 1, kCover3dPixelBytes, f) == kCover3dPixelBytes;
    std::fclose(f);
    if (!ok) {
        victim->id.clear();
        victim->used = 0;
        g_absent.insert(game_id);
        return nullptr;
    }
    DCFlushRange(victim->data, kCover3dPixelBytes);
    GX_InvalidateTexAll();
    victim->id = game_id;
    victim->used = ++g_clock;
    victim->frame = FrameTimer;
    g_index[game_id] = victim;
    return victim->data;
}

void ForgetCover3d(const std::string& game_id) {
    g_absent.erase(game_id);
    const auto found = g_index.find(game_id);
    if (found == g_index.end()) return;
    found->second->id.clear();
    found->second->used = 0;
    g_index.erase(found);
}

}  // namespace riftwii::wii
