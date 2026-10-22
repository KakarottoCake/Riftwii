// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The RiftWii channel: a moment of the RiftWii start screen (the rift
// tears open and the name comes out of it), then RiftWii itself from the
// SD card (or the USB drive). The channel holds
// only this program, never RiftWii: RiftWii's own updates replace
// apps/riftwii/boot.dol, and the channel starts whatever is there.
//
// The DOL is read whole into MEM2, then copied into place by the code
// in channel/common/dolboot.c. The channel's boot program
// (channel/loader) reads this one from the channel's content 2 and starts
// it; it is linked at 0x80100000, above that program and below RiftWii
// (0x80a00000 up).
//
// The SD card and the USB drive are read on a thread of their own, while
// the main thread shows each step under the name and gives up on a step
// that takes too long (issue #228: on some vWii consoles the channel
// stopped for good after the intro, with nothing on screen to say where).
//
// For a test build that stops at one step, as such a console does, define
// CHANNEL_TEST_HANG as that step's number (the Step enum below):
//   make -f Makefile.channel CHANNEL_TEST_HANG=1    (stops at mounting the SD card)
// and a plain make -f Makefile.channel builds the normal one again.

#include <gccore.h>
#include <fat.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/usbstorage.h>
#include <sdcard/wiisd_io.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dolboot.h"
// The start screen's pictures (tools/make_channel_art.py): the backdrop
// (RGB, drawn twice its size), the name (RGBA) and the rift (alpha).
#include "splash.h"
#include "splash_bg_rgb.h"
#include "splash_rift_a.h"
#include "splash_word_rgba.h"

static const char* const kPaths[] = {
    "sd:/apps/riftwii/boot.dol",
    "usb:/apps/riftwii/boot.dol",
};

static GXRModeObj* g_mode;
static u32* g_xfb;
// How wide the name and the rift are drawn, in 256ths: 192 on a 16:9 TV,
// where the picture is stretched to the screen's full width (as the
// menu's) and each pixel is wider by 4:3.
static int g_squeeze = 256;

// The screen in the Wii's own pixel format: two pixels share one Cb/Cr.
static u32 yuv_pair(const int* c0, const int* c1) {
    const int y0 = (77 * c0[0] + 150 * c0[1] + 29 * c0[2]) >> 8;
    const int y1 = (77 * c1[0] + 150 * c1[1] + 29 * c1[2]) >> 8;
    const int r = (c0[0] + c1[0]) >> 1, g = (c0[1] + c1[1]) >> 1, b = (c0[2] + c1[2]) >> 1;
    int cb = 128 + ((-43 * r - 85 * g + 128 * b) >> 8);
    int cr = 128 + ((128 * r - 107 * g - 21 * b) >> 8);
    if (cb < 0) cb = 0;
    if (cb > 255) cb = 255;
    if (cr < 0) cr = 0;
    if (cr > 255) cr = 255;
    return ((u32)y0 << 24) | ((u32)cb << 16) | ((u32)y1 << 8) | (u32)cr;
}

// One frame of the start screen. Levels are 0..256.
typedef struct {
    int sky;    // the backdrop's brightness
    int tear;   // how far the rift has opened (its height)
    int glow;   // the rift's brightness
    int flash;  // white over everything
    int word;   // the name's opacity
    int apart;  // how far "Rift" and "Wii" still are from their places, toward the rift
} Frame;

static void blend(int* c, int r, int g, int b, int a) {
    c[0] += (r - c[0]) * a >> 8;
    c[1] += (g - c[1]) * a >> 8;
    c[2] += (b - c[2]) * a >> 8;
}

// The status line under the name: which step of the start the channel is
// on (show_status), so a console that stops shows where it stopped. In the
// console's own 8x16 font, which fail() links in anyway.
extern u8 console_font_8x16[];
enum { kGlyphWidth = 8, kGlyphHeight = 16 };
static const char* g_status = NULL;
static Frame g_last;  // the frame on screen, to draw a new status line over

static int status_top(void) { return g_mode->xfbHeight - 72; }

