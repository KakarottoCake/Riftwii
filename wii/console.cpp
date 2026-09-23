// SPDX-License-Identifier: GPL-3.0-or-later
#include "console.hpp"

#include <gccore.h>
#include <ogc/console.h>
#include <ogc/pad.h>
#include <ogc/system.h>
#include <ogc/video.h>
#include <wiiuse/wpad.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "log.hpp"

namespace riftwii::wii {

void ConsoleStart(bool video_initialised) {
    if (!video_initialised) VIDEO_Init();
    GXRModeObj* rmode = VIDEO_GetPreferredMode(nullptr);
    void* xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
    CON_EnableGecko(1, false);
    LogEchoToScreen(true);
    std::printf("\x1b[2;0H");
}

void ConsoleStartInFrame(void* xfb, int fb_width, int fb_height, int x, int y, int width, int height) {
    // CON_Init may blank more than its window: keep a copy of the frame and
    // put back everything around the window afterwards.
    const std::size_t stride = static_cast<std::size_t>(fb_width) * VI_DISPLAY_PIX_SZ;
    const std::size_t bytes = stride * static_cast<std::size_t>(fb_height);
    u8* keep = static_cast<u8*>(std::malloc(bytes));
    if (keep) std::memcpy(keep, xfb, bytes);
    // libogc takes the window's right and bottom edges, not its size.
    CON_Init(xfb, x, y, x + width, y + height, static_cast<int>(stride));
    if (keep) {
        u8* out = static_cast<u8*>(xfb);
        const std::size_t left = static_cast<std::size_t>(x) * VI_DISPLAY_PIX_SZ;
        const std::size_t right = static_cast<std::size_t>(x + width) * VI_DISPLAY_PIX_SZ;
        for (int row = 0; row < fb_height; ++row) {
            u8* dst = out + stride * row;
            const u8* src = keep + stride * row;
            if (row < y || row >= y + height) {
                std::memcpy(dst, src, stride);
            } else {
                std::memcpy(dst, src, left);
                if (right < stride) std::memcpy(dst + right, src + right, stride - right);
            }
        }
        std::free(keep);
    }
    CON_EnableGecko(1, false);
    LogEchoToScreen(true);
    // Truecolour: the menu's ink on the card's white.
    std::printf("\x1b[38;2;46;46;54m\x1b[48;2;255;255;255m\x1b[2J\x1b[0;0H");
}

void WaitForExit() {
    PAD_Init();
    WPAD_Init();
    for (;;) {
        VIDEO_WaitVSync();
        if (SYS_ResetButtonDown()) break;
        PAD_ScanPads();
        WPAD_ScanPads();
        bool pressed = false;
        for (int i = 0; i < 4; ++i) {
            if (PAD_ButtonsDown(i) & PAD_BUTTON_START) pressed = true;
            if (WPAD_ButtonsDown(i) & (WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME)) pressed = true;
        }
        if (pressed) break;
    }
    WPAD_Shutdown();
}

}  // namespace riftwii::wii
