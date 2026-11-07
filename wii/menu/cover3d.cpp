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
#include "riftwii/checkedalloc.hpp"
#include "riftwii/coverart.hpp"
#include "riftwii/modcover.hpp"
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

// Beside a mod tile's picture or miss: the GameTDB entry it came from.
std::string FromPath(const std::string& id) { return std::string(kDir) + "/" + id + ".mod"; }

// A game's six characters, a homebrew app's four (GameTDB keeps full and
// 3D covers for those too, riftwii/homebrewmatch.hpp), or a mod tile's
// key (riftwii/modcover.hpp).
bool ValidId(const std::string& id) { return valid_art_name(id); }

// The GameTDB entry a mod tile matches now; "" for a game, or a tile GameTDB doesn't list.
std::string TileMatch(const std::string& id) { return is_tile_art_name(id) ? ModTileCoverId(id) : std::string(); }

bool WriteAll(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    const std::string temp = path + ".part";
    FILE* f = std::fopen(temp.c_str(), "wb");
    if (!f) return false;
    bool ok = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    ok = std::fclose(f) == 0 && ok;
    // The old file goes only once the new one is whole: a full card used
    // to cost the picture that was already there (BUG-13). FAT's rename
    // does not replace a file, so the old one is removed first.
    if (!ok) {
        std::remove(temp.c_str());
        return false;
    }
    std::remove(path.c_str());
    if (std::rename(temp.c_str(), path.c_str()) != 0) {
        std::remove(temp.c_str());
        return false;
    }
    return true;
}

// A mod tile's "GameTDB has none", as its flat cover's (#394): a picture
// stored for an earlier match goes, and the miss counts only with the
// entry's name beside it. A tile whose miss can't be written would be
// asked for again at once: a failure, counted as one. NotFound when noted.
CoverFetch NoteTileMiss(const std::string& key, const std::string& art_of, bool missed, std::string& error) {
    if (!missed) {
        error = "cannot write " + MissPath(key) + " (card full?)";
        return CoverFetch::Failed;
    }
    std::remove(StorePath(key).c_str());
    if (!WriteTileArtFrom(FromPath(key), art_of)) {
        std::remove(MissPath(key).c_str());
        error = "cannot write " + FromPath(key) + " (card full?)";
        return CoverFetch::Failed;
    }
    return CoverFetch::NotFound;
}

