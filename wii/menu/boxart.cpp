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
#include "riftwii/checkedalloc.hpp"
#include "riftwii/coverart.hpp"
#include "riftwii/modcover.hpp"
#include "skin.hpp"
#include "video.h"

namespace riftwii::wii {
namespace {

constexpr const char* kBoxDir = "sd:/riftwii/boxes";
constexpr std::time_t kRetryAfter = 7 * 24 * 60 * 60;
// A 4:3 shelf draws 27 boxes, a 16:9 one 43 (wii/menu/gui_shelf.cpp): room for
// all of those, about 4 MB of MEM2, taken slot by slot as the shelf first
// needs them. With 32 a 16:9 shelf never fit: every frame read a box from
// the card and pushed out one still on screen, so the spines kept turning
// back into names and scrolling crawled (a tester's video, 2610-178).
constexpr int kSlots = 44;

// .rw2: the box with its back (version 1's .rwb files had no back and are left alone).
std::string BoxPath(const std::string& id) { return std::string(kBoxDir) + "/" + id + ".rw2"; }
std::string MissPath(const std::string& id) { return std::string(kBoxDir) + "/" + id + ".none"; }

// Beside a mod tile's picture or miss: the GameTDB entry it came from.
std::string FromPath(const std::string& id) { return std::string(kBoxDir) + "/" + id + ".mod"; }

// The player's own box (riftwii::own_art_needs_cut): their PNG, and the box
// cut from it beside it. In the pool under '!' and the name, which no ID or
// key starts with, so it never meets the downloaded box of the same name.
std::string OwnPngPath(const std::string& id) { return std::string(kBoxDir) + "/" + id + ".png"; }
std::string OwnBoxPath(const std::string& id) { return std::string(kBoxDir) + "/" + id + ".own.rw2"; }
std::string OwnSourcePath(const std::string& id) { return std::string(kBoxDir) + "/" + id + ".own.src"; }
std::string OwnKey(const std::string& id) { return "!" + id; }
// GameTDB's large full cover scans (1024 x 680) are about 1.1 MB.
constexpr std::size_t kOwnPngMax = 4u << 20;

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
    if (std::remove(BoxPath(key).c_str()) != 0 && stat(BoxPath(key).c_str(), &st) == 0) {
        std::remove(MissPath(key).c_str());
        error = "cannot remove " + BoxPath(key) + " (card write-protected?)";
        return CoverFetch::Failed;
    }
    if (!WriteTileArtFrom(FromPath(key), art_of)) {
        std::remove(MissPath(key).c_str());
        error = "cannot write " + FromPath(key) + " (card full?)";
        return CoverFetch::Failed;
    }
    return CoverFetch::NotFound;
}

// `tile_match`: for a mod tile, the GameTDB entry it matched when the
// download started (StartBoxFetch), so what is fetched is what was recorded.
CoverFetch FetchBoxNow(const std::string& game_id, const std::string& tile_match, std::string& error) {
    // A mod tile: the picture of the mod's own GameTDB entry, kept under
    // the tile's key; none when GameTDB doesn't list the mod.
    const bool tile = is_tile_art_name(game_id);
    const std::string art_of = art_download_id(game_id, tile_match);
    if (art_of.empty()) return CoverFetch::NotFound;
    mkdir("sd:/riftwii", 0777);
    mkdir(kBoxDir, 0777);
    for (const std::string& art_id : gametdb_art_ids(art_of)) {
        for (const std::string& region : cover_regions(art_of, MenuLanguage())) {
            std::vector<std::uint8_t> png;
            std::string why;
            // A full cover is around a megabyte.
            if (!HttpGet(coverfull_url(region, art_id), png, why, 3u << 20, 15000)) {
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
                logf("Shelf: GameTDB's %s full cover of %s: %s\n", region.c_str(), game_id.c_str(), why.c_str());
                continue;
            }
            std::vector<std::uint8_t>().swap(png);
            const std::vector<std::uint8_t> file = make_box_file(rgba.data(), w, h);
            if (file.empty()) {
                if (took_out_of_memory()) return CoverFetch::Failed;
                logf("Shelf: GameTDB's %s full cover of %s (%dx%d) has no spine where a Wii case has it\n", region.c_str(),
                     game_id.c_str(), w, h);
                continue;
            }
            if (!WriteAll(BoxPath(game_id), file)) {
                error = "cannot write " + BoxPath(game_id) + " (card full?)";
                return CoverFetch::Failed;
            }
            std::remove(MissPath(game_id).c_str());
            // Without its entry's name beside it, the tile's picture would
            // be wanted again on every pass, and never drawn.
            if (tile && !WriteTileArtFrom(FromPath(game_id), art_of)) {
                std::remove(BoxPath(game_id).c_str());
                error = "cannot write " + FromPath(game_id) + " (card full?)";
                return CoverFetch::Failed;
            }
            if (tile)
                logf("Shelf: box of %s, GameTDB's %s (%s, %dx%d)\n", game_id.c_str(), art_id.c_str(), region.c_str(),
                     w, h);
            else
                logf("Shelf: box of %s (%s, %dx%d)\n", game_id.c_str(), region.c_str(), w, h);
            return CoverFetch::Stored;
        }
    }
    const bool missed = WriteAll(MissPath(game_id), {});
    if (tile) {
        const CoverFetch noted = NoteTileMiss(game_id, art_of, missed, error);
        if (noted != CoverFetch::NotFound) return noted;
    }
    logf("Shelf: GameTDB has no full cover of %s\n", game_id.c_str());
    return CoverFetch::NotFound;
}

struct Background {
    std::string id;
    std::string entry;  // a mod tile's GameTDB entry, read when it started
    CoverFetch got = CoverFetch::Failed;
    std::string error;
};
Background g_background;

void RunBackgroundFetch() {
    // A full scan unpacks to a few MB: without the room, give up on this box
    // rather than end the thread.
    OutOfMemoryScope out_of_memory;
    g_background.got = FetchBoxNow(g_background.id, g_background.entry, g_background.error);
    if (g_background.got != CoverFetch::Stored && out_of_memory.failed()) {
        // NoAnswer, not Failed: a memory shortage must not turn the boxes off.
        g_background.got = CoverFetch::NoAnswer;
        g_background.error = "out of memory";
        logf("Shelf: no room to cut the box of %s\n", g_background.id.c_str());
        mem::LogUsage("after the full cover ran out of memory");
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
// Mod tiles read (or found absent), by the GameTDB entry they matched then:
// once matched to another (renamed, GameTDB's list changed), read again.
std::unordered_map<std::string, std::string> g_tileReadFor;

// Reads a stored box into the pool for `key`: the slot drawn longest ago
// makes room, but never one drawn in this frame or the last (still on
// screen). With none free the box stays a named spine until one is: a
// screen with more boxes than slots (a smaller display scale) settles
// instead of reading for ever. `path_of` names the file once a slot is
// free (it may read the card: never while none is); an empty name, or a
// file missing or unreadable, notes `key` absent. One read a frame.
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
    if (!victim->data) victim->data = skin::Mem2Alloc(kBoxPixelBytes);
    std::uint8_t header[kBoxHeaderSize];
    const bool ok = victim->data && std::fread(header, 1, sizeof header, f) == sizeof header && box_header_valid(header) &&
                    std::fread(victim->data, 1, kBoxPixelBytes, f) == kBoxPixelBytes;
    std::fclose(f);
    if (!ok) {
        victim->id.clear();
        victim->used = 0;
        g_absent.insert(key);
        return nullptr;
    }
    DCFlushRange(victim->data, kBoxPixelBytes);
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

// Cuts the player's PNG into its box when there is no cut one yet or it was
// cut from another PNG, as a downloaded scan is cut. Ready when a cut box
// is there to read. The cut is kept, so this costs one decode per picture, not
// one a visit; a PNG taken away takes its box with it.
OwnCut CutOwnBox(const std::string& id) {
    const std::string png_path = OwnPngPath(id);
    const std::string cut_path = OwnBoxPath(id);
    const std::string note_path = OwnSourcePath(id);
    struct stat png_info {};
    struct stat cut_info {};
    const bool png_there = stat(png_path.c_str(), &png_info) == 0 && S_ISREG(png_info.st_mode) && png_info.st_size > 0;
    // A cut copy counts only when it is whole: one cut short (the card pulled
    // out while it was written) is cut again, whatever its note says.
    const bool cut_there = stat(cut_path.c_str(), &cut_info) == 0 && S_ISREG(cut_info.st_mode) &&
                           static_cast<std::size_t>(cut_info.st_size) == kBoxFileSize;
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
        logf("Shelf: your box %s is %lld bytes, more than the %u MB a box picture may be\n", png_path.c_str(),
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
        logf("Shelf: your box %s can't be read: %s\n", png_path.c_str(), read ? why.c_str() : "the card refused it");
        return !read || out_of_memory.failed() ? OwnCut::Retry : OwnCut::None;
    }
    std::vector<std::uint8_t>().swap(png);
    const std::vector<std::uint8_t> box = make_box_file(rgba.data(), w, h);
    if (box.empty()) {
        if (out_of_memory.failed())
            logf("Shelf: no room to cut your box %s (%dx%d)\n", png_path.c_str(), w, h);
        else
            logf("Shelf: your box %s (%dx%d) has no spine where a Wii case has it: it should be a whole case's scan, "
                 "back, spine and front, as GameTDB's full covers are\n", png_path.c_str(), w, h);
        return out_of_memory.failed() ? OwnCut::Retry : OwnCut::None;
    }
    const std::string note = png_stamp + "\n";
    if (!WriteAll(cut_path, box) || !WriteAll(note_path, std::vector<std::uint8_t>(note.begin(), note.end()))) {
        logf("Shelf: no room on the SD card for the box cut from %s\n", png_path.c_str());
        return OwnCut::Retry;
    }
    logf("Shelf: your own box for %s, cut from %s (%dx%d)\n", id.c_str(), png_path.c_str(), w, h);
    return OwnCut::Ready;
}

// The player's own box for `id` (a game's ID or a mod tile's key), cut
// from their PNG the first time it is wanted; nullptr when there is none.
const u8* OwnBoxTexture(const std::string& id) {
    if (!ValidId(id)) return nullptr;
    const std::string key = OwnKey(id);
    const auto found = g_index.find(key);
    if (found != g_index.end()) {
        found->second->used = ++g_clock;
        found->second->frame = FrameTimer;
        return found->second->data;
    }
    if (g_readFrame == FrameTimer || g_absent.count(key)) return nullptr;
    const auto waiting = g_ownRetry.find(key);
    if (waiting != g_ownRetry.end() && static_cast<s32>(FrameTimer - waiting->second.after) < 0) return nullptr;
    OwnCut cut = OwnCut::None;
    const u8* picture = LoadIntoSlot(key, [&] {
        cut = CutOwnBox(id);
        return cut == OwnCut::Ready ? OwnBoxPath(id) : std::string();
    });
    if (picture) {
        g_ownRetry.erase(key);
    } else if (cut == OwnCut::Retry) {
        OwnRetry& retry = g_ownRetry[key];
        if (++retry.tries < kOwnTries) {
            g_absent.erase(key);
            retry.after = FrameTimer + kOwnRetryFrames;
        }
    } else if (cut == OwnCut::Ready && g_absent.count(key)) {
        // Cut, yet it won't read: without its note it is cut again next time.
        std::remove(OwnSourcePath(id).c_str());
    }
    return picture;
}

}  // namespace

void ForgetBoxMiss(const std::string& game_id) {
    if (ValidId(game_id)) std::remove(MissPath(game_id).c_str());
}

bool BoxWanted(const std::string& game_id) {
    if (!ValidId(game_id)) return false;
    const std::string match = TileMatch(game_id);
    // A mod tile GameTDB doesn't list: nothing to ask for (it shows its game's).
    if (is_tile_art_name(game_id) && match.empty()) return false;
    struct stat st;
    StoredArt stored;
    stored.picture = stat(BoxPath(game_id).c_str(), &st) == 0;
    stored.recent_miss = stat(MissPath(game_id).c_str(), &st) == 0 && std::time(nullptr) - st.st_mtime < kRetryAfter;
    if (!match.empty()) stored.from = ReadTileArtFrom(FromPath(game_id));
    return art_wanted(game_id, match, stored);
}

bool StartBoxFetch(const std::string& game_id) {
    if (!g_background.id.empty() || NetBackgroundBusy()) return false;
    g_background = Background{};
    g_background.id = game_id;
    g_background.entry = TileMatch(game_id);
    if (NetRunInBackground(RunBackgroundFetch)) return true;
    g_background.id.clear();
    return false;
}

const std::string& BoxFetchGame() { return g_background.id; }

const std::string& BoxFetchEntry() { return g_background.entry; }

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
    const bool tile = is_tile_art_name(game_id);
    const std::string match = tile ? ModTileCoverId(game_id) : std::string();
    if (tile) {
        const auto read_for = g_tileReadFor.find(game_id);
        if (read_for != g_tileReadFor.end() && read_for->second != match) ForgetBox(game_id);
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
        return current ? BoxPath(game_id) : std::string();
    });
}

