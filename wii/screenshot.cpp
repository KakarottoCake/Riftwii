// SPDX-License-Identifier: GPL-3.0-or-later
#include "screenshot.hpp"

#include <gccore.h>
#include <ogc/isfs.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "libwiigui/gui.h"
#include "log.hpp"
#include "riftwii/shotfile.hpp"
#include "skin.hpp"

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
    const bool ok = write_yuyv_png(yuyv, width, lines, std::size_t(width) * 2, double_lines, FileSink, f, g_work);
    std::fflush(f);
    fsync(fileno(f));
    std::fclose(f);
    if (!ok) {
        unlink(path.c_str());
        logf("Screenshot: writing %s failed\n", path.c_str());
        return false;
    }
    logf("Screenshot: %s (%ux%u, %u ms)\n", path.c_str(), static_cast<unsigned>(width),
         static_cast<unsigned>(double_lines ? lines * 2 : lines), static_cast<unsigned>(diff_msec(start, gettime())));
    return true;
}

// The in-game captures on the NAND, each to a PNG, then deleted there.
void ImportGameShots() {
    if (ISFS_Initialize() < 0) return;
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
    unsigned done = 0, dropped = 0;
    const char* name = names;
    for (u32 i = 0; i < count && !g_stop; ++i, name += std::strlen(name) + 1) {
        const std::size_t len = std::strlen(name);
        if (len < 5 || len > 12 || std::strcmp(name + len - 4, ".raw") != 0) continue;
        char path[80];
        std::snprintf(path, sizeof(path), "%s/%s", kGameShotDir, name);
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
            logf("Screenshot: %s: %s; deleted\n", path, got < 0 ? "unreadable" : error.c_str());
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
struct Chord {
    bool held_back = false;  // the modifier went down; its press is owed
    bool used = false;       // the combo fired while it was held
};
Chord g_remote[4];
Chord g_pad_l[4], g_pad_r[4];

void Fire() {
    if (g_busy || g_want) return;
    g_want = true;
}

// Keeps a modifier's press back; returns whether it is held.
bool Modifier(Chord& c, u32& down, u32 held, u32 bit) {
    if (down & bit) {
        down &= ~bit;
        c.held_back = true;
        c.used = false;
    }
    if (c.held_back && !(held & bit)) {
        if (!c.used) down |= bit;  // let go alone: the press, late
        c.held_back = false;
    }
    return c.held_back && (held & bit);
}

}  // namespace

void ScreenshotsStart() {
    if (g_thread != LWP_THREAD_NULL) return;
    g_stop = false;
    if (LWP_CreateThread(&g_thread, Worker, nullptr, nullptr, 16384, 30) < 0) g_thread = LWP_THREAD_NULL;
}

void ScreenshotsStop() {
    if (g_thread == LWP_THREAD_NULL) return;
    g_stop = true;
    LWP_JoinThread(g_thread, nullptr);
    g_thread = LWP_THREAD_NULL;
}

void ScreenshotPoll() {
    for (int i = 0; i < 4; ++i) {
        WPADData* w = userInput[i].wpad;
        if (w) {
            u32 down = w->btns_d;
            if (Modifier(g_remote[i], down, w->btns_h, WPAD_BUTTON_1) && (down & WPAD_BUTTON_HOME)) {
                down &= ~WPAD_BUTTON_HOME;
                g_remote[i].used = true;
                Fire();
            }
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
        p.btns_d = static_cast<u16>(down);
    }
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

}  // namespace riftwii::wii