// Whether the status text covers screen pixel `px` on glyph row `glyph_y`.
// It is narrowed on a 16:9 TV, as the art is.
static bool status_covers(int px, int glyph_y, int text_x, int length) {
    if (px < text_x) return false;
    const int column = (px - text_x) * 256 / g_squeeze;
    const int index = column / kGlyphWidth;
    if (index >= length) return false;
    const u8 bits = console_font_8x16[(u8)g_status[index] * kGlyphHeight + glyph_y];
    return bits & (0x80 >> (column % kGlyphWidth));
}

// Rows y_from..y_to-1 of a frame, into the framebuffer.
static void render_rows(const Frame* f, int y_from, int y_to) {
    const int w = g_mode->fbWidth, h = g_mode->xfbHeight;
    const int sq = g_squeeze;
    const int word_x = (w - kWordWidth * sq / 256) / 2, word_y = (h - kWordHeight) / 2 - 6;
    const int seam_x = word_x + kWordSeam * sq / 256;  // where the rift is, on screen
    const int cy = h / 2 - 6;
    const int half = kRiftHeight * f->tear / 512;  // the rift's half height now
    const int text_length = g_status ? (int)strlen(g_status) : 0;
    const int text_x = (w - text_length * kGlyphWidth * sq / 256) / 2;
    for (int y = y_from; y < y_to; ++y) {
        u32* row = g_xfb + y * (w / 2);
        const u8* bg_row = splash_bg_rgb + (y * kBgHeight / h) * kBgWidth * 3;
        const int wy = y - word_y;
        const bool in_word = f->word > 0 && wy >= 0 && wy < kWordHeight;
        const bool in_rift = half > 0 && y >= cy - half && y < cy + half;
        const int ry = in_rift ? kRiftHeight / 2 + (y - cy) * (kRiftHeight / 2) / half : 0;
        const int glyph_y = y - status_top();
        const bool in_status = text_length > 0 && glyph_y >= 0 && glyph_y < kGlyphHeight;
        for (int x = 0; x < w; x += 2) {
            int c[2][3];
            for (int i = 0; i < 2; ++i) {
                const int px = x + i;
                const u8* bg = bg_row + (px * kBgWidth / w) * 3;
                c[i][0] = bg[0] * f->sky >> 8;
                c[i][1] = bg[1] * f->sky >> 8;
                c[i][2] = bg[2] * f->sky >> 8;
                const int rx = (px - seam_x) * 256 / sq + kRiftWidth / 2;
                if (in_rift && rx >= 0 && rx < kRiftWidth) {
                    const int a = splash_rift_a[ry * kRiftWidth + rx] * f->glow >> 8;
                    blend(c[i], 205, 238, 255, a);
                }
                if (in_word) {
                    // "Rift" is drawn nearer the rift by `apart`, "Wii" too
                    const int art_x = px < word_x ? -1 : (px - word_x) * 256 / sq;
                    int wx = art_x;
                    wx += wx < kWordSeam ? -f->apart : f->apart;
                    const int side = art_x < kWordSeam;
                    if (wx >= 0 && wx < kWordWidth && (wx < kWordSeam) == side) {
                        const u8* p = splash_word_rgba + (wy * kWordWidth + wx) * 4;
                        blend(c[i], p[0], p[1], p[2], p[3] * f->word >> 8);
                    }
                }
                // Not quite opaque, so the line stays quieter than the
                // name; it fades out with the name.
                if (in_status && status_covers(px, glyph_y, text_x, text_length)) {
                    blend(c[i], 220, 228, 240, 200 * f->word >> 8);
                }
                if (f->flash) blend(c[i], 255, 255, 255, f->flash);
            }
            row[x / 2] = yuv_pair(c[0], c[1]);
        }
    }
}

static void draw(const Frame* f) {
    g_last = *f;
    render_rows(f, 0, g_mode->xfbHeight);
    DCFlushRange(g_xfb, g_mode->fbWidth * g_mode->xfbHeight * 2);
    VIDEO_WaitVSync();
}

// A new status line over the frame on screen: only its rows are drawn again.
static void show_status(const char* text) {
    g_status = text;
    const int top = status_top();
    render_rows(&g_last, top, top + kGlyphHeight);
    DCFlushRange(g_xfb + top * (g_mode->fbWidth / 2), g_mode->fbWidth * kGlyphHeight * 2);
}

static int ease_out(int t, int span) {  // 0..256 over span frames, slowing down
    if (t <= 0) return 0;
    if (t >= span) return 256;
    const int u = 256 - t * 256 / span;
    return 256 - u * u / 256;
}

