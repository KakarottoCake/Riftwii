// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "cover3d.hpp"

#include <ogc/cache.h>
#include <sys/stat.h>

#include <cerrno>
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

// The player's own 3D cover (riftwii::own_art_needs_cut): their PNG, and
// the picture cut from it beside it. In the pool under '!' and the name,
// which no ID or key starts with, apart from the downloaded one.
std::string OwnPngPath(const std::string& id) { return std::string(kDir) + "/" + id + ".png"; }
std::string OwnStorePath(const std::string& id) { return std::string(kDir) + "/" + id + ".own.rw3"; }
std::string OwnSourcePath(const std::string& id) { return std::string(kDir) + "/" + id + ".own.src"; }
std::string OwnKey(const std::string& id) { return "!" + id; }
// As a download's (kMaxPng): GameTDB's are some 60 KB. The 3D covers are
// read on the GUI thread, so the cap also bounds the one decode a picture's
// first showing costs.
constexpr std::size_t kOwnPngMax = kMaxPng;

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
    // The earlier match's picture must be gone first: left there, the note
    // below would make it look like the new entry's.
    struct stat st;
    if (std::remove(StorePath(key).c_str()) != 0 && stat(StorePath(key).c_str(), &st) == 0) {
        std::remove(MissPath(key).c_str());
        error = "cannot remove " + StorePath(key) + " (card write-protected?)";
        return CoverFetch::Failed;
    }
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

// Reads a stored 3D cover into the pool for `key`: the slot drawn longest
// ago makes room, never one drawn in this frame or the last (still on
// screen). `path_of` names the file once a slot is free (it may read the
// card: never while none is); an empty name, or a file missing or
// unreadable, notes `key` absent. One read a frame.
template <typename PathOf>
const u8* LoadIntoSlot(const std::string& key, PathOf path_of) {
    Slot* victim = nullptr;
    for (Slot& s : g_slots) {
        if (!s.id.empty() && s.frame + 1 >= FrameTimer) continue;
        if (!victim || s.used < victim->used) victim = &s;
    }
    if (!victim) return nullptr;
    if (!victim->id.empty()) g_index.erase(victim->id);
    victim->id.clear();
    g_readFrame = FrameTimer;
    const std::string path = path_of();
    FILE* f = path.empty() ? nullptr : std::fopen(path.c_str(), "rb");
    if (!f) {
        g_absent.insert(key);
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
        g_absent.insert(key);
        return nullptr;
    }
    DCFlushRange(victim->data, kCover3dPixelBytes);
    GX_InvalidateTexAll();
    victim->id = key;
    victim->used = ++g_clock;
    victim->frame = FrameTimer;
    g_index[key] = victim;
    return victim->data;
}

// The note of which PNG a cut copy came from (riftwii::own_art_stamp), or
// "" when there is none.
std::string ReadSourceNote(const std::string& path) {
    char line[48] = {};
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return std::string();
    const std::size_t got = std::fread(line, 1, sizeof line - 1, f);
    std::fclose(f);
    std::string note(line, got);
    while (!note.empty() && (note.back() == '\n' || note.back() == '\r')) note.pop_back();
    return note;
}

// What a try at the player's own picture came to. Retry: a failure that
// may pass (no memory, the card full or refusing a read or write): tried
// again some seconds later, a few times, rather than given up on until
// RiftWii starts again.
enum class OwnCut { Ready, None, Retry };
constexpr u32 kOwnRetryFrames = 600;  // about ten seconds
constexpr int kOwnTries = 3;
struct OwnRetry {
    u32 after = 0;
    int tries = 0;
};
std::unordered_map<std::string, OwnRetry> g_ownRetry;
// Names whose PNG was found, so a picture waiting for a slot or a retry
// doesn't look on the card again every frame.
std::set<std::string> g_ownFound;

