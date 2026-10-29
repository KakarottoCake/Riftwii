// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "screenshot.hpp"

#include <gccore.h>
#include <ogc/isfs.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "libwiigui/gui.h"
#include "video.h"
#include "log.hpp"
#include "riftwii/homechord.hpp"
#include "riftwii/shotfile.hpp"
#include "skin.hpp"
#include "nanduse.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kShotFolder = "sd:/riftwii/screenshots";
constexpr std::size_t kFrameBytes = kShotHeaderBytes + std::size_t(kShotMaxWidth) * 2 * kShotMaxLines;

// Worker: one job at a time; the GUI thread hands a frame over only
// while it is idle.
lwp_t g_thread = LWP_THREAD_NULL;
volatile bool g_stop = false;
volatile bool g_busy = false;       // a frame is being written (or the import runs)
volatile bool g_frame_ready = false;
bool g_want = false;                // the combo fired: copy the next frame
int g_flash = 0;

// MEM2, taken on first use: a frame (or a raw file from the NAND) and the
// encoder's tables.
u8* g_frame = nullptr;
void* g_work = nullptr;
int g_frame_width = 0, g_frame_height = 0;

// The next free number per prefix, from the folder at first use.
std::map<std::string, unsigned> g_next;

bool Buffers() {
    if (!g_frame) g_frame = skin::Mem2Alloc(kFrameBytes);
    if (!g_work) g_work = skin::Mem2Alloc(PngWriter::work_bytes());
    return g_frame && g_work;
}

unsigned NextNumber(const std::string& prefix) {
    auto it = g_next.find(prefix);
    if (it == g_next.end()) {
        unsigned highest = 0;
        if (DIR* d = opendir(kShotFolder)) {
            while (dirent* e = readdir(d)) {
                const std::string name = e->d_name;
                if (name.size() < prefix.size() + 6 || name.compare(0, prefix.size() + 1, prefix + "-") != 0) continue;
                const unsigned n = static_cast<unsigned>(std::strtoul(name.c_str() + prefix.size() + 1, nullptr, 10));
                if (n > highest) highest = n;
            }
            closedir(d);
        }
        it = g_next.emplace(prefix, highest + 1).first;
    }
    return it->second++;
}

bool FileSink(void* user, const std::uint8_t* data, std::size_t size) {
    return std::fwrite(data, 1, size, static_cast<FILE*>(user)) == size;
}

// Writes a YUYV picture as the next <prefix>-NNNN.png.
bool SavePng(const std::string& prefix, const u8* yuyv, std::uint32_t width, std::uint32_t lines, bool double_lines) {
    mkdir("sd:/riftwii", 0777);
    mkdir(kShotFolder, 0777);
    const std::string path = std::string(kShotFolder) + "/" + shot_file_name(prefix, NextNumber(prefix));
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        logf("Screenshot: cannot write %s\n", path.c_str());
        return false;
    }
    const u64 start = gettime();
    // The file's end counts too: a write the card refused while the PNG was
    // buffered came out as a broken PNG that stayed (a tester).
    bool ok = write_yuyv_png(yuyv, width, lines, std::size_t(width) * 2, double_lines, FileSink, f, g_work);
    ok = std::fflush(f) == 0 && ok;
    ok = fsync(fileno(f)) == 0 && ok;
    ok = std::fclose(f) == 0 && ok;
    if (!ok) {
        unlink(path.c_str());
        logf("Screenshot: writing %s failed\n", path.c_str());
        return false;
    }
    logf("Screenshot: %s (%ux%u, %u ms)\n", path.c_str(), static_cast<unsigned>(width),
         static_cast<unsigned>(double_lines ? lines * 2 : lines), static_cast<unsigned>(diff_msec(start, gettime())));
    return true;
}

// Shots still being written when their game ended (the blob writes in
// part/ and moves a file out once it is whole): never whole, deleted.
void DropUnfinishedShots() {
    static char names[64 * 13] ATTRIBUTE_ALIGN(32);
    char dir[48];
    std::snprintf(dir, sizeof(dir), "%s/part", kGameShotDir);
    u32 count = 0;
    if (ISFS_ReadDir(dir, nullptr, &count) < 0 || count == 0) return;
    if (count > 64) count = 64;
    std::memset(names, 0, sizeof(names));
    if (ISFS_ReadDir(dir, names, &count) < 0) return;
    const char* name = names;
    for (u32 i = 0; i < count; ++i, name += std::strlen(name) + 1) {
        char path[80];
        // A NAND file name is at most 12 characters.
        std::snprintf(path, sizeof(path), "%s/%.12s", dir, name);
        ISFS_Delete(path);
        logf("Screenshot: %s was not finished when its game ended; deleted\n", path);
    }
}

