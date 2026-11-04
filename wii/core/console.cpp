// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "console.hpp"

#include <gccore.h>
#include <ogc/console.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/pad.h>
#include <ogc/system.h>
#include <ogc/video.h>
#include <wiiuse/wpad.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gcadapter.hpp"
#include "gecko.hpp"
#include "log.hpp"

namespace riftwii::wii {
namespace {

// The GameCube adapter's ports (and mapped USB pads) for a waiting
// screen, read as the Wii's own GameCube ports are: a Wii U has no
// GameCube ports and no RESET button, so with only an adapter the screen
// could not be answered. Started for the screen when the menu's rule
// lets this IOS reach the adapter, stopped on the way out.
// Its lines (the driver's own too, as it finds the adapter and its ports
// while polled) go to the session log only, not under the screen's own
// text: the echo stays off while the screen waits, back on the way out.
// One plugged in after the screen came up is looked for every 2 s, as the
// menu does. Each port counts presses only once it has been seen with
// nothing held, so a button already held (the A that chose Play, a pad
// plugged in with A down) counts only once let go and pressed again, and
// a pad left with a button held doesn't stop the others answering.
class AdapterPads {
public:
    explicit AdapterPads(bool wanted) : echo_(LogEchoesToScreen()), wanted_(wanted) {
        LogEchoToScreen(false);
        started_ = wanted && GcAdapterScreenStart();
        tried_ = gettime();
    }
    ~AdapterPads() {
        if (started_) GcAdapterStop();
        LogEchoToScreen(echo_);
    }
    AdapterPads(const AdapterPads&) = delete;
    AdapterPads& operator=(const AdapterPads&) = delete;

    // PAD_BUTTON_* newly pressed on any port since the last call.
    u32 Down() {
        if (!started_) {
            if (!wanted_ || ticks_to_millisecs(gettime() - tried_) < 2000) return 0;
            tried_ = gettime();
            started_ = GcAdapterScreenStart(false);
            if (!started_) return 0;
        }
        GcAdapterView view;
        GcAdapterPoll(view);
        u32 down = 0;
        for (unsigned p = 0; p < GCAD_PORTS; ++p) {
            const u16 held = view.present[p] ? view.pads[p].buttons : 0;
            if (!view.present[p]) armed_[p] = false;
            else if (held == 0) armed_[p] = true;
            if (armed_[p]) down |= held & ~held_[p];
            held_[p] = held;
        }
        return down;
    }

private:
    bool echo_;
    bool wanted_;
    bool started_ = false;
    u64 tried_ = 0;
    u16 held_[GCAD_PORTS] = {};
    bool armed_[GCAD_PORTS] = {};
};

}  // namespace

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
    // A USB Gecko in slot B gets the console's text. With the USB Gecko log
    // on, it keeps its copy of stdout instead (logf sends its own lines).
    if (GeckoActive()) GeckoRetakeStdout();
    else CON_EnableGecko(1, false);
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
    // A USB Gecko in slot B gets the console's text. With the USB Gecko log
    // on, it keeps its copy of stdout instead (logf sends its own lines).
    if (GeckoActive()) GeckoRetakeStdout();
    else CON_EnableGecko(1, false);
    LogEchoToScreen(true);
    // Truecolour: the menu's ink on the card's white.
    std::printf("\x1b[38;2;46;46;54m\x1b[48;2;255;255;255m\x1b[2J\x1b[0;0H");
}

void WaitForExit() {
    PAD_Init();
    WPAD_Init();
    AdapterPads adapter(true);
    for (;;) {
        VIDEO_WaitVSync();
        if (SYS_ResetButtonDown()) break;
        PAD_ScanPads();
        WPAD_ScanPads();
        bool pressed = (adapter.Down() & PAD_BUTTON_START) != 0;
        for (int i = 0; i < 4; ++i) {
            if (PAD_ButtonsDown(i) & PAD_BUTTON_START) pressed = true;
            if (WPAD_ButtonsDown(i) & (WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME)) pressed = true;
        }
        if (pressed) break;
    }
    WPAD_Shutdown();
}

ExitChoice WaitForChoice(int timeout_seconds, bool adapter_wanted) {
    PAD_Init();
    WPAD_Init();
    AdapterPads adapter(adapter_wanted);
    const u64 start = gettime();
    ExitChoice choice = ExitChoice::Restart;
    for (;;) {
        VIDEO_WaitVSync();
        if (SYS_ResetButtonDown()) break;
        if (timeout_seconds > 0 && ticks_to_secs(gettime() - start) >= static_cast<u64>(timeout_seconds)) break;
        PAD_ScanPads();
        WPAD_ScanPads();
        const u32 adapter_down = adapter.Down();
        bool restart = (adapter_down & PAD_BUTTON_A) != 0, leave = (adapter_down & PAD_BUTTON_START) != 0;
        for (int i = 0; i < 4; ++i) {
            const u32 gc = PAD_ButtonsDown(i);
            const u32 wii = WPAD_ButtonsDown(i);
            if ((gc & PAD_BUTTON_A) || (wii & (WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A))) restart = true;
            if ((gc & PAD_BUTTON_START) || (wii & (WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME))) leave = true;
        }
        if (leave) {
            choice = ExitChoice::Leave;
            break;
        }
        if (restart) break;
    }
    WPAD_Shutdown();
    return choice;
}

}  // namespace riftwii::wii
