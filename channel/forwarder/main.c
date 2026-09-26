// SPDX-License-Identifier: GPL-3.0-or-later
//
// The RiftWii channel: shows the RiftWii logo for a moment, then starts
// RiftWii itself from the SD card (or the USB drive). The channel holds
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

#include "logo_rgba.h"  // RGBA, from hbc/icon.png (tools/make_channel.py logo)

#define kLogoWidth 128
#define kLogoHeight 48
#define kLogo logo_rgba

#define DOL_MAX (16u << 20)

static const char* const kPaths[] = {
    "sd:/apps/riftwii/boot.dol",
    "usb:/apps/riftwii/boot.dol",
};

static GXRModeObj* g_mode;
static u32* g_xfb;

// The screen in the Wii's own pixel format: two pixels share one Cb/Cr.
static u32 yuv_pair(int r0, int g0, int b0, int r1, int g1, int b1) {
    const int y0 = (299 * r0 + 587 * g0 + 114 * b0) / 1000;
    const int y1 = (299 * r1 + 587 * g1 + 114 * b1) / 1000;
    const int r = (r0 + r1) / 2, g = (g0 + g1) / 2, b = (b0 + b1) / 2;
    int cb = 128 + (-169 * r - 331 * g + 500 * b) / 1000;
    int cr = 128 + (500 * r - 419 * g - 81 * b) / 1000;
    if (cb < 0) cb = 0;
    if (cb > 255) cb = 255;
    if (cr < 0) cr = 0;
    if (cr > 255) cr = 255;
    return ((u32)y0 << 24) | ((u32)cb << 16) | ((u32)y1 << 8) | (u32)cr;
}

// The background: RiftWii's dark blue, lighter toward the middle.
static void background(int x, int y, int* r, int* g, int* b) {
    const int w = g_mode->fbWidth, h = g_mode->xfbHeight;
    const int dx = x - w / 2, dy = (y - h / 2) * 2;
    const int d = (dx * dx + dy * dy) / (w * 4);
    const int k = d > 64 ? 64 : d;
    *r = 22 - k / 4;
    *g = 34 - k / 3;
    *b = 58 - k / 2;
}

// One frame: the logo, twice its size, at `alpha` (0..256) over the
// background, lifted by `rise` lines.
static void draw(int alpha, int rise) {
    const int w = g_mode->fbWidth, h = g_mode->xfbHeight;
    const int scale = 2;
    const int lw = kLogoWidth * scale, lh = kLogoHeight * scale;
    const int left = (w - lw) / 2 & ~1, top = (h - lh) / 2 - rise;
    for (int y = 0; y < h; ++y) {
        u32* row = g_xfb + y * (w / 2);
        for (int x = 0; x < w; x += 2) {
            int c[2][3];
            for (int i = 0; i < 2; ++i) {
                background(x + i, y, &c[i][0], &c[i][1], &c[i][2]);
                const int lx = (x + i - left) / scale, ly = (y - top) / scale;
                if (x + i >= left && y >= top && lx < kLogoWidth && ly < kLogoHeight) {
                    const u8* p = kLogo + (ly * kLogoWidth + lx) * 4;
                    const int a = p[3] * alpha / 256;
                    for (int k = 0; k < 3; ++k) c[i][k] = (p[k] * a + c[i][k] * (255 - a)) / 255;
                }
            }
            row[x / 2] = yuv_pair(c[0][0], c[0][1], c[0][2], c[1][0], c[1][1], c[1][2]);
        }
    }
    DCFlushRange(g_xfb, w * h * 2);
    VIDEO_WaitVSync();
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

// The logo fades in and drifts up a little: about half a second.
static void intro(void) {
    for (int f = 0; f <= 30; ++f) {
        const int t = f * 256 / 30;
        const int eased = 256 - (256 - t) * (256 - t) / 256;
        draw(eased, eased * 8 / 256);
    }
}

static void outro(void) {
    for (int f = 30; f >= 0; f -= 3) draw(f * 256 / 30, 8);
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
static u8* read_dol(const char* path, u32* size) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8* data = NULL;
    if (n > 0x100 && (u32)n <= DOL_MAX) {
        const u32 lo = ((u32)SYS_GetArena2Lo() + 31) & ~31u;
        if (lo + (u32)n <= (u32)SYS_GetArena2Hi()) {
            SYS_SetArena2Lo((void*)(lo + (((u32)n + 31) & ~31u)));
            data = (u8*)lo;
        }
    }
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) data = NULL;
    fclose(f);
    *size = (u32)n;
    return data;
}