// The in-game captures on the NAND, each to a PNG, then deleted there.
// The NAND is open (ImportGameShots).
void ImportOpenShots() {
    DropUnfinishedShots();
    u32 count = 0;
    if (ISFS_ReadDir(kGameShotDir, nullptr, &count) < 0 || count == 0) return;
    static char names[64 * 13] ATTRIBUTE_ALIGN(32);
    if (count > 64) count = 64;
    std::memset(names, 0, sizeof(names));
    if (ISFS_ReadDir(kGameShotDir, names, &count) < 0) return;
    if (!Buffers()) {
        logf("Screenshot: no memory to import the game's screenshots\n");
        return;
    }
    // In the order they were taken (the NAND lists in its own order).
    std::vector<std::string> files;
    const char* name = names;
    for (u32 i = 0; i < count; ++i, name += std::strlen(name) + 1) {
        const std::size_t len = std::strlen(name);
        if (len >= 5 && len <= 12 && std::strcmp(name + len - 4, ".raw") == 0) files.push_back(name);
    }
    std::sort(files.begin(), files.end());
    unsigned done = 0, dropped = 0;
    for (const std::string& file : files) {
        if (g_stop) break;
        char path[80];
        std::snprintf(path, sizeof(path), "%s/%s", kGameShotDir, file.c_str());
        const s32 fd = ISFS_Open(path, ISFS_OPEN_READ);
        if (fd < 0) {
            logf("Screenshot: cannot open %s (%d)\n", path, static_cast<int>(fd));
            continue;
        }
        static fstats stats ATTRIBUTE_ALIGN(32);
        s32 size = ISFS_GetFileStats(fd, &stats) < 0 ? -1 : static_cast<s32>(stats.file_length);
        if (size > static_cast<s32>(kFrameBytes)) size = kFrameBytes;
        const s32 got = size > 0 ? ISFS_Read(fd, g_frame, static_cast<u32>(size)) : size;
        ISFS_Close(fd);
        ShotInfo info;
        std::string error;
        if (got < 0 || !parse_shot_header(g_frame, static_cast<std::size_t>(got), info, error)) {
            // Cut short by a power-off, or not ours: it would only fill the NAND.
            // The NAND's own answer: a size it could not give (-1), or a read it refused.
            if (got < 0)
                logf("Screenshot: %s: unreadable (size %d, read %d); deleted\n", path, static_cast<int>(size),
                     static_cast<int>(got));
            else
                logf("Screenshot: %s: %s; deleted\n", path, error.c_str());
            ISFS_Delete(path);
            ++dropped;
            continue;
        }
        if (!SavePng(info.game_id.empty() ? "game" : info.game_id, g_frame + kShotHeaderBytes, info.width,
                     info.lines, info.flags & kShotFlagDoubleLines))
            break;  // the card: try again next time
        ISFS_Delete(path);
        ++done;
    }
    if (done || dropped) logf("Screenshot: %u taken in games saved, %u dropped\n", done, dropped);
}

// Closes only what it opened: the count is shared (nanduse.hpp), and a
// close after a failed open took the menu's channel scan's use away, so
// /dev/fs closed under that scan as before the count.
void ImportGameShots() {
    if (NandOpen() < 0) return;
    ImportOpenShots();
    // /dev/fs is not kept open: an IOS reload with it open damages the heap.
    NandClose();
}

void* Worker(void*) {
    g_busy = true;
    ImportGameShots();
    g_busy = false;
    while (!g_stop) {
        if (g_frame_ready) {
            SavePng("riftwii", g_frame, static_cast<std::uint32_t>(g_frame_width),
                    static_cast<std::uint32_t>(g_frame_height), false);
            g_frame_ready = false;
            g_busy = false;
        }
        usleep(50000);
    }
    return nullptr;
}

// The combo per controller. The held button's own press is kept back
// until it is let go without the combo.
// The combo per controller, kept back as riftwii/homechord.hpp does.
using Chord = HeldBack;
Chord g_remote[4];
Chord g_pad_l[4], g_pad_r[4];
// 1 and 2 together on Home: 2 is kept back as 1 is, so the pair works
// whichever goes down first, but only while Home owns the controllers.
Chord g_remote_two[4];
volatile bool g_view_chord = false;  // the pair was pressed; Home takes it

