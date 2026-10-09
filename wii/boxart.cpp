// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "boxart.hpp"

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

constexpr const char* kBoxDir = "sd:/riftwii/boxes";
constexpr std::time_t kRetryAfter = 7 * 24 * 60 * 60;
// A 4:3 shelf draws 27 boxes, a 16:9 one 43 (wii/gui_shelf.cpp): room for
// all of those, about 4 MB of MEM2, taken slot by slot as the shelf first
// needs them. With 32 a 16:9 shelf never fit: every frame read a box from
// the card and pushed out one still on screen, so the spines kept turning
// back into names and scrolling crawled (a tester's video, 2610-178).
constexpr int kSlots = 44;

// .rw2: the box with its back (version 1's .rwb files had no back and are left alone).
std::string BoxPath(const std::string& id) { return std::string(kBoxDir) + "/" + id + ".rw2"; }
std::string MissPath(const std::string& id) { return std::string(kBoxDir) + "/" + id + ".none"; }

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

CoverFetch FetchBoxNow(const std::string& game_id, std::string& error) {
    if (!ValidId(game_id)) return CoverFetch::NotFound;
    mkdir("sd:/riftwii", 0777);
    mkdir(kBoxDir, 0777);
    for (const std::string& region : cover_regions(game_id, MenuLanguage())) {
        std::vector<std::uint8_t> png;
        std::string why;
        // A full cover is around a megabyte.
        if (!HttpGet(coverfull_url(region, game_id), png, why, 3u << 20, 15000)) {
            static const std::string kMissing = " answered 404";
            if (why.size() >= kMissing.size() && why.compare(why.size() - kMissing.size(), kMissing.size(), kMissing) == 0) continue;
            error = why;
            return CoverFetch::Failed;
        }
        std::vector<std::uint8_t> rgba;
        int w = 0, h = 0;
        if (!DecodePngRgba(png, rgba, w, h, why)) {
            logf("Shelf: GameTDB's %s full cover of %s: %s\n", region.c_str(), game_id.c_str(), why.c_str());
            continue;
        }
        std::vector<std::uint8_t>().swap(png);
        const std::vector<std::uint8_t> file = make_box_file(rgba.data(), w, h);
        if (file.empty()) {
            logf("Shelf: GameTDB's %s full cover of %s (%dx%d) has no spine where a Wii case has it\n", region.c_str(),
                 game_id.c_str(), w, h);
            continue;
        }
        if (!WriteAll(BoxPath(game_id), file)) {
            error = "cannot write " + BoxPath(game_id) + " (card full?)";
            return CoverFetch::Failed;
        }
        std::remove(MissPath(game_id).c_str());
        logf("Shelf: box of %s (%s, %dx%d)\n", game_id.c_str(), region.c_str(), w, h);
        return CoverFetch::Stored;
    }
    WriteAll(MissPath(game_id), {});
    logf("Shelf: GameTDB has no full cover of %s\n", game_id.c_str());
    return CoverFetch::NotFound;
}

struct Background {
    std::string id;
    CoverFetch got = CoverFetch::Failed;
    std::string error;
};
Background g_background;

void RunBackgroundFetch() {
    // A full scan unpacks to a few MB: without the room, give up on this box
    // rather than end the thread.
    try {
        g_background.got = FetchBoxNow(g_background.id, g_background.error);
    } catch (const std::bad_alloc&) {
        g_background.got = CoverFetch::NotFound;
        g_background.error = "out of memory";
        logf("Shelf: no room to cut the box of %s\n", g_background.id.c_str());
    }
    mem::WatchHeap(("after the box download of " + g_background.id).c_str());
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
u32 g_readFrame = ~0u;          // the frame a box was last read in
std::set<std::string> g_absent;  // not on the card (or unreadable)

}  // namespace

bool BoxWanted(const std::string& game_id) {
    if (!ValidId(game_id)) return false;
    struct stat st;
    if (stat(BoxPath(game_id).c_str(), &st) == 0) return false;
    if (stat(MissPath(game_id).c_str(), &st) == 0 && std::time(nullptr) - st.st_mtime < kRetryAfter) return false;
    return true;
}

bool StartBoxFetch(const std::string& game_id) {
    if (!g_background.id.empty() || NetBackgroundBusy()) return false;
    g_background = Background{};
    g_background.id = game_id;
    if (NetRunInBackground(RunBackgroundFetch)) return true;
    g_background.id.clear();
    return false;
}

const std::string& BoxFetchGame() { return g_background.id; }

bool TakeBoxFetch(std::string& game_id, CoverFetch& got, std::string& error) {
    if (g_background.id.empty() || NetBackgroundBusy()) return false;
    NetWaitForBackground();
    game_id = g_background.id;
    got = g_background.got;
    error = g_background.error;
    g_background.id.clear();
    return true;
}

const u8* BoxTexture(const std::string& game_id) {
    if (game_id.empty()) return nullptr;
    const auto found = g_index.find(game_id);
    if (found != g_index.end()) {
        found->second->used = ++g_clock;
        found->second->frame = FrameTimer;
        return found->second->data;
    }
    if (g_readFrame == FrameTimer || g_absent.count(game_id)) return nullptr;
    // A read: the slot drawn longest ago makes room, but never one drawn in
    // this frame or the last (still on screen). With none free the box
    // stays a named spine until one is: a screen with more boxes than slots
    // (a smaller display scale) settles instead of reading for ever.
    Slot* victim = nullptr;
    for (Slot& s : g_slots) {
        if (!s.id.empty() && s.frame + 1 >= FrameTimer) continue;
        if (!victim || s.used < victim->used) victim = &s;
    }
    if (!victim) return nullptr;
    if (!victim->id.empty()) g_index.erase(victim->id);
    victim->id.clear();
    g_readFrame = FrameTimer;
    FILE* f = std::fopen(BoxPath(game_id).c_str(), "rb");
    if (!f) {
        g_absent.insert(game_id);
        return nullptr;
    }
    if (!victim->data) victim->data = skin::Mem2Alloc(kBoxPixelBytes);
    std::uint8_t header[kBoxHeaderSize];
    const bool ok = victim->data && std::fread(header, 1, sizeof header, f) == sizeof header && box_header_valid(header) &&
                    std::fread(victim->data, 1, kBoxPixelBytes, f) == kBoxPixelBytes;
    std::fclose(f);
    if (!ok) {
        victim->id.clear();
        victim->used = 0;
        g_absent.insert(game_id);
        return nullptr;
    }
    DCFlushRange(victim->data, kBoxPixelBytes);
    GX_InvalidateTexAll();
    victim->id = game_id;
    victim->used = ++g_clock;
    victim->frame = FrameTimer;
    g_index[game_id] = victim;
    return victim->data;
}

void ForgetBox(const std::string& game_id) {
    g_absent.erase(game_id);
    const auto found = g_index.find(game_id);
    if (found == g_index.end()) return;
    found->second->id.clear();
    found->second->used = 0;
    g_index.erase(found);
}

}  // namespace riftwii::wii