static void video_init(void) {
    VIDEO_Init();
    g_mode = VIDEO_GetPreferredMode(NULL);
    // A 16:9 TV: the picture across the whole screen, as RiftWii's menu
    // has it (a tester saw black bars at the sides), the art narrowed to
    // keep its shape.
    CONF_Init();
    if (CONF_GetAspectRatio() == CONF_ASPECT_16_9) {
        g_mode->viWidth = VI_MAX_WIDTH_PAL;
        g_squeeze = 192;
    }
    g_xfb = (u32*)MEM_K0_TO_K1(SYS_AllocateFramebuffer(g_mode));
    VIDEO_Configure(g_mode);
    VIDEO_SetNextFramebuffer(g_xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (g_mode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
}

// About a second: the sky comes up, the rift tears open with a flash,
// and "Rift" and "Wii" come out of it to their places.
static void intro(void) {
    for (int t = 0; t <= 62; ++t) {
        Frame f;
        f.sky = ease_out(t, 16);
        f.tear = ease_out(t - 6, 16);
        f.glow = 256;
        f.flash = t >= 20 && t < 32 ? (32 - t) * 15 : 0;
        f.word = ease_out(t - 20, 18);
        f.apart = (256 - ease_out(t - 20, 26)) * 60 >> 8;
        draw(&f);
    }
}

// Everything fades out once RiftWii is read.
static void outro(void) {
    for (int t = 12; t >= 0; t -= 2) {
        Frame f = {t * 256 / 12, 256, t * 256 / 12, 0, t * 256 / 12, 0};
        draw(&f);
    }
}

// POWER and RESET pressed during the start steps or on the error screen.
// The callbacks run in interrupt context and only set a flag; the main
// thread acts on it (check_buttons). Without them a short POWER press did
// nothing while the channel was stuck: libogc's default ignores it (seen in
// Dolphin, where a stop request is that press).
static volatile bool g_power_pressed;
static volatile bool g_reset_pressed;

static void on_power(void) { g_power_pressed = true; }

static void on_reset(u32 irq, void* ctx) {
    (void)irq;
    (void)ctx;
    g_reset_pressed = true;
}

// Turns the Wii off, or goes back to the Wii Menu, if asked to.
static void check_buttons(void) {
    if (g_power_pressed) SYS_ResetSystem(SYS_POWEROFF, 0, 0);
    if (g_reset_pressed) SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
}

// What fail() advises for each kind of failure.
static const char kCopyAdvice[] =
    "  Copy RiftWii to the SD card (apps/riftwii/boot.dol)\n"
    "  from riftwii.zip, then open this channel again.\n";
static const char kHangAdvice[] =
    "  Take the SD card (or USB drive) out, put it back in and open this\n"
    "  channel again. If it stops at the same step, start RiftWii from the\n"
    "  Homebrew Channel and report what this screen says.\n";
static const char kRetryAdvice[] =
    "  Open this channel again. If this happens again, start RiftWii from\n"
    "  the Homebrew Channel and report what this screen says.\n";

// Nothing to start: a message on the console, then back to the Wii Menu
// after 15 seconds, RESET or a GameCube controller button; POWER turns the
// Wii off. (No Wii Remote
// stack: it would double this program's size, which RiftWii carries.)
// It may run while the loader thread is stuck in a call to IOS that never
// returns, which cannot be cancelled: so it touches neither the SD card
// nor the USB drive, only the screen, the controller ports (SI, not IOS)
// and the return to the Wii Menu.
static void fail(const char* why, const char* advice) {
    CON_Init(g_xfb, 20, 20, g_mode->fbWidth, g_mode->xfbHeight, g_mode->fbWidth * VI_DISPLAY_PIX_SZ);
    printf("\x1b[2J\x1b[4;2H");
    printf("RiftWii could not be started.\n\n  %s\n\n", why);
    printf("%s\n", advice);
    printf("  Back to the Wii Menu in 15 seconds.\n");
    printf("  Press RESET for the Wii Menu, POWER to turn the Wii off.\n");
    PAD_Init();
    for (int f = 0; f < 15 * 60; ++f) {
        check_buttons();
        if (SYS_ResetButtonDown()) break;
        if (PAD_ScanPads() && PAD_ButtonsDown(0)) break;
        VIDEO_WaitVSync();
    }
    SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
}

// The steps of finding RiftWii, in order, each with how long it may take
// before the channel gives up on it. A card or drive that works passes
// all of them in well under a second, but reading boot.dol (about 7 MiB)
// from a slow card or drive can take a few seconds.
typedef enum {
    STEP_SD_START,
    STEP_SD_MOUNT,
    STEP_SD_READ,
    STEP_USB_MOUNT,
    STEP_USB_READ,
    STEP_COUNT,
} StepId;

typedef struct {
    const char* status;      // under the name while it runs
    const char* device;      // what did not answer, for fail()
    const char* stopped_at;  // the step, for fail()
    int seconds;
} Step;

static const Step kSteps[STEP_COUNT] = {
    {"Starting the SD card...", "The SD card", "starting the SD card", 10},
    {"Mounting the SD card...", "The SD card", "mounting the SD card", 10},
    {"Reading apps/riftwii/boot.dol...", "The SD card", "reading boot.dol from the SD card", 60},
    // mount_usb's own tries take about 5 seconds when no drive is there
    {"Trying the USB drive...", "The USB drive", "starting the USB drive", 20},
    {"Reading apps/riftwii/boot.dol from the USB drive...", "The USB drive",
     "reading boot.dol from the USB drive", 60},
};

// What the loader thread found, read by the main thread once g_step is
// STEP_COUNT (its last write).
static volatile int g_step = -1;
static volatile bool g_given_up;
static u8* g_dol;
static u32 g_size;
static const char* g_path;

// A thread that never goes on (it sleeps, so the main thread keeps running).
static void stay_here(void) {
    for (;;) usleep(1000000);
}

// Held by the loader thread around the device calls that follow a step's
// long call (the log write, the unmount after a read), and taken by the main
// thread to give up: a call that returns late after the error screen is up
// then goes no further, and fail() never runs beside an unmount.
static mutex_t g_device_lock;

// Before such a call: goes on only if the main thread has not given up.
static void device_begin(void) {
    LWP_MutexLock(g_device_lock);
    if (g_given_up) {
        LWP_MutexUnlock(g_device_lock);
        stay_here();
    }
}

static void device_end(void) { LWP_MutexUnlock(g_device_lock); }

// The main thread gives up. An unmount or log write under way gets up to
// 5 seconds to finish first; one stuck in IOS longer is left as it is,
// which fail() does not touch either.
static void give_up(void) {
    bool locked = false;
    for (int f = 0; f < 5 * 60; ++f) {
        if (LWP_MutexTryLock(g_device_lock) == 0) {
            locked = true;
            break;
        }
        VIDEO_WaitVSync();
    }
    g_given_up = true;
    if (locked) LWP_MutexUnlock(g_device_lock);
}

static void enter_step(StepId step) {
    // Once the main thread has given up, start nothing more on the way
    // back to the Wii Menu.
    if (g_given_up) stay_here();
    g_step = step;
#ifdef CHANNEL_TEST_HANG
    // The test build: what a console that never comes back from this step
    // looks like.
    if (step == CHANNEL_TEST_HANG) stay_here();
#endif
}

static bool mount_usb(void) {
    for (int tries = 0; tries < 20; ++tries) {  // a USB drive can take a few seconds to spin up
        if (fatMountSimple("usb", &__io_usbstorage)) return true;
        usleep(250000);
    }
    return false;
}

static void unmount(int which) {
    fatUnmount(which == 0 ? "sd:" : "usb:");
    if (which == 0) __io_wiisd.shutdown();
    else __io_usbstorage.shutdown();
}

// Into MEM2, taken off the arena directly: the heap would put part of it
// in MEM1, where the sections are copied to.
static void* mem2(u32 n) {
    const u32 lo = ((u32)SYS_GetArena2Lo() + 31) & ~31u;
    if (lo + n > (u32)SYS_GetArena2Hi()) return NULL;
    SYS_SetArena2Lo((void*)(lo + ((n + 31) & ~31u)));
    return (void*)lo;
}

// The last mem2 block, given back.
static void mem2_release(void* data) { SYS_SetArena2Lo(data); }

static void read_dol(int which) {
    u8* dol = dolboot_read(kPaths[which], &g_size, mem2, mem2_release);
    device_begin();
    g_dol = dol;
    if (dol) g_path = kPaths[which];
    unmount(which);
    device_end();
}

// That the channel got this far, in the installer's log, and how long the
// card took: the first line is the one earlier channels wrote, which
// reports are searched for.
static void log_sd_start(u32 started_ms, u32 mounted_ms) {
    FILE* log = fopen("sd:/riftwii/channel.log", "a");
    if (!log) return;
    fprintf(log, "Channel started, IOS%d\n", IOS_GetVersion());
    fprintf(log, "Channel: the SD card started in %u ms and mounted in %u ms.\n",
            (unsigned)started_ms, (unsigned)mounted_ms);
    fclose(log);
}

// The loader thread: SD card first, the USB drive when the card has no
// RiftWii. The SD card's start and mount are separate steps, so a console
// that stops in one shows which (__io_wiisd.startup() does nothing the
// second time, inside fatMountSimple).
static void* load_thread(void* unused) {
    (void)unused;
    const u64 sd_begin = gettime();
    enter_step(STEP_SD_START);
    bool sd = __io_wiisd.startup();
    const u32 started_ms = diff_msec(sd_begin, gettime());
    if (sd) {
        enter_step(STEP_SD_MOUNT);
        sd = fatMountSimple("sd", &__io_wiisd);
    }
    if (sd) {
        const u32 mounted_ms = diff_msec(sd_begin, gettime()) - started_ms;
        enter_step(STEP_SD_READ);
        device_begin();
        log_sd_start(started_ms, mounted_ms);
        device_end();
        read_dol(0);
    }
    if (!g_dol) {
        enter_step(STEP_USB_MOUNT);
        if (mount_usb()) {
            enter_step(STEP_USB_READ);
            read_dol(1);
        }
    }
    g_step = STEP_COUNT;
    return NULL;
}

// The loader thread's stack: static, as libogc's thread workspace is small.
static u8 g_loader_stack[32 * 1024] ATTRIBUTE_ALIGN(32);

// Runs the loader thread, showing each step it is on, until it is done.
// A step past its time ends in fail(): the thread is stuck in IOS and
// cannot be stopped, but the console can still go back to the Wii Menu.
static void load(void) {
    // Below the main thread (64), so each frame's check gets the CPU back
    // from it even while it computes.
    lwp_t loader;
    LWP_MutexInit(&g_device_lock, false);
    // Without the thread no step would ever start, and the intro's last
    // frame would stay up for good.
    if (LWP_CreateThread(&loader, load_thread, NULL, g_loader_stack, sizeof(g_loader_stack), 48) != 0) {
        fail("The channel could not start its loader thread.", kRetryAdvice);
    }
    int shown = -1;
    u64 shown_since = 0;
    for (;;) {
        check_buttons();
        const int step = g_step;
        if (step == STEP_COUNT) break;
        if (step >= 0 && step != shown) {
            shown = step;
            shown_since = gettime();
            show_status(kSteps[step].status);
        }
        if (shown >= 0 && diff_msec(shown_since, gettime()) >= kSteps[shown].seconds * 1000u) {
            give_up();
            char why[128];
            snprintf(why, sizeof(why), "%s did not answer (stopped at: %s).", kSteps[shown].device,
                     kSteps[shown].stopped_at);
            fail(why, kHangAdvice);
        }
        VIDEO_WaitVSync();
    }
    LWP_JoinThread(loader, NULL);
}

int main(void) {
    video_init();
    SYS_SetPowerCallback(on_power);
    SYS_SetResetCallback(on_reset);
    intro();
    load();

    if (!g_dol) fail("apps/riftwii/boot.dol is not on the SD card or the USB drive.", kCopyAdvice);
    if (!dolboot_valid(g_dol, g_size)) fail("apps/riftwii/boot.dol is damaged or is not a Wii program.", kCopyAdvice);

    show_status("Starting RiftWii...");
    outro();
    VIDEO_SetBlack(TRUE);
    VIDEO_Flush();
    VIDEO_WaitVSync();

    // Keeps the IOS this channel runs under (58, from its TMD), as the
    // Homebrew Channel does.
    dolboot_run(g_dol, g_path);
    return 0;
}
