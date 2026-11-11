// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "boxart.hpp"

#include <ogc/cache.h>
#include <ogc/mutex.h>  // before cond.h, which needs mutex_t
#include <ogc/cond.h>
#include <ogc/lwp.h>
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
    const bool png_found = stat(png_path.c_str(), &png_info) == 0;
    // A card that won't answer is not a PNG taken away: keep the cut copy and try again later.
    if (!png_found && errno != ENOENT && errno != ENOTDIR) return OwnCut::Retry;
    const bool png_there = png_found && S_ISREG(png_info.st_mode) && png_info.st_size > 0;
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

struct Slot {
    std::string id;
    u8* data = nullptr;
    bool ready = false;  // read and flushed: the GUI may draw it (else being read)
    unsigned used = 0;
    u32 frame = 0;  // the frame it was last drawn in
};
Slot g_slots[kSlots];
std::unordered_map<std::string, Slot*> g_index;  // the slots by game, ready or being read
unsigned g_clock = 0;
std::set<std::string> g_absent;  // not on the card (or unreadable)
// Mod tiles read (or found absent), by the GameTDB entry they matched then:
// once matched to another (renamed, GameTDB's list changed), read again.
std::unordered_map<std::string, std::string> g_tileReadFor;

// The card is read by a thread of its own, one box at a time, so the GUI
// thread never waits on it. A box is a FAT lookup and a 96 KB read: a few
// frames on a Wii's SD card (a cover, 18 KB, took 26 ms on a tester's),
// and longer when the covers' loader is reading the card or a USB drive at
// the same time. Read on the GUI thread, every box that came into view
// stopped the shelf for those frames, so it never scrolled smoothly.
//
// The GUI thread fills in a job (the slot chosen, the file) only while no
// job is out, and takes the answer back (TakeRead) before the next one.
// The reader touches nothing but the job and the slot's pixels.
struct ReadJob {
    Slot* slot = nullptr;   // out while set
    std::string id;
    std::string path;       // BoxPath(id)
    std::string tile_from;  // a mod tile's FromPath, "" for a game's
    std::string tile_match; // the GameTDB entry the tile matches now
    std::string own;        // the player's own box: its name, cut (once) by the reader before the read
    OwnCut cut = OwnCut::None;  // the reader's: what that cut came to
    unsigned forgets = 0;   // g_forgets when it went out
    bool done = false;      // the reader's: answered (under g_readLock)
    bool ok = false;
};
ReadJob g_job;
unsigned g_forgets = 0;  // ForgetBox and ForgetAllBoxes, so a read in flight is dropped
mutex_t g_readLock = LWP_MUTEX_NULL;
cond_t g_readWake = LWP_COND_NULL;
cond_t g_readIdle = LWP_COND_NULL;  // signalled when a read ends
bool g_readHold = false;    // BoxReaderHold: no new read until BoxReaderRelease
bool g_reading = false;     // the card is being read, without the lock
lwp_t g_reader = LWP_THREAD_NULL;
alignas(32) u8 g_readerStack[16384];

void* ReaderMain(void*) {
    LWP_MutexLock(g_readLock);
    for (;;) {
        if (g_readHold || !g_job.slot || g_job.done) {
            LWP_CondWait(g_readWake, g_readLock);
            continue;
        }
        // The job is the reader's until it says done: copied out so the
        // card is read without the lock.
        u8* pixels = g_job.slot->data;
        const std::string path = g_job.path;
        const std::string tile_from = g_job.tile_from;
        const std::string tile_match = g_job.tile_match;
        const std::string own = g_job.own;
        g_reading = true;
        LWP_MutexUnlock(g_readLock);

        // The player's own box: its PNG cut first, once a picture (the covers'
        // loader decodes the player's own PNGs on a stack this size too).
        OwnCut cut = OwnCut::None;
        std::string read_path = path;
        if (!own.empty()) {
            cut = CutOwnBox(own);
            read_path = cut == OwnCut::Ready ? OwnBoxPath(own) : std::string();
        }

        // A tile's picture only when it came from the entry the tile matches now.
        const bool current = tile_from.empty() || tile_art_current(ReadTileArtFrom(tile_from), tile_match);
        FILE* f = current && !read_path.empty() ? std::fopen(read_path.c_str(), "rb") : nullptr;
        bool ok = false;
        if (f) {
            std::uint8_t header[kBoxHeaderSize];
            ok = std::fread(header, 1, sizeof header, f) == sizeof header && box_header_valid(header) &&
                 std::fread(pixels, 1, kBoxPixelBytes, f) == kBoxPixelBytes;
            std::fclose(f);
        }
        if (ok) DCFlushRange(pixels, kBoxPixelBytes);

        LWP_MutexLock(g_readLock);
        g_job.ok = ok;
        g_job.cut = cut;
        g_job.done = true;
        g_reading = false;
        LWP_CondSignal(g_readIdle);
    }
    return nullptr;
}

