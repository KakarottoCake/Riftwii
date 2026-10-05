// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "boxart.hpp"

#include <ogc/cache.h>
#include <sys/stat.h>

#include <cstdio>
#include <ctime>
#include <set>
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
// A shelf shows about 25 boxes; room for twice that, so scrolling back
// finds them: 2.4 MB of MEM2, taken once when the shelf is first drawn.
constexpr int kSlots = 48;

std::string BoxPath(const std::string& id) { return std::string(kBoxDir) + "/" + id + ".rwb"; }
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
    g_background.got = FetchBoxNow(g_background.id, g_background.error);
    mem::WatchHeap(("after the box download of " + g_background.id).c_str());
}

struct Slot {
    std::string id;
    u8* data = nullptr;
    unsigned used = 0;
};
Slot g_slots[kSlots];
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
    Slot* victim = &g_slots[0];
    for (Slot& s : g_slots) {
        if (s.data && s.id == game_id) {
            s.used = ++g_clock;
            return s.data;
        }
        if (s.used < victim->used) victim = &s;
    }
    if (g_absent.count(game_id) || g_readFrame == FrameTimer) return nullptr;
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
    return victim->data;
}

void ForgetBox(const std::string& game_id) {
    g_absent.erase(game_id);
    for (Slot& s : g_slots)
        if (s.id == game_id) {
            s.id.clear();
            s.used = 0;
        }
}

}  // namespace riftwii::wii
