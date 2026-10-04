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

#include <cstdio>
#include <ctime>
#include <map>
#include <set>
#include <vector>

#include "loadersettings.hpp"
#include "log.hpp"
#include "memlimits.hpp"
#include "online.hpp"
#include "riftwii/coverart.hpp"
#include "riftwii/pngdecode.hpp"
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

bool ValidId(const std::string& id) {
    if (id.size() != 6) return false;
    for (char c : id) {
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    }
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

// PNG to RGBA rows (src/pngdecode.cpp, in place of libpng: about 110 KiB
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
alignas(32) u8 g_stack[16384];

// A request not renewed for this long is dropped: the grid asks every frame.
constexpr unsigned kRequestMs = 200;
// A slot drawn this recently is not refilled: the GPU may still read it.
constexpr unsigned kDrawnMs = 100;

struct Lock {
    Lock() { LWP_MutexLock(g_lock); }
    ~Lock() { LWP_MutexUnlock(g_lock); }
};

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

        bool ok = false;
        if (FILE* f = std::fopen(CoverPath(id).c_str(), "rb")) {
            std::uint8_t header[kCoverHeaderSize];
            ok = std::fread(header, 1, sizeof(header), f) == sizeof(header) && cover_header_valid(header) &&
                 std::fread(slot->data, 1, kCoverPixelBytes, f) == kCoverPixelBytes;
            std::fclose(f);
        }
        if (ok) DCFlushRange(slot->data, kCoverPixelBytes);

        LWP_MutexLock(g_lock);
        g_reading = false;
        LWP_CondBroadcast(g_idle);
        if (ok && forgets == g_forgets) {
            slot->ready = true;
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
    LWP_CondSignal(g_wake);
    const u64 now = gettime();
    for (Request& r : g_requests) {
        if (r.id == game_id) {
            r.rank = rank;
            r.at = now;
            return;
        }
    }
    g_requests.push_back({game_id, rank, now});
}

}  // namespace

bool CoverWanted(const std::string& game_id) {
    if (!ValidId(game_id)) return false;
    struct stat st;
    if (stat(CoverPath(game_id).c_str(), &st) == 0) return false;
    if (stat(MissPath(game_id).c_str(), &st) == 0 && std::time(nullptr) - st.st_mtime < kRetryAfter) return false;
    return true;
}

bool CoverStored(const std::string& game_id) {
    struct stat st;
    return ValidId(game_id) && stat(CoverPath(game_id).c_str(), &st) == 0;
}

namespace {
CoverFetch FetchCoverNow(const std::string& game_id, std::string& error);
}  // namespace

// Each download, decode and write checked: a tester's heap broke in a
// menu session that downloaded 25 covers (wii/memlimits.hpp WatchHeap).
CoverFetch FetchCover(const std::string& game_id, std::string& error) {
    const CoverFetch got = FetchCoverNow(game_id, error);
    mem::WatchHeap(("after the cover download of " + game_id).c_str());
    return got;
}

namespace {
CoverFetch FetchCoverNow(const std::string& game_id, std::string& error) {
    if (!ValidId(game_id)) return CoverFetch::NotFound;
    mkdir("sd:/riftwii", 0777);
    mkdir(kCoverDir, 0777);
    for (const std::string& region : cover_regions(game_id, MenuLanguage())) {
        std::vector<std::uint8_t> png;
        std::string why;
        if (!HttpGet(cover_url(region, game_id), png, why, 512u << 10, 8000)) {
            static const std::string kMissing = " answered 404";  // HttpGet's words for a 404
            if (why.size() >= kMissing.size() && why.compare(why.size() - kMissing.size(), kMissing.size(), kMissing) == 0) continue;
            error = why;
            return CoverFetch::Failed;
        }
        std::vector<std::uint8_t> rgba;
        int w = 0, h = 0;
        if (!DecodePng(png, rgba, w, h)) {
            logf("Covers: GameTDB's %s cover of %s is not a PNG we can read\n", region.c_str(), game_id.c_str());
            continue;
        }
        const std::vector<std::uint8_t> file = make_cover_file(rgba.data(), w, h);
        if (file.empty()) continue;
        if (!WriteAll(CoverPath(game_id), file)) {
            error = "cannot write " + CoverPath(game_id) + " (card full?)";
            return CoverFetch::Failed;
        }
        std::remove(MissPath(game_id).c_str());
        logf("Covers: %s (%s, %dx%d)\n", game_id.c_str(), region.c_str(), w, h);
        return CoverFetch::Stored;
    }
    WriteAll(MissPath(game_id), {});
    logf("Covers: GameTDB has none for %s\n", game_id.c_str());
    return CoverFetch::NotFound;
}
}  // namespace

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

void CoverPrefetch(const std::string& game_id, int rank) {
    if (!ValidId(game_id) || !Started()) return;
    Lock lock;
    if (Slot* s = Find(game_id)) {
        if (s->ready) s->touched = gettime();  // still wanted: kept over others
    } else if (!g_absent.count(game_id)) {
        Ask(game_id, rank);
    }
}

void ForgetCover(const std::string& game_id) {
    if (g_lock == LWP_MUTEX_NULL) return;
    Lock lock;
    g_absent.erase(game_id);
    ++g_forgets;
    if (Slot* s = Find(game_id)) SetId(*s, "");
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
// links with --wrap=dvmUnmountVolume).
extern "C" void __real_dvmUnmountVolume(const char* name);
extern "C" void __wrap_dvmUnmountVolume(const char* name) {
    riftwii::wii::CoverLoaderHold();
    __real_dvmUnmountVolume(name);
}