// The reader, the first time a box is asked for. GUI thread.
bool ReaderStarted() {
    if (g_reader != LWP_THREAD_NULL) return true;
    if (g_readLock == LWP_MUTEX_NULL) {
        LWP_MutexInit(&g_readLock, false);
        LWP_CondInit(&g_readWake);
        LWP_CondInit(&g_readIdle);
    }
    // Below the GUI thread (70), as the covers' loader: it reads while the
    // GUI waits for the next frame.
    if (LWP_CreateThread(&g_reader, ReaderMain, nullptr, g_readerStack, sizeof(g_readerStack), 40) < 0) {
        g_reader = LWP_THREAD_NULL;
        return false;
    }
    return true;
}

// Whether a pool key is the player's own box's (OwnKey), not a game's or a tile's.
bool IsOwnKey(const std::string& key) { return !key.empty() && key[0] == '!'; }

// Keeps a passing failure of the player's own box from counting as "none":
// tried again about ten seconds later, up to kOwnTries times. A cut copy that
// was ready but wouldn't read loses its note, so the next try cuts it again.
void OwnRetryLater(const std::string& key) {
    OwnRetry& retry = g_ownRetry[key];
    if (++retry.tries < kOwnTries) {
        g_absent.erase(key);
        retry.after = FrameTimer + kOwnRetryFrames;
    } else {
        g_absent.insert(key);
    }
}

// A cut copy that was ready but wouldn't read loses its note (cut again on the retry); a cut that may pass
// (no memory, the card full) is retried; no usable PNG stays "none".
void OwnReadFailed(const std::string& key, OwnCut cut) {
    if (cut == OwnCut::Ready) std::remove(OwnSourcePath(key.substr(1)).c_str());
    if (cut != OwnCut::None) OwnRetryLater(key);
}

// The answer to the job out, once the reader has one: the slot is ready,
// or given up and its game noted as not on the card. GUI thread.
void TakeRead() {
    if (!g_job.slot) return;
    bool done, ok;
    LWP_MutexLock(g_readLock);
    done = g_job.done;
    ok = g_job.ok;
    LWP_MutexUnlock(g_readLock);
    if (!done) return;
    Slot* slot = g_job.slot;
    // Forgotten while it was read (a new download, another SD card): the
    // slot was emptied already, and the box is read again when asked for.
    const bool forgotten = g_job.forgets != g_forgets || slot->id != g_job.id;
    if (ok && !forgotten) {
        // The texture cache may still hold the slot's last box.
        GX_InvalidateTexAll();
        slot->ready = true;
        // The player's own box read: its earlier failures, if any, are behind it.
        if (IsOwnKey(g_job.id)) g_ownRetry.erase(g_job.id);
    } else if (!forgotten) {
        g_index.erase(slot->id);
        slot->id.clear();
        slot->used = 0;
        g_absent.insert(g_job.id);
        if (IsOwnKey(g_job.id)) OwnReadFailed(g_job.id, g_job.cut);
    }
    LWP_MutexLock(g_readLock);
    g_job = ReadJob{};
    LWP_MutexUnlock(g_readLock);
}


// The player's own box for `id` (a game's ID or a mod tile's key); nullptr
// when there is none, or while it is being cut or read. Both happen on the
// reader's thread, so the shelf never waits for a PNG to be decoded.
const u8* OwnBoxTexture(const std::string& id) {
    if (!ValidId(id)) return nullptr;
    const std::string key = OwnKey(id);
    TakeRead();
    const auto found = g_index.find(key);
    if (found != g_index.end()) {
        Slot* slot = found->second;
        if (!slot->ready) return nullptr;  // being read
        slot->used = ++g_clock;
        slot->frame = FrameTimer;
        return slot->data;
    }
    if (g_absent.count(key)) return nullptr;
    // No PNG, as for most games: noted without a read from the card beyond
    // this one look, so the downloaded box behind it isn't held up. A PNG
    // found is looked for only once.
    if (!g_ownFound.count(key)) {
        struct stat png_info {};
        if (stat(OwnPngPath(id).c_str(), &png_info) != 0 && (errno == ENOENT || errno == ENOTDIR)) {
            g_absent.insert(key);
            return nullptr;
        }
        g_ownFound.insert(key);
    }
    const auto waiting = g_ownRetry.find(key);
    if (waiting != g_ownRetry.end() && static_cast<s32>(FrameTimer - waiting->second.after) < 0) return nullptr;
    // One read out at a time, as for the downloaded boxes.
    if (g_job.slot || !ReaderStarted()) return nullptr;
    Slot* victim = nullptr;
    for (Slot& s : g_slots) {
        if (!s.id.empty() && s.frame + 1 >= FrameTimer) continue;
        if (!victim || s.used < victim->used) victim = &s;
    }
    if (!victim) return nullptr;
    if (!victim->data) victim->data = skin::Mem2Alloc(kBoxPixelBytes);
    if (!victim->data) return nullptr;
    if (!victim->id.empty()) g_index.erase(victim->id);
    victim->id = key;
    victim->ready = false;
    victim->used = ++g_clock;
    victim->frame = FrameTimer;
    g_index[key] = victim;

    LWP_MutexLock(g_readLock);
    g_job = ReadJob{};
    g_job.slot = victim;
    g_job.id = key;
    g_job.own = id;
    g_job.forgets = g_forgets;
    LWP_CondSignal(g_readWake);
    LWP_MutexUnlock(g_readLock);
    return nullptr;
}

}  // namespace