typedef struct {
    u32 offset[18];
    u32 address[18];
    u32 size[18];
    u32 bss_address;
    u32 bss_size;
    u32 entry;
} DolHeader;

// The DOL's sections must fit its file and land in MEM1, clear of this
// program.
static bool dol_ok(const u8* data, u32 size) {
    extern u8 __app_start[], __bss_end[];
    const u32 self_lo = (u32)__app_start, self_hi = (u32)__bss_end;
    const DolHeader* h = (const DolHeader*)data;
    for (int i = 0; i < 18; ++i) {
        if (h->size[i] == 0) continue;
        const u32 lo = h->address[i], hi = lo + h->size[i];
        if (h->offset[i] + h->size[i] > size || lo < 0x80003f00 || hi > 0x81200000) return false;
        if (lo < self_hi && hi > self_lo) return false;
    }
    const u32 bss_hi = h->bss_address + h->bss_size;
    if (h->bss_size && (h->bss_address < 0x80003f00 || bss_hi > 0x81200000 ||
                        (h->bss_address < self_hi && bss_hi > self_lo))) {
        return false;
    }
    return h->entry >= 0x80003f00 && h->entry < 0x81200000;
}

// Hands `path` over as argv[0], as the Homebrew Channel does: in the
// program's "_arg" block after its entry branch, with the string placed
// after its BSS, where libogc keeps its arena clear of it.
static void set_argv(const DolHeader* h, const char* path) {
    u32 text0 = h->address[0];
    struct __argv* args = (struct __argv*)(text0 + 8);
    if (*(u32*)(text0 + 4) != ARGV_MAGIC) return;
    u32 end = 0;
    for (int i = 0; i < 18; ++i) {
        if (h->size[i] && h->address[i] + h->size[i] > end) end = h->address[i] + h->size[i];
    }
    if (h->bss_address + h->bss_size > end) end = h->bss_address + h->bss_size;
    char* line = (char*)((end + 31) & ~31u);
    const u32 len = strlen(path) + 1;
    memcpy(line, path, len);
    line[len] = 0;
    DCFlushRange(line, (len + 32) & ~31u);
    args->argvMagic = ARGV_MAGIC;
    args->commandLine = line;
    args->length = (int)len + 1;
    DCFlushRange(args, sizeof(*args));
}

int main(void) {
    video_init();
    intro();

    u8* dol = NULL;
    u32 size = 0;
    const char* path = NULL;
    for (int i = 0; i < 2 && !dol; ++i) {
        if (!mount(i)) continue;
        dol = read_dol(kPaths[i], &size);
        if (dol) path = kPaths[i];
        unmount(i);
    }
    if (!dol) fail("apps/riftwii/boot.dol is not on the SD card or the USB drive.");
    if (!dol_ok(dol, size)) fail("apps/riftwii/boot.dol is damaged or is not a Wii program.");

    outro();
    VIDEO_SetBlack(TRUE);
    VIDEO_Flush();
    VIDEO_WaitVSync();

    // Keeps the IOS this channel runs under (58, from its TMD), as the
    // Homebrew Channel does.
    const DolHeader h = *(const DolHeader*)dol;
    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
    IRQ_Disable();

    for (int i = 0; i < 18; ++i) {
        if (h.size[i] == 0) continue;
        memmove((void*)h.address[i], dol + h.offset[i], h.size[i]);
        DCFlushRange((void*)h.address[i], h.size[i]);
        ICInvalidateRange((void*)h.address[i], h.size[i]);
    }
    if (h.bss_size) {
        memset((void*)h.bss_address, 0, h.bss_size);
        DCFlushRange((void*)h.bss_address, h.bss_size);
    }
    set_argv(&h, path);
    ((void (*)(void))h.entry)();
    return 0;
}