CoverFetch FetchNow(const std::string& game_id, std::string& error) {
    // A mod tile: the picture of the mod's own GameTDB entry, kept under
    // the tile's key; none when GameTDB doesn't list the mod.
    const bool tile = is_tile_art_name(game_id);
    const std::string art_of = art_download_id(game_id, TileMatch(game_id));
    if (art_of.empty()) return CoverFetch::NotFound;
    mkdir("sd:/riftwii", 0777);
    mkdir(kDir, 0777);
    for (const std::string& art_id : gametdb_art_ids(art_of)) {
        for (const std::string& region : cover_regions(art_of, MenuLanguage())) {
            std::vector<std::uint8_t> png;
            std::string why;
            if (!HttpGet(cover3d_url(region, art_id), png, why, kMaxPng, 15000)) {
                // A refusal means none in this region (the covers' rule,
                // riftwii/coverart.hpp); anything else is no answer at all.
                if (cover_answer_means_missing(http_answered_status(why))) continue;
                error = why;
                return CoverFetch::NoAnswer;
            }
            std::vector<std::uint8_t> rgba;
            int w = 0, h = 0;
            if (!DecodePngRgba(png, rgba, w, h, why)) {
                // No room is not "no cover": RunBackgroundFetch says so, and
                // no miss is noted.
                if (took_out_of_memory()) return CoverFetch::NotFound;
                logf("3D covers: GameTDB's %s 3D cover of %s: %s\n", region.c_str(), game_id.c_str(), why.c_str());
                continue;
            }
            const std::vector<std::uint8_t> file = make_cover3d_file(rgba.data(), w, h);
            if (file.empty()) {
                if (took_out_of_memory()) return CoverFetch::Failed;
                logf("3D covers: GameTDB's %s 3D cover of %s is an unusable %dx%d\n", region.c_str(), game_id.c_str(), w, h);
                continue;
            }
            if (!WriteAll(StorePath(game_id), file)) {
                error = "cannot write " + StorePath(game_id) + " (card full?)";
                return CoverFetch::Failed;
            }
            std::remove(MissPath(game_id).c_str());
            // Without its entry's name beside it, the tile's picture would
            // be wanted again on every pass, and never drawn.
            if (tile && !WriteTileArtFrom(FromPath(game_id), art_of)) {
                std::remove(StorePath(game_id).c_str());
                error = "cannot write " + FromPath(game_id) + " (card full?)";
                return CoverFetch::Failed;
            }
            if (tile)
                logf("3D covers: %s stored, GameTDB's %s (%s, %dx%d)\n", game_id.c_str(), art_id.c_str(),
                     region.c_str(), w, h);
            else
                logf("3D covers: %s stored (%s, %dx%d)\n", game_id.c_str(), region.c_str(), w, h);
            return CoverFetch::Stored;
        }
    }
    const bool missed = WriteAll(MissPath(game_id), {});
    if (tile) {
        const CoverFetch noted = NoteTileMiss(game_id, art_of, missed, error);
        if (noted != CoverFetch::NotFound) return noted;
    }
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
    OutOfMemoryScope out_of_memory;
    g_background.got = FetchNow(g_background.id, g_background.error);
    if (g_background.got != CoverFetch::Stored && out_of_memory.failed()) {
        // NoAnswer, not Failed: a memory shortage must not turn 3D covers off.
        g_background.got = CoverFetch::NoAnswer;
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
// Mod tiles read (or found absent), by the GameTDB entry they matched then:
// once matched to another (renamed, GameTDB's list changed), read again.
std::unordered_map<std::string, std::string> g_tileReadFor;

}  // namespace

void ForgetCover3dMiss(const std::string& game_id) {
    if (ValidId(game_id)) std::remove(MissPath(game_id).c_str());
}

bool Cover3dWanted(const std::string& game_id) {
    if (!ValidId(game_id)) return false;
    const std::string match = TileMatch(game_id);
    // A mod tile GameTDB doesn't list: nothing to ask for (it shows its game's).
    if (is_tile_art_name(game_id) && match.empty()) return false;
    struct stat st;
    StoredArt stored;
    stored.picture = stat(StorePath(game_id).c_str(), &st) == 0;
    stored.recent_miss = stat(MissPath(game_id).c_str(), &st) == 0 && std::time(nullptr) - st.st_mtime < kRetryAfter;
    if (!match.empty()) stored.from = ReadTileArtFrom(FromPath(game_id));
    return art_wanted(game_id, match, stored);
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
    const bool tile = is_tile_art_name(game_id);
    const std::string match = tile ? ModTileCoverId(game_id) : std::string();
    if (tile) {
        const auto read_for = g_tileReadFor.find(game_id);
        if (read_for != g_tileReadFor.end() && read_for->second != match) ForgetCover3d(game_id);
        // A tile GameTDB doesn't list has none of its own: its game's shows.
        if (match.empty()) return nullptr;
    }
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
    // A tile's picture only when it came from the entry the tile matches now.
    if (tile) g_tileReadFor[game_id] = match;
    const bool current = !tile || tile_art_current(ReadTileArtFrom(FromPath(game_id)), match);
    FILE* f = current ? std::fopen(StorePath(game_id).c_str(), "rb") : nullptr;
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

const u8* TileCover3dTexture(const std::string& cover_name, const std::string& game_id, const u8* front) {
    // As TileBoxTexture: a mod tile's own 3D cover after only the player's
    // own PNG for that tile.
    if (is_tile_art_name(cover_name) && !(front && CoverIsTileOwn(cover_name))) {
        if (const u8* own_case = Cover3dTexture(cover_name)) return own_case;
    }
    if (front && CoverBeforeBox(cover_name)) return nullptr;
    return game_id.empty() ? nullptr : Cover3dTexture(game_id);
}

void ForgetCover3d(const std::string& game_id) {
    g_absent.erase(game_id);
    g_tileReadFor.erase(game_id);
    const auto found = g_index.find(game_id);
    if (found == g_index.end()) return;
    found->second->id.clear();
    found->second->used = 0;
    g_index.erase(found);
}

}  // namespace riftwii::wii