// Whether Home owns the controllers this frame: its pairs and its search
// hold are watched, and their presses kept back, only then. Decided once a
// frame at the start of ScreenshotPoll, on the GUI thread that reads the
// controllers, by the function Home gives SetHomeOwner: so it is never a
// frame late, whichever screen, box, carry or close-up takes the buttons
// or gives them back, and only this thread writes it.
bool (*volatile g_home_owner)() = nullptr;
bool g_home_owns = false;
// Whether this 1 and this 2 went down while Home owned the controllers: a
// 1 held since before (during the HOME Menu, say) is no half of the pair,
// even once Home owns them again. Cleared when Home lets them go.
bool g_one_watched[4] = {};
bool g_two_watched[4] = {};
// The same pair on the other controllers, on the buttons that are 1 and 2
// for them on Home: a Classic Controller's (or Wii U Pro Controller's) Y
// and X, a GameCube controller's Y and Z, the Wii U GamePad's X and Y.
ViewPair g_classic_pair[4], g_pad_pair[4], g_drc_pair;

// Home's random game by pad: the search button's own button held, B on a
// GameCube controller, ZL on a Classic Controller (or Wii U Pro
// Controller). Its press is kept back while Home owns the controllers:
// let go sooner, it is the search.
constexpr u32 kSearchHoldFrames = 50;  // as B held on the search button with a Wii Remote
SearchHold g_pad_search[4], g_classic_search[4];
volatile bool g_search_hold = false;  // held its second; Home takes it

// One frame of a controller's pair or hold (riftwii/homechord.hpp).
void WatchPair(ViewPair& v, u32& down, u32 held, u32 first, u32 second) {
    if (watch_pair(v, g_home_owns, down, held, first, second)) g_view_chord = true;
}

void WatchHold(SearchHold& h, u32& down, u32 held, u32 bit) {
    if (watch_hold(h, g_home_owns, FrameTimer, kSearchHoldFrames, down, held, bit)) g_search_hold = true;
}

// No half of a pair pressed so far counts towards one any more.
void ForgetPairHalves() {
    for (int i = 0; i < 4; ++i) {
        g_one_watched[i] = g_two_watched[i] = false;
        for (ViewPair* v : {&g_classic_pair[i], &g_pad_pair[i]}) v->first_watched = v->second_watched = false;
    }
    g_drc_pair.first_watched = g_drc_pair.second_watched = false;
}

// Whether Home owns the controllers at this moment, asked afresh (not the
// frame's decision): a carry or a close-up can start later in the frame.
bool HomeOwnsNow() {
    bool (*owner)() = g_home_owner;
    return owner != nullptr && owner();
}

// At the start of each frame: Home's owner function says whether Home owns
// the controllers now. When it lets them go (a screen, a box, a carry or a
// close-up took them, or Home closed), every press it kept back comes to
// nothing, a half pair or a hold begun on Home counts for nothing, and a
// pair or hold finished this frame is dropped: the presses were made on
// Home, and the new owner gets its own presses from this frame on.
void DecideHomeOwner() {
    bool (*owner)() = g_home_owner;
    const bool owns = owner != nullptr && owner();
    if (owns == g_home_owns) return;
    g_home_owns = owns;
    if (owns) return;
    ForgetPairHalves();
    g_view_chord = false;
    g_search_hold = false;
    SpendHeldPresses();
}

void Fire() {
    if (g_busy || g_want) return;
    g_want = true;
}

// Keeps a modifier's press back; returns whether it is held.
bool Modifier(Chord& c, u32& down, u32 held, u32 bit) { return hold_back(c, down, held, bit); }

}  // namespace

void ScreenshotsStart() {
    if (g_thread != LWP_THREAD_NULL) return;
    g_stop = false;
    // Busy from now: the import uses g_frame, and the GUI thread must not
    // copy a frame into it before the worker has even started.
    g_busy = true;
    if (LWP_CreateThread(&g_thread, Worker, nullptr, nullptr, 16384, 30) < 0) {
        g_thread = LWP_THREAD_NULL;
        g_busy = false;
    }
}

void ScreenshotsStop() {
    if (g_thread == LWP_THREAD_NULL) return;
    g_stop = true;
    LWP_JoinThread(g_thread, nullptr);
    g_thread = LWP_THREAD_NULL;
}