void BoxReaderHold() {
    if (g_readLock == LWP_MUTEX_NULL || g_reader == LWP_THREAD_NULL) return;
    if (LWP_GetSelf() == g_reader) return;
    LWP_MutexLock(g_readLock);
    g_readHold = true;
    while (g_reading) LWP_CondWait(g_readIdle, g_readLock);
    LWP_MutexUnlock(g_readLock);
}

void BoxReaderRelease() {
    if (g_readLock == LWP_MUTEX_NULL || g_reader == LWP_THREAD_NULL) return;
    LWP_MutexLock(g_readLock);
    g_readHold = false;
    LWP_CondSignal(g_readWake);
    LWP_MutexUnlock(g_readLock);
}

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
    TakeRead();
    const auto found = g_index.find(game_id);
    if (found != g_index.end()) {
        Slot* slot = found->second;
        if (!slot->ready) return nullptr;  // being read
        slot->used = ++g_clock;
        slot->frame = FrameTimer;
        return slot->data;
    }
    // One read out at a time: the first box asked for while the reader is
    // free gets it, so the shelf asks for the boxes nearest the focus first.
    if (g_job.slot || g_absent.count(game_id) || !ReaderStarted()) return nullptr;
    // The slot drawn longest ago makes room, but never one drawn in this
    // frame or the last (still on screen, the GPU may still read it). With
    // none free the box stays a named spine until one is: a screen with
    // more boxes than slots (a smaller display scale) settles instead of
    // reading for ever.
    Slot* victim = nullptr;
    for (Slot& s : g_slots) {
        if (!s.id.empty() && s.frame + 1 >= FrameTimer) continue;
        if (!victim || s.used < victim->used) victim = &s;
    }
    if (!victim) return nullptr;
    if (!victim->data) victim->data = skin::Mem2Alloc(kBoxPixelBytes);
    if (!victim->data) return nullptr;
    if (!victim->id.empty()) g_index.erase(victim->id);
    victim->id = game_id;
    victim->ready = false;
    victim->used = ++g_clock;
    victim->frame = FrameTimer;
    g_index[game_id] = victim;
    if (tile) g_tileReadFor[game_id] = match;

    LWP_MutexLock(g_readLock);
    g_job = ReadJob{};
    g_job.slot = victim;
    g_job.id = game_id;
    g_job.path = BoxPath(game_id);
    if (tile) {
        g_job.tile_from = FromPath(game_id);
        g_job.tile_match = match;
    }
    g_job.forgets = g_forgets;
    LWP_CondSignal(g_readWake);
    LWP_MutexUnlock(g_readLock);
    return nullptr;
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
    g_tileReadFor.erase(game_id);
    // A read of it in flight may be of the old file.
    if (g_job.slot && g_job.id == game_id) ++g_forgets;
    // The player's own box for the name too: looked for and read again.
    const std::string own_key = OwnKey(game_id);
    g_absent.erase(own_key);
    g_ownRetry.erase(own_key);
    g_ownFound.erase(own_key);
    if (g_job.slot && g_job.id == own_key) ++g_forgets;
    const auto own = g_index.find(own_key);
    if (own != g_index.end()) {
        own->second->id.clear();
        own->second->ready = false;
        own->second->used = 0;
        g_index.erase(own);
    }
    const auto found = g_index.find(game_id);
    if (found == g_index.end()) return;
    found->second->id.clear();
    found->second->ready = false;
    found->second->used = 0;
    g_index.erase(found);
}

void ForgetAllBoxes() {
    ++g_forgets;  // a read in flight was of the old card
    g_absent.clear();
    g_ownRetry.clear();
    g_ownFound.clear();
    g_tileReadFor.clear();
    g_index.clear();
    for (Slot& s : g_slots) {
        s.id.clear();
        s.ready = false;
        s.used = 0;
    }
}

}  // namespace riftwii::wii