// Cuts the player's PNG into a 3D cover when there is no cut one yet or it
// was cut from another PNG, as a downloaded one is. Ready when a cut one
// is there to read. Kept, so a picture is decoded once, not every visit; a PNG
// taken away takes its cut copy with it.
OwnCut CutOwnCover3d(const std::string& id) {
    const std::string png_path = OwnPngPath(id);
    const std::string cut_path = OwnStorePath(id);
    const std::string note_path = OwnSourcePath(id);
    struct stat png_info {};
    struct stat cut_info {};
    const bool png_found = stat(png_path.c_str(), &png_info) == 0;
    // A card that won't answer is not a PNG taken away: keep the cut copy and try again later.
    if (!png_found && errno != ENOENT && errno != ENOTDIR) return OwnCut::Retry;
    const bool png_there = png_found && S_ISREG(png_info.st_mode) && png_info.st_size > 0;
    // A cut copy counts only when it is whole: one cut short (the card pulled
    // out while it was written) is cut again, whatever its note says.
    const bool cut_there = stat(cut_path.c_str(), &cut_info) == 0 && S_ISREG(cut_info.st_mode) &&
                           static_cast<std::size_t>(cut_info.st_size) == kCover3dHeaderSize + kCover3dPixelBytes;
    if (!png_there) {
        if (cut_there) std::remove(cut_path.c_str());
        std::remove(note_path.c_str());
        return OwnCut::None;
    }
    const std::string png_stamp = own_art_stamp(static_cast<long long>(png_info.st_size),
                                                static_cast<long long>(png_info.st_mtime));
    if (!own_art_needs_cut(png_there, png_stamp, cut_there, cut_there ? ReadSourceNote(note_path) : std::string()))
        return OwnCut::Ready;
    if (static_cast<std::uint64_t>(png_info.st_size) > kOwnPngMax) {
        logf("3D covers: your 3D cover %s is %lld bytes, more than the %u MB one may be\n", png_path.c_str(),
             static_cast<long long>(png_info.st_size), static_cast<unsigned>(kOwnPngMax >> 20));
        return OwnCut::None;
    }
    OutOfMemoryScope out_of_memory;
    std::vector<std::uint8_t> png;
    FILE* f = std::fopen(png_path.c_str(), "rb");
    const bool read = f && try_resize(png, static_cast<std::size_t>(png_info.st_size)) &&
                      std::fread(png.data(), 1, png.size(), f) == png.size();
    if (f) std::fclose(f);
    std::vector<std::uint8_t> rgba;
    int w = 0, h = 0;
    std::string why;
    if (!read || !DecodePngRgba(png, rgba, w, h, why)) {
        if (out_of_memory.failed()) why = "out of memory";
        logf("3D covers: your 3D cover %s can't be read: %s\n", png_path.c_str(),
             read ? why.c_str() : "the card refused it");
        return !read || out_of_memory.failed() ? OwnCut::Retry : OwnCut::None;
    }
    std::vector<std::uint8_t>().swap(png);
    const std::vector<std::uint8_t> file = make_cover3d_file(rgba.data(), w, h);
    if (file.empty()) {
        if (out_of_memory.failed())
            logf("3D covers: no room to cut your 3D cover %s (%dx%d)\n", png_path.c_str(), w, h);
        else
            logf("3D covers: your 3D cover %s (%dx%d) can't be used: it should be a picture of the case as "
                 "GameTDB's 3D covers are\n", png_path.c_str(), w, h);
        return out_of_memory.failed() ? OwnCut::Retry : OwnCut::None;
    }
    const std::string note = png_stamp + "\n";
    if (!WriteAll(cut_path, file) || !WriteAll(note_path, std::vector<std::uint8_t>(note.begin(), note.end()))) {
        logf("3D covers: no room on the SD card for the 3D cover cut from %s\n", png_path.c_str());
        return OwnCut::Retry;
    }
    logf("3D covers: your own 3D cover for %s, cut from %s (%dx%d)\n", id.c_str(), png_path.c_str(), w, h);
    return OwnCut::Ready;
}

