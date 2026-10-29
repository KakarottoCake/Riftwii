// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "covers.hpp"

#include <ogc/cache.h>
#include <ogc/gx.h>
#include <ogc/mutex.h>  // before cond.h, which needs mutex_t
#include <ogc/cond.h>
#include <ogc/lwp.h>
#include <ogc/lwp_watchdog.h>
#include <sys/stat.h>
#include <zlib.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <iterator>
#include <set>
#include <vector>

#include "fullfont.hpp"
#include "emulatorcovers.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "memlimits.hpp"
#include "netsock.hpp"
#include "online.hpp"
#include "riftwii/coverart.hpp"
#include "riftwii/emulatorart.hpp"
#include "riftwii/pngdecode.hpp"
#include "riftwii/profiles.hpp"
#include "skin.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kCoverDir = "sd:/riftwii/covers";
constexpr std::time_t kRetryAfter = 7 * 24 * 60 * 60;
// Room for every game's cover (CoverReserve), up to this many: 7 MB of
// MEM2, where 37 MB were free with 326 games. A bigger library shares it.
constexpr int kPoolMax = 400;
// Before the grid says how many: four pages and the game page.
constexpr int kPoolMin = 49;

std::string CoverPath(const std::string& id) { return std::string(kCoverDir) + "/" + id + ".rwc"; }
// An empty file: GameTDB had no cover when it was written.
std::string MissPath(const std::string& id) { return std::string(kCoverDir) + "/" + id + ".none"; }
std::string SecondSetPath(const std::string& id) { return std::string(kCoverDir) + "/" + id + ".second"; }
// A mod tile's downloaded cover (or miss) was for this GameTDB entry
// (#385): a tile keeps its key when renamed, so a cover for the old name's
// match is told from the new one's.
std::string ModFromPath(const std::string& key) { return std::string(kCoverDir) + "/" + key + ".mod"; }
// Beside a downloaded cover of GameTDB's second set: the set the stored
// cover is of, apart from the set chosen (a download for the chosen set
// may not have come yet).
std::string StoredSecondPath(const std::string& id) { return std::string(kCoverDir) + "/" + id + ".rwc-second"; }
bool StoredIsSecond(const std::string& id) {
    struct stat st;
    return stat(StoredSecondPath(id).c_str(), &st) == 0;
}

