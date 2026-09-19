// SPDX-License-Identifier: GPL-3.0-or-later
#include "console.hpp"

#include <gccore.h>
#include <ogc/console.h>
#include <ogc/pad.h>
#include <ogc/system.h>
#include <ogc/video.h>
#include <wiiuse/wpad.h>

#include <cstdio>

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
    std::printf("\x1b[2;0H");
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