// The player's own 3D cover for `id` (a game's ID or a mod tile's key), cut
// from their PNG the first time it is wanted; nullptr when there is none.
const u8* OwnCover3dTexture(const std::string& id) {
    if (!ValidId(id)) return nullptr;
    const std::string key = OwnKey(id);
    const auto found = g_index.find(key);
    if (found != g_index.end()) {
        found->second->used = ++g_clock;
        found->second->frame = FrameTimer;
        return found->second->data;
    }
    if (g_absent.count(key)) return nullptr;
    // No PNG, as for most games: noted without using this frame's one read
    // from the card, so the downloaded picture behind it isn't held up.
    if (!g_ownFound.count(key)) {
        struct stat png_info {};
        if (stat(OwnPngPath(id).c_str(), &png_info) != 0 && (errno == ENOENT || errno == ENOTDIR)) {
            g_absent.insert(key);
            return nullptr;
        }
        g_ownFound.insert(key);
    }
    if (g_readFrame == FrameTimer) return nullptr;
    const auto waiting = g_ownRetry.find(key);
    if (waiting != g_ownRetry.end() && static_cast<s32>(FrameTimer - waiting->second.after) < 0) return nullptr;
    OwnCut cut = OwnCut::None;
    const u8* picture = LoadIntoSlot(key, [&] {
        cut = CutOwnCover3d(id);
        return cut == OwnCut::Ready ? OwnStorePath(id) : std::string();
    });
    if (picture) {
        g_ownRetry.erase(key);
    } else if (cut == OwnCut::Retry || cut == OwnCut::Ready) {
        // Ready, yet it wouldn't read (or no memory for its slot): without its
        // note it no longer counts as cut, so the retry cuts it again.
        if (cut == OwnCut::Ready) std::remove(OwnSourcePath(id).c_str());
        OwnRetry& retry = g_ownRetry[key];
        if (++retry.tries < kOwnTries) {
            g_absent.erase(key);
            retry.after = FrameTimer + kOwnRetryFrames;
        }
    }
    return picture;
}

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
    return LoadIntoSlot(game_id, [&] {
        // A tile's picture only when it came from the entry the tile matches now.
        if (tile) g_tileReadFor[game_id] = match;
        const bool current = !tile || tile_art_current(ReadTileArtFrom(FromPath(game_id)), match);
        return current ? StorePath(game_id) : std::string();
    });
}

const u8* TileCover3dTexture(const std::string& cover_name, const std::string& game_id, const u8* front) {
    // As TileBoxTexture: the player's own 3D cover for this tile or game
    // first, whatever the tile is called (riftwii::own_art_needs_cut), then
    // riftwii::tile_art_order's: a mod tile's own 3D cover after only the
    // player's own PNG for that tile.
    if (const u8* players = OwnCover3dTexture(cover_name)) return players;
    const bool tile = is_tile_art_name(cover_name);
    if (tile_art_order(tile, front && tile && CoverIsTileOwn(cover_name), false).tile_art_first) {
        if (const u8* own_case = Cover3dTexture(cover_name)) return own_case;
    }
    if (tile_art_order(tile, false, front && CoverBeforeBox(cover_name)).flat_before_game_art) return nullptr;
    if (game_id.empty()) return nullptr;
    // A mod tile with no 3D cover of its own: its game's, the player's own first.
    if (tile) {
        if (const u8* players_game = OwnCover3dTexture(game_id)) return players_game;
    }
    return Cover3dTexture(game_id);
}

void ForgetCover3d(const std::string& game_id) {
    g_absent.erase(game_id);
    g_absent.erase(OwnKey(game_id));
    g_ownRetry.erase(OwnKey(game_id));
    g_ownFound.erase(OwnKey(game_id));
    const auto own = g_index.find(OwnKey(game_id));
    if (own != g_index.end()) {
        own->second->id.clear();
        own->second->used = 0;
        g_index.erase(own);
    }
    g_tileReadFor.erase(game_id);
    const auto found = g_index.find(game_id);
    if (found == g_index.end()) return;
    found->second->id.clear();
    found->second->used = 0;
    g_index.erase(found);
}

void ForgetAllCovers3d() {
    g_absent.clear();
    g_ownRetry.clear();
    g_ownFound.clear();
    g_tileReadFor.clear();
    g_index.clear();
    for (Slot& s : g_slots) {
        s.id.clear();
        s.used = 0;
    }
}

}  // namespace riftwii::wii
