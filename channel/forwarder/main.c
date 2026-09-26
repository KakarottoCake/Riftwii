// SPDX-License-Identifier: GPL-3.0-or-later
//
// The RiftWii channel: a moment of the RiftWii start screen (the rift
// tears open and the name comes out of it), then RiftWii itself from the
// SD card (or the USB drive). The channel holds
// only this program, never RiftWii: RiftWii's own updates replace
// apps/riftwii/boot.dol, and the channel starts whatever is there.
//
// The DOL is read whole into MEM2, then copied into place by the code
// below. This program is linked high in MEM1 (see Makefile.forwarder), clear
// of RiftWii (0x80a00000 up) and of ordinary homebrew (0x80004000 up).

#include <gccore.h>
#include <fat.h>
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

static void draw(const Frame* f) {
    const int w = g_mode->fbWidth, h = g_mode->xfbHeight;
    const int word_x = (w - kWordWidth) / 2, word_y = (h - kWordHeight) / 2 - 6;
    const int rift_x = word_x + kWordSeam - kRiftWidth / 2;
    const int cy = h / 2 - 6;
    const int half = kRiftHeight * f->tear / 512;  // the rift's half height now
    for (int y = 0; y < h; ++y) {
        u32* row = g_xfb + y * (w / 2);
        const u8* bg_row = splash_bg_rgb + (y * kBgHeight / h) * kBgWidth * 3;
        const int wy = y - word_y;
        const bool in_word = f->word > 0 && wy >= 0 && wy < kWordHeight;
        const bool in_rift = half > 0 && y >= cy - half && y < cy + half;
        const int ry = in_rift ? kRiftHeight / 2 + (y - cy) * (kRiftHeight / 2) / half : 0;
        for (int x = 0; x < w; x += 2) {
            int c[2][3];
            for (int i = 0; i < 2; ++i) {
                const int px = x + i;
                const u8* bg = bg_row + (px * kBgWidth / w) * 3;
                c[i][0] = bg[0] * f->sky >> 8;
                c[i][1] = bg[1] * f->sky >> 8;
                c[i][2] = bg[2] * f->sky >> 8;
                const int rx = px - rift_x;
                if (in_rift && rx >= 0 && rx < kRiftWidth) {
                    const int a = splash_rift_a[ry * kRiftWidth + rx] * f->glow >> 8;
                    blend(c[i], 205, 238, 255, a);
                }
                if (in_word) {
                    // "Rift" is drawn nearer the rift by `apart`, "Wii" too
                    int wx = px - word_x;
                    wx += wx < kWordSeam ? -f->apart : f->apart;
                    const int side = px - word_x < kWordSeam;
                    if (wx >= 0 && wx < kWordWidth && (wx < kWordSeam) == side) {
                        const u8* p = splash_word_rgba + (wy * kWordWidth + wx) * 4;
                        blend(c[i], p[0], p[1], p[2], p[3] * f->word >> 8);
                    }
                }
                if (f->flash) blend(c[i], 255, 255, 255, f->flash);
            }
            row[x / 2] = yuv_pair(c[0], c[1]);
        }
    }
    DCFlushRange(g_xfb, w * h * 2);
    VIDEO_WaitVSync();
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

// Nothing to start: a message on the console, then back to the Wii Menu
// after 15 seconds, RESET or a GameCube controller button. (No Wii Remote
// stack: it would double this program's size, which RiftWii carries.)
static void fail(const char* why) {
    CON_Init(g_xfb, 20, 20, g_mode->fbWidth, g_mode->xfbHeight, g_mode->fbWidth * VI_DISPLAY_PIX_SZ);
    printf("\x1b[2J\x1b[4;2H");
    printf("RiftWii could not be started.\n\n  %s\n\n", why);
    printf("  Copy RiftWii to the SD card (apps/riftwii/boot.dol)\n");
    printf("  from riftwii.zip, then open this channel again.\n\n");
    printf("  Back to the Wii Menu in 15 seconds (or press RESET).\n");
    PAD_Init();
    for (int f = 0; f < 15 * 60; ++f) {
        if (SYS_ResetButtonDown()) break;
        if (PAD_ScanPads() && PAD_ButtonsDown(0)) break;
        VIDEO_WaitVSync();
    }
    SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
}

static bool mount(int which) {
    if (which == 0) return fatMountSimple("sd", &__io_wiisd);
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

int main(void) {
    video_init();
    intro();

    u8* dol = NULL;
    u32 size = 0;
    const char* path = NULL;
    for (int i = 0; i < 2 && !dol; ++i) {
        if (!mount(i)) continue;
        dol = dolboot_read(kPaths[i], &size, mem2);
        if (dol) path = kPaths[i];
        unmount(i);
    }
    if (!dol) fail("apps/riftwii/boot.dol is not on the SD card or the USB drive.");
    if (!dolboot_valid(dol, size)) fail("apps/riftwii/boot.dol is damaged or is not a Wii program.");

    outro();
    VIDEO_SetBlack(TRUE);
    VIDEO_Flush();
    VIDEO_WaitVSync();

    // Keeps the IOS this channel runs under (58, from its TMD), as the
    // Homebrew Channel does.
    dolboot_run(dol, path);
    return 0;
}