const u8* TileBoxTexture(const std::string& cover_name, const std::string& game_id, const u8* front, bool* tile_box) {
    if (tile_box) *tile_box = false;
    const bool tile = is_tile_art_name(cover_name);
    // The player's own box for this tile or game comes before everything
    // else (riftwii::own_art_needs_cut): a tile's needs no GameTDB entry, so
    // a tile can be called anything and still have its box.
    if (const u8* players = OwnBoxTexture(cover_name)) {
        if (tile_box) *tile_box = tile;
        return players;
    }
    // Then riftwii::tile_art_order's: a mod tile's own box (#385) after only
    // the player's own PNG for that tile.
    if (tile_art_order(tile, front && tile && CoverIsTileOwn(cover_name), false).tile_art_first) {
        if (const u8* own_box = BoxTexture(cover_name)) {
            if (tile_box) *tile_box = true;
            return own_box;
        }
    }
    if (tile_art_order(tile, false, front && CoverBeforeBox(cover_name)).flat_before_game_art) return nullptr;
    if (game_id.empty()) return nullptr;
    // A mod tile with no box of its own: its game's, the player's own first.
    if (tile) {
        if (const u8* players_game = OwnBoxTexture(game_id)) return players_game;
    }
    return BoxTexture(game_id);
}

void ForgetBox(const std::string& game_id) {
    g_absent.erase(game_id);
    g_absent.erase(OwnKey(game_id));
    g_ownRetry.erase(OwnKey(game_id));
    g_tileReadFor.erase(game_id);
    const auto found = g_index.find(game_id);
    if (found == g_index.end()) return;
    found->second->id.clear();
    found->second->used = 0;
    g_index.erase(found);
}

void ForgetAllBoxes() {
    g_absent.clear();
    g_ownRetry.clear();
    g_tileReadFor.clear();
    g_index.clear();
    for (Slot& s : g_slots) {
        s.id.clear();
        s.used = 0;
    }
}

}  // namespace riftwii::wii