// A game's six characters, or a homebrew app's four (GameTDB's 9XGX for
// Snes9x GX, riftwii/homebrewmatch.hpp).
bool ValidId(const std::string& id) {
    if (emulator_cover_id(id)) return true;
    // A mod's tile, "<game ID>@<slug>": the player's own cover for it, else
    // the game's (#384).
    if (id.find('@') != std::string::npos) return valid_tile_key(id);
    if (id.size() != 6 && id.size() != 4) return false;
    for (char c : id) {
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

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

// The PNG's zlib stream, with zlib (which the menu links anyway).
bool Inflate(const std::uint8_t* data, std::size_t size, std::uint8_t* out, std::size_t out_size) {
    z_stream z = {};
    if (inflateInit(&z) != Z_OK) return false;
    z.next_in = const_cast<Bytef*>(data);
    z.avail_in = static_cast<uInt>(size);
    z.next_out = out;
    z.avail_out = static_cast<uInt>(out_size);
    int result = Z_OK;
    while (z.avail_out > 0 && result == Z_OK) result = inflate(&z, Z_SYNC_FLUSH);
    inflateEnd(&z);
    return z.avail_out == 0 && (result == Z_OK || result == Z_STREAM_END || result == Z_BUF_ERROR);
}

// PNG to RGBA rows (src/art/pngdecode.cpp, in place of libpng: about 110 KiB
// less of the DOL, which is MEM1 the menu's heap gets back).
bool DecodePng(const std::vector<std::uint8_t>& png, std::vector<std::uint8_t>& rgba, int& w, int& h) {
    std::string error;
    if (!DecodePngRgba(png, rgba, w, h, error)) {
        logf("Covers: %s\n", error.c_str());
        return false;
    }
    return true;
}

// The pool is filled by a loader thread, so the GUI thread never waits on
// the card: it asks for covers (the page shown, then its neighbours) and
// draws those that are ready. A cover read is a FAT lookup and an 18 KB
// read, 26 ms on a tester's Wii, a dropped frame when the drawing did it.
// With nothing asked for, the loader reads the rest of the games' covers
// (CoverReadAll) while there is room, so fast paging cannot outrun it.
struct Slot {
    std::string id;        // the cover in it, or being read into it
    u8* data = nullptr;
    bool ready = false;    // read and flushed: the GUI may draw it
    bool own = false;      // the player's own PNG (#384), not GameTDB's
    bool secondSet = false; // GameTDB's second set, chosen (#384): its full box is the first's
    unsigned fill = 0;     // CoverFill: g_fills when it was read
    bool modCover = false; // a mod tile's own cover from GameTDB (#385)
    u64 touched = 0;       // last drawn or read (gettime ticks)
};
struct Request {
    std::string id;
    int rank;              // 0 the page shown, then the read-ahead order
    u64 at;                // last asked (gettime ticks)
};
Slot g_pool[kPoolMax];
int g_slots = 0;                         // of g_pool, with MEM2
std::map<std::string, Slot*> g_index;    // the slots by id
std::set<std::string> g_absent;          // asked for and not on the card
// CoverWanted's answer to "does the player's own PNG read?", by game: a
// decode each, so once until the covers are forgotten. Menu thread only.
std::map<std::string, bool> g_ownReads;
std::vector<Request> g_requests;
std::vector<std::string> g_readAll;   // CoverReadAll's list
std::size_t g_readAllNext = 0;
mutex_t g_lock = LWP_MUTEX_NULL;
cond_t g_wake = LWP_COND_NULL;   // a request, or the hold lifted
cond_t g_idle = LWP_COND_NULL;   // a read finished
lwp_t g_thread = LWP_THREAD_NULL;
bool g_hold = false;      // CoverLoaderHold: no reads until CoverLoaderRelease
bool g_reading = false;   // the loader is reading the card
bool g_texDirty = false;  // a slot was refilled since the GUI invalidated
unsigned g_forgets = 0;   // ForgetCover calls, so a read in flight is dropped
unsigned g_fills = 0;     // slots read, for CoverFill
bool g_fellBack = false;  // FetchCoverNow went back to the first set (the background's thread)
alignas(32) u8 g_stack[16384];

// A request not renewed for this long is dropped: the grid asks every frame.
constexpr unsigned kRequestMs = 200;
// A slot drawn this recently is not refilled: the GPU may still read it.
constexpr unsigned kDrawnMs = 100;

struct Lock {
    Lock() { LWP_MutexLock(g_lock); }
    ~Lock() { LWP_MutexUnlock(g_lock); }
};

// A downloaded cover (CoverPath) read into `pixels`, its header checked.
bool ReadStoredCover(const std::string& id, u8* pixels) {
    FILE* f = std::fopen(CoverPath(id).c_str(), "rb");
    if (!f) return false;
    std::uint8_t header[kCoverHeaderSize];
    const bool ok = std::fread(header, 1, sizeof(header), f) == sizeof(header) && cover_header_valid(header) &&
                    std::fread(pixels, 1, kCoverPixelBytes, f) == kCoverPixelBytes;
    std::fclose(f);
    return ok;
}

// Mod tiles' GameTDB entries (#385), by tile key: set on the GUI thread,
// read by the download's; under g_lock.
std::map<std::string, std::string> g_modCoverIds;
unsigned g_modCoverIdsCleared = 0;  // ForgetAllCovers calls: matched again

std::string ModCoverId(const std::string& tile_key) {
    if (g_lock == LWP_MUTEX_NULL) return {};
    Lock lock;
    const auto it = g_modCoverIds.find(tile_key);
    return it == g_modCoverIds.end() ? std::string() : it->second;
}

// Whether what is stored for the mod tile (its cover, or its miss) is for
// the GameTDB entry it matches now. Never while it matches none.
bool TileStoredForMatch(const std::string& tile_key) {
    const std::string id = ModCoverId(tile_key);
    if (id.empty()) return false;
    FILE* f = std::fopen(ModFromPath(tile_key).c_str(), "rb");
    if (!f) return false;
    char from[16] = {};
    const std::size_t got = std::fread(from, 1, sizeof(from) - 1, f);
    std::fclose(f);
    return std::string(from, got) == id;
}

Slot* Find(const std::string& game_id) {
    const auto it = g_index.find(game_id);
    return it == g_index.end() ? nullptr : it->second;
}

// A slot's cover, kept in the index. Locked.
void SetId(Slot& s, const std::string& id) {
    if (!s.id.empty()) g_index.erase(s.id);
    s.id = id;
    s.ready = false;
    if (!id.empty()) g_index[id] = &s;
}

Slot* EmptySlot() {
    for (int i = 0; i < g_slots; ++i) {
        if (g_pool[i].id.empty()) return &g_pool[i];
    }
    return nullptr;
}

// The next cover to read, lowest rank first; stale requests dropped.
// Locked.
bool NextRequest(std::string& id) {
    const u64 now = gettime();
    for (std::size_t i = g_requests.size(); i-- > 0;) {
        const Request& r = g_requests[i];
        if (ticks_to_millisecs(now - r.at) > kRequestMs || g_absent.count(r.id) || Find(r.id))
            g_requests.erase(g_requests.begin() + static_cast<std::ptrdiff_t>(i));
    }
    if (g_requests.empty()) return false;
    std::size_t best = 0;
    for (std::size_t i = 1; i < g_requests.size(); ++i) {
        if (g_requests[i].rank < g_requests[best].rank) best = i;
    }
    id = g_requests[best].id;
    g_requests.erase(g_requests.begin() + static_cast<std::ptrdiff_t>(best));
    return true;
}

// The slot to read an asked-for cover into: an empty one, else the one
// longest not drawn or asked for (CoverPrefetch renews a ready one).
// Locked.
Slot* Victim() {
    if (Slot* s = EmptySlot()) return s;
    const u64 now = gettime();
    Slot* pick = nullptr;
    for (int i = 0; i < g_slots; ++i) {
        Slot& s = g_pool[i];
        if (!s.ready || ticks_to_millisecs(now - s.touched) < kDrawnMs) continue;
        if (!pick || s.touched < pick->touched) pick = &s;
    }
    return pick;
}

// With nothing asked for: the next of CoverReadAll's covers not in memory,
// into an empty slot (it never pushes another cover out). Locked.
bool NextReadAll(std::string& id, Slot*& slot) {
    while (g_readAllNext < g_readAll.size()) {
        const std::string& next = g_readAll[g_readAllNext];
        if (!ValidId(next) || g_absent.count(next) || Find(next)) {
            ++g_readAllNext;
            continue;
        }
        slot = EmptySlot();
        if (!slot) {
            g_readAllNext = g_readAll.size();  // full: the rest on demand
            return false;
        }
        id = next;
        ++g_readAllNext;
        return true;
    }
    return false;
}

void* LoaderMain(void*) {
    LWP_MutexLock(g_lock);
    for (;;) {
        std::string id;
        Slot* slot = nullptr;
        const bool asked = !g_hold && NextRequest(id) && (slot = Victim());
        if (!asked && (g_hold || !NextReadAll(id, slot))) {
            LWP_CondWait(g_wake, g_lock);
            continue;
        }
        SetId(*slot, id);
        g_reading = true;
        const unsigned forgets = g_forgets;
        LWP_MutexUnlock(g_lock);

        const bool emulator = emulator_cover_id(id);
        const std::string game = emulator ? id : tile_game_id(id);
        const bool tile = game != id;
        std::vector<std::uint8_t> local;
        const auto own_png = [&](const std::string& name) {
            return ReadOwnCover(name, local) && local.size() == kCoverFileSize;
        };
        bool ok = false, own = false, mod_cover = false;
        if (emulator) {
            ok = ReadEmulatorLocalCover(id, local) && local.size() == kCoverFileSize;
            if (ok) std::memcpy(slot->data, local.data() + kCoverHeaderSize, kCoverPixelBytes);
        } else {
            // The player's own PNG first, never written over, and offline
            // too (#384); a mod tile's own before the game's, and the mod's
            // own cover from GameTDB (#385) before the game's too.
            own = tile && own_png(id);
            if (!own && tile && TileStoredForMatch(id)) ok = mod_cover = ReadStoredCover(id, slot->data);
            if (!ok && !own) own = own_png(game);
            if (own) std::memcpy(slot->data, local.data() + kCoverHeaderSize, kCoverPixelBytes);
            ok = ok || own;
        }
        const bool cache_allowed = !emulator || EmulatorDownloadedCoversAllowed();
        // A mod's tile without a cover of its own shows the game's.
        if (!ok && cache_allowed) ok = ReadStoredCover(game, slot->data);
        if (ok) DCFlushRange(slot->data, kCoverPixelBytes);
        // GameTDB's full box art is of the first set only: a second-set
        // cover comes before it, as the player's own does.
        const bool second_set = ok && !own && !mod_cover && !emulator && StoredIsSecond(game);

        LWP_MutexLock(g_lock);
        g_reading = false;
        LWP_CondBroadcast(g_idle);
        if (ok && forgets == g_forgets) {
            slot->ready = true;
            slot->own = own;
            slot->secondSet = second_set;
            slot->fill = ++g_fills;
            slot->modCover = mod_cover;
            slot->touched = gettime();
            g_texDirty = true;  // GX is the GUI thread's: it invalidates
        } else {
            if (slot->id == id) SetId(*slot, "");
            if (forgets == g_forgets) g_absent.insert(id);
        }
    }
    return nullptr;
}

// MEM2 for `covers` slots in all, in one piece (the heap grows into MEM2
// too: many small pieces between its growths would split it), or as much
// of that as there is. GUI thread.
int g_reserveAsked = 0;  // the most asked for: no retry (and log) each frame
void Reserve(int covers) {
    if (covers > kPoolMax) covers = kPoolMax;
    if (covers <= g_slots || covers <= g_reserveAsked) return;
    g_reserveAsked = covers;
    for (int more = covers - g_slots; more > 0; more /= 2) {
        u8* block = skin::Mem2Alloc(static_cast<std::size_t>(more) * kCoverPixelBytes);
        if (!block) continue;
        Lock lock;
        for (int i = 0; i < more; ++i) g_pool[g_slots + i].data = block + static_cast<std::size_t>(i) * kCoverPixelBytes;
        g_slots += more;
        break;
    }
    logf("Covers: room for %d covers (%d wanted); MEM2 arena left: %u KB\n", g_slots, covers,
         static_cast<unsigned>((reinterpret_cast<u32>(SYS_GetArena2Hi()) - reinterpret_cast<u32>(SYS_GetArena2Lo())) >> 10));
}

// The lock and the loader thread, the first time a cover is asked for,
// with kPoolMin slots unless CoverReserve gave some. GUI thread.
bool Started() {
    if (g_thread != LWP_THREAD_NULL) return true;
    if (g_lock == LWP_MUTEX_NULL) {
        LWP_MutexInit(&g_lock, false);
        LWP_CondInit(&g_wake);
        LWP_CondInit(&g_idle);
    }
    if (g_slots == 0) Reserve(kPoolMin);
    // Below the GUI thread (70) and the menu's: it reads while they wait.
    if (LWP_CreateThread(&g_thread, LoaderMain, nullptr, g_stack, sizeof(g_stack), 40) < 0) {
        g_thread = LWP_THREAD_NULL;
        return false;
    }
    return true;
}

// Every frame for every cover wanted and not ready. Wakes the loader each
// time: one that found no free slot waits for the next ask.
void Ask(const std::string& game_id, int rank) {
    const u64 now = gettime();
    for (Request& r : g_requests) {
        if (r.id == game_id) {
            // Renewed every frame: the loader is woken only when it matters more.
            if (rank < r.rank) LWP_CondSignal(g_wake);
            r.rank = rank;
            r.at = now;
            return;
        }
    }
    g_requests.push_back({game_id, rank, now});
    LWP_CondSignal(g_wake);
}

}  // namespace

void ForgetCoverMiss(const std::string& game_id) {
    if (emulator_cover_id(game_id)) ForgetEmulatorCoverMiss(game_id);
    if (ValidId(game_id)) std::remove(MissPath(game_id).c_str());
}

bool CoverWanted(const std::string& game_id) {
    if (emulator_cover_id(game_id)) return EmulatorCoverWanted(game_id);
    if (!ValidId(game_id)) return false;
    // A mod's tile: only when GameTDB lists the mod as a game of its own;
    // a cover or miss stored for another match (the tile renamed) is none.
    const bool tile = tile_game_id(game_id) != game_id;
    if (tile && ModCoverId(game_id).empty()) return false;
    // The player's own cover is there (and reads): nothing to download.
    if (OwnCoverThere(game_id)) {
        const auto known = g_ownReads.find(game_id);
        if (known == g_ownReads.end()) {
            std::vector<std::uint8_t> own;
            g_ownReads[game_id] = ReadOwnCover(game_id, own);
        }
        if (g_ownReads[game_id]) return false;
    }
    if (tile && !TileStoredForMatch(game_id)) return true;
    struct stat st;
    // A stored cover of the other set than the one chosen (its download
    // never came) is downloaded again.
    if (stat(CoverPath(game_id).c_str(), &st) == 0 && StoredIsSecond(game_id) == CoverSecondSet(game_id)) return false;
    if (stat(MissPath(game_id).c_str(), &st) == 0 && std::time(nullptr) - st.st_mtime < kRetryAfter) return false;
    return true;
}

bool CoverSecondSet(const std::string& game_id) {
    struct stat st;
    return ValidId(game_id) && !emulator_cover_id(game_id) && stat(SecondSetPath(game_id).c_str(), &st) == 0;
}

bool ChooseCoverSet(const std::string& game_id, bool second) {
    if (!ValidId(game_id) || emulator_cover_id(game_id)) return false;
    // A miss was the other set's: this one is asked for afresh.
    std::remove(MissPath(game_id).c_str());
    if (!second) {
        // Not there already is the first set; any other error leaves it.
        errno = 0;
        if (std::remove(SecondSetPath(game_id).c_str()) != 0 && errno != ENOENT) return false;
        return !CoverSecondSet(game_id);
    }
    mkdir("sd:/riftwii", 0777);
    mkdir(kCoverDir, 0777);
    return WriteAll(SecondSetPath(game_id), {});
}

bool CoverStored(const std::string& game_id) {
    struct stat st;
    return ValidId(game_id) && stat(CoverPath(game_id).c_str(), &st) == 0;
}

namespace {
CoverFetch FetchCoverNow(const std::string& game_id, std::string& error);
}  // namespace

// Each download, decode and write checked: a tester's heap broke in a
// menu session that downloaded 25 covers (wii/core/memlimits.hpp WatchHeap).
CoverFetch FetchCover(const std::string& game_id, std::string& error) {
    const CoverFetch got = FetchCoverNow(game_id, error);
    mem::WatchHeap(("after the cover download of " + game_id).c_str());
    return got;
}

namespace {
CoverFetch FetchCoverNow(const std::string& game_id, std::string& error) {
    if (emulator_cover_id(game_id)) return FetchEmulatorCover(game_id, error);
    if (!ValidId(game_id)) return CoverFetch::NotFound;
    // A mod's tile: the cover of the mod's own GameTDB entry, kept under
    // the tile's key (sd:/riftwii/covers/RMCE01@retro-rewind.rwc).
    const bool tile = tile_game_id(game_id) != game_id;
    const std::string art_of = tile ? ModCoverId(game_id) : game_id;
    if (art_of.empty()) return CoverFetch::NotFound;
    mkdir("sd:/riftwii", 0777);
    mkdir(kCoverDir, 0777);
    // The second set is the game page's choice, for the game's own cover.
    const bool second = !tile && CoverSecondSet(game_id);
    for (const std::string& art_id : gametdb_art_ids(art_of)) {
        for (const std::string& region : cover_regions(art_of, MenuLanguage())) {
            std::vector<std::uint8_t> png;
            std::string why;
            if (!HttpGet(cover_url(region, art_id, second), png, why, 512u << 10, 8000)) {
                // GameTDB's refusal (a 404 most often) means no cover in this
                // region; anything else is no answer about this game at all.
                if (cover_answer_means_missing(http_answered_status(why))) continue;
                error = why;
                return CoverFetch::NoAnswer;
            }
            std::vector<std::uint8_t> rgba;
            int w = 0, h = 0;
            if (!DecodePng(png, rgba, w, h)) {
                logf("Covers: GameTDB's %s cover of %s is not a PNG we can read\n", region.c_str(), game_id.c_str());
                continue;
            }
            const std::vector<std::uint8_t> file = make_cover_file(rgba.data(), w, h);
            if (file.empty()) continue;
            // Which set the cover is of, written before the cover: a marker
            // that can't be written leaves the stored cover as it was (a
            // second set's would pass for the first's), and one written for
            // a cover that then can't be is taken back.
            const bool marked_before = StoredIsSecond(game_id);
            if (second && !marked_before && !WriteAll(StoredSecondPath(game_id), {})) {
                error = "cannot write " + StoredSecondPath(game_id) + " (card full?)";
                return CoverFetch::Failed;
            }
            if (!WriteAll(CoverPath(game_id), file)) {
                if (second && !marked_before) std::remove(StoredSecondPath(game_id).c_str());
                error = "cannot write " + CoverPath(game_id) + " (card full?)";
                return CoverFetch::Failed;
            }
            std::remove(MissPath(game_id).c_str());
            if (!second) std::remove(StoredSecondPath(game_id).c_str());
            // Without its entry's name beside it, the tile's cover would be
            // wanted again on every pass: no cover, and a failed download.
            if (tile && !WriteAll(ModFromPath(game_id), std::vector<std::uint8_t>(art_of.begin(), art_of.end()))) {
                std::remove(CoverPath(game_id).c_str());
                error = "cannot write " + ModFromPath(game_id) + " (card full?)";
                return CoverFetch::Failed;
            }
            if (tile) logf("Covers: %s, GameTDB's %s (%s, %dx%d)\n", game_id.c_str(), art_id.c_str(), region.c_str(), w, h);
            else logf("Covers: %s (%s, %dx%d)\n", game_id.c_str(), region.c_str(), w, h);
            return CoverFetch::Stored;
        }
    }
    if (second) {
        // No second set after all: back to the first, and no miss to wait
        // out, since the first may well be there.
        if (!ChooseCoverSet(game_id, false)) {
            error = "cannot write " + SecondSetPath(game_id) + " (card full?)";
            return CoverFetch::Failed;
        }
        logf("Covers: GameTDB has no second cover for %s; back to the first\n", game_id.c_str());
        g_fellBack = true;
        return FetchCoverNow(game_id, error);
    }
    WriteAll(MissPath(game_id), {});
    if (tile) {
        // A cover stored for the tile's earlier match is not this one's.
        std::remove(CoverPath(game_id).c_str());
        // The miss is this entry's only with its name beside it.
        if (!WriteAll(ModFromPath(game_id), std::vector<std::uint8_t>(art_of.begin(), art_of.end()))) {
            std::remove(MissPath(game_id).c_str());
            error = "cannot write " + ModFromPath(game_id) + " (card full?)";
            return CoverFetch::Failed;
        }
    }
    logf("Covers: GameTDB has none for %s\n", game_id.c_str());
    return CoverFetch::NotFound;
}
}  // namespace

namespace {
// The background fetch: written by its thread only between Start and the
// thread's end, read by Take only after it ended.
struct Background {
    std::string id;
    bool second = false;  // the set fetched: chosen when it started, the first after a fallback
    CoverFetch got = CoverFetch::Failed;
    std::string error;
};
Background g_background;
bool g_lastFetchSecond = false;

void RunBackgroundFetch() {
    g_fellBack = false;
    g_background.got = FetchCover(g_background.id, g_background.error);
    if (g_fellBack) g_background.second = false;
}
}  // namespace

bool StartCoverFetch(const std::string& game_id) {
    if (!g_background.id.empty() || NetBackgroundBusy()) return false;
    g_background = Background{};
    g_background.id = game_id;
    g_background.second = CoverSecondSet(game_id);
    if (NetRunInBackground(RunBackgroundFetch, false, false)) return true;
    g_background.id.clear();
    return false;
}

const std::string& CoverFetchGame() { return g_background.id; }

bool TakeCoverFetch(std::string& game_id, CoverFetch& got, std::string& error) {
    // Only this fetch can hold the network's thread while one is started.
    if (g_background.id.empty() || NetBackgroundBusy()) return false;
    NetWaitForBackground();
    game_id = g_background.id;
    got = g_background.got;
    error = g_background.error;
    g_lastFetchSecond = g_background.second;
    g_background.id.clear();
    return true;
}

bool TakenFetchWasSecondSet() { return g_lastFetchSecond; }

const u8* CoverTexture(const std::string& game_id, int rank) {
    if (!ValidId(game_id) || !Started()) return nullptr;
    Lock lock;
    if (g_texDirty) {
        // A refilled slot may still be in the texture cache as its old cover.
        g_texDirty = false;
        GX_InvalidateTexAll();
    }
    if (Slot* s = Find(game_id)) {
        if (s->ready) {
            s->touched = gettime();
            return s->data;
        }
        return nullptr;  // being read
    }
    if (!g_absent.count(game_id)) Ask(game_id, rank);
    return nullptr;
}

bool CoverIsOwn(const std::string& name) {
    if (!ValidId(name) || !Started()) return false;
    Lock lock;
    const Slot* s = Find(name);
    return s && s->ready && s->own;
}

unsigned CoverFill(const std::string& name) {
    if (!ValidId(name) || !Started()) return 0;
    Lock lock;
    const Slot* s = Find(name);
    return s && s->ready ? s->fill : 0;
}

bool CoverBeforeBox(const std::string& name) {
    if (!ValidId(name) || !Started()) return false;
    Lock lock;
    const Slot* s = Find(name);
    return s && s->ready && (s->own || s->secondSet || s->modCover);
}

void CoverPrefetch(const std::string& game_id, int rank) {
    if (!ValidId(game_id) || !Started()) return;
    Lock lock;
    if (Slot* s = Find(game_id)) {
        if (s->ready) s->touched = gettime();  // still wanted: kept over others
    } else if (!g_absent.count(game_id)) {
        Ask(game_id, rank);
    }
}

unsigned ModCoverIdsCleared() {
    if (g_lock == LWP_MUTEX_NULL) return 0;
    Lock lock;
    return g_modCoverIdsCleared;
}

bool SetModCoverIds(std::map<std::string, std::string> ids) {
    if (g_lock == LWP_MUTEX_NULL) return false;
    Lock lock;
    if (ids == g_modCoverIds) return true;
    // A tile matched anew, or no longer, may show another cover now: what
    // is in memory for it (or being read) is read again.
    std::set<std::string> changed;
    for (const auto& now : ids) {
        const auto was = g_modCoverIds.find(now.first);
        if (was == g_modCoverIds.end() || was->second != now.second) changed.insert(now.first);
    }
    for (const auto& was : g_modCoverIds)
        if (!ids.count(was.first)) changed.insert(was.first);
    bool forgot = false;
    for (const std::string& key : changed) {
        g_absent.erase(key);
        if (Slot* slot = Find(key)) {
            SetId(*slot, "");
            forgot = true;
        }
    }
    if (forgot) ++g_forgets;
    g_modCoverIds = std::move(ids);
    return true;
}

void ForgetCover(const std::string& game_id) {
    // The game's, and its mod tiles' (<ID>@<key>).
    for (auto it = g_ownReads.begin(); it != g_ownReads.end();)
        it = it->first == game_id || it->first.rfind(game_id + "@", 0) == 0 ? g_ownReads.erase(it) : std::next(it);
    if (g_lock == LWP_MUTEX_NULL) return;
    Lock lock;
    // The game's mod tiles show its cover too, unless they have their own.
    const std::string tiles = game_id + "@";
    for (auto it = g_absent.begin(); it != g_absent.end();)
        it = *it == game_id || it->compare(0, tiles.size(), tiles) == 0 ? g_absent.erase(it) : std::next(it);
    ++g_forgets;
    for (int i = 0; i < g_slots; ++i)
        if (g_pool[i].id == game_id || g_pool[i].id.compare(0, tiles.size(), tiles) == 0) SetId(g_pool[i], "");
}

void ForgetGameCoversBeforeUsb() {
    g_ownReads.clear();  // the drive may have the player's PNGs
    ForgetUsbCoversFolder();
    if (g_lock == LWP_MUTEX_NULL) return;
    Lock lock;
    for (auto it = g_absent.begin(); it != g_absent.end();)
        it = emulator_cover_id(*it) ? std::next(it) : g_absent.erase(it);
    ++g_forgets;
    for (int i = 0; i < g_slots; ++i)
        if (!g_pool[i].id.empty() && !emulator_cover_id(g_pool[i].id)) SetId(g_pool[i], "");
}

void ForgetAllCovers() {
    g_ownReads.clear();
    if (g_lock == LWP_MUTEX_NULL) return;
    Lock lock;
    // Another SD card, maybe: its tiles are matched again from its list.
    g_modCoverIds.clear();
    ++g_modCoverIdsCleared;
    g_absent.clear();
    ++g_forgets;
    for (int i = 0; i < g_slots; ++i) SetId(g_pool[i], "");
}

void CoverReserve(int covers) {
    if (g_lock == LWP_MUTEX_NULL) {
        LWP_MutexInit(&g_lock, false);
        LWP_CondInit(&g_wake);
        LWP_CondInit(&g_idle);
    }
    Reserve(covers);
}

void CoverReadAll(const std::vector<std::string>& game_ids) {
    if (!Started()) return;
    Lock lock;
    g_readAll = game_ids;
    g_readAllNext = 0;
    LWP_CondSignal(g_wake);
}

void CoverLoaderHold() {
    if (g_lock == LWP_MUTEX_NULL) return;
    Lock lock;
    g_hold = true;
    while (g_reading) LWP_CondWait(g_idle, g_lock);
}

void CoverLoaderRelease() {
    if (g_lock == LWP_MUTEX_NULL) return;
    Lock lock;
    g_hold = false;
    LWP_CondSignal(g_wake);
}

bool DecodePngRgba(const std::vector<std::uint8_t>& png, std::vector<std::uint8_t>& rgba, int& w, int& h,
                   std::string& error) {
    std::uint32_t width = 0, height = 0;
    if (!decode_png(png.data(), png.size(), &Inflate, 1024, rgba, width, height, error)) return false;
    w = static_cast<int>(width);
    h = static_cast<int>(height);
    return true;
}

}  // namespace riftwii::wii

// Every unmount (fatUnmount is dvmUnmountVolume) first stops the cover
// loader, so no read is left in flight on a card going away (Makefile.wii
// links with --wrap=dvmUnmountVolume), and closes the full menu font's
// file, kept closed until the unmount is over (the GUI thread could open
// it again meanwhile): a launch handing the card to the game, an IOS
// reload, a remount. The next glyph that needs it opens it again.
extern "C" void __real_dvmUnmountVolume(const char* name);
extern "C" void __wrap_dvmUnmountVolume(const char* name) {
    const riftwii::wii::fullfont::ClosedHold font_closed("a drive is unmounted");
    riftwii::wii::CoverLoaderHold();
    __real_dvmUnmountVolume(name);
}