void ScreenshotPoll() {
    DecideHomeOwner();
    for (int i = 0; i < 4; ++i) {
        WPADData* w = userInput[i].wpad;
        if (w) {
            u32 down = w->btns_d;
            if (g_home_owns && (down & WPAD_BUTTON_1)) g_one_watched[i] = true;
            if (g_home_owns && (down & WPAD_BUTTON_2)) g_two_watched[i] = true;
            if (Modifier(g_remote[i], down, w->btns_h, WPAD_BUTTON_1) && (down & WPAD_BUTTON_HOME)) {
                down &= ~WPAD_BUTTON_HOME;
                g_remote[i].used = true;
                Fire();
            }
            // A 2 kept back is let go here even after Home lets the
            // controllers go (DecideHomeOwner spends it, so it comes to
            // nothing), so its hold-back always ends.
            const bool one = g_remote[i].held_back && (w->btns_h & WPAD_BUTTON_1);
            if (g_home_owns || g_remote_two[i].held_back) {
                const bool two = Modifier(g_remote_two[i], down, w->btns_h, WPAD_BUTTON_2);
                // Only a pair made while Home owns the controllers is used
                // up: one made after gives both presses back when let go.
                // As riftwii::watch_pair: a half already used (a wake press)
                // makes the pair, and the other half, come to nothing.
                if (g_home_owns && one && two && g_one_watched[i] && g_two_watched[i]) {
                    const bool fresh = !g_remote[i].used && !g_remote_two[i].used;
                    g_remote[i].used = g_remote_two[i].used = true;
                    if (fresh) g_view_chord = true;
                }
            }
            WatchPair(g_classic_pair[i], down, w->btns_h, WPAD_CLASSIC_BUTTON_Y, WPAD_CLASSIC_BUTTON_X);
            WatchHold(g_classic_search[i], down, w->btns_h, WPAD_CLASSIC_BUTTON_ZL);
            w->btns_d = down;
        }
        PADData& p = userInput[i].pad;
        u32 down = p.btns_d;
        const bool l = Modifier(g_pad_l[i], down, p.btns_h, PAD_TRIGGER_L);
        const bool r = Modifier(g_pad_r[i], down, p.btns_h, PAD_TRIGGER_R);
        if (l && r && (down & PAD_BUTTON_DOWN)) {
            down &= ~PAD_BUTTON_DOWN;
            g_pad_l[i].used = g_pad_r[i].used = true;
            Fire();
        }
        WatchPair(g_pad_pair[i], down, p.btns_h, PAD_BUTTON_Y, PAD_TRIGGER_Z);
        WatchHold(g_pad_search[i], down, p.btns_h, PAD_BUTTON_B);
        p.btns_d = static_cast<u16>(down);
    }
    GamePadData& drc = userInput[0].wiidrcdata;
    u32 down = drc.btns_d;
    WatchPair(g_drc_pair, down, drc.btns_h, WIIDRC_BUTTON_X, WIIDRC_BUTTON_Y);
    drc.btns_d = static_cast<u16>(down);
}

void ScreenshotAfterFrame(const void* xfb, int width, int height) {
    if (g_flash > 0) g_flash = g_flash > 24 ? g_flash - 24 : 0;
    if (!g_want) return;
    g_want = false;
    if (g_thread == LWP_THREAD_NULL || g_busy || width <= 0 || height <= 0 || width > int(kShotMaxWidth) ||
        height > int(kShotMaxLines) || !Buffers())
        return;
    // The frame GX wrote: read through the cache, which may hold older lines.
    const std::size_t bytes = std::size_t(width) * 2 * height;
    void* cached = MEM_K1_TO_K0(xfb);
    DCInvalidateRange(cached, bytes);
    std::memcpy(g_frame, cached, bytes);
    g_frame_width = width;
    g_frame_height = height;
    g_busy = true;
    g_frame_ready = true;
    g_flash = 200;
}

int ScreenshotFlash() { return g_flash; }

void SetHomeOwner(bool (*owns)()) {
    g_home_owner = owns;
}

// A pair or hold finishes when a frame's presses are read, and Home takes
// it later. It counts only if Home still owns the controllers when it does:
// one finished in the frame a carry or a close-up began, or a box opened,
// is dropped here, whatever the reason Home let go.
bool TakeViewChord() {
    if (!g_view_chord) return false;
    g_view_chord = false;
    return HomeOwnsNow();
}

void ForgetViewChord() { g_view_chord = false; }

void SpendHeldPresses() {
    // Let go later, a press kept back here comes to nothing.
    const auto spend = [](Chord& c) {
        if (c.held_back) c.used = true;
    };
    for (int i = 0; i < 4; ++i) {
        for (Chord* c : {&g_remote[i], &g_remote_two[i], &g_pad_l[i], &g_pad_r[i], &g_classic_pair[i].first,
                         &g_classic_pair[i].second, &g_pad_pair[i].first, &g_pad_pair[i].second,
                         &g_pad_search[i].button, &g_classic_search[i].button})
            spend(*c);
    }
    spend(g_drc_pair.first);
    spend(g_drc_pair.second);
}

void ForgetPopupPresses() {
    g_view_chord = false;
    g_search_hold = false;  // a hold finished as the box opened starts no game after it
    ForgetPairHalves();
    SpendHeldPresses();
}

bool TakeSearchHold() {
    if (!g_search_hold) return false;
    g_search_hold = false;
    return HomeOwnsNow();
}

}  // namespace riftwii::wii
