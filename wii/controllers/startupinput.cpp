// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "startupinput.hpp"

#include <atomic>
#include <gccore.h>
#include <unistd.h>
#include <wiiuse/wpad.h>

#include "log.hpp"
#include "wiidrc.h"

namespace riftwii::wii {
namespace {

alignas(32) unsigned char g_stack[8192];
lwp_t g_thread = LWP_THREAD_NULL;
bool g_started = false;
std::atomic<bool> g_running{false};
std::atomic<bool> g_cancelled{false};

void SampleStartupB() {
    PAD_ScanPads();
    WPAD_ScanPads();
    for (int channel = 0; channel < 4; ++channel) {
        const u32 remote = WPAD_ButtonsDown(channel) | WPAD_ButtonsHeld(channel);
        const u16 pad = PAD_ButtonsDown(channel) | PAD_ButtonsHeld(channel);
        if ((remote & (WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B)) || (pad & PAD_BUTTON_B))
            g_cancelled = true;
    }
    if (WiiDRC_Inited() && WiiDRC_Connected()) {
        WiiDRC_ScanPads();
        if ((WiiDRC_ButtonsDown() | WiiDRC_ButtonsHeld()) & WIIDRC_BUTTON_B) g_cancelled = true;
    }
}

void* SampleStartup(void*) {
    while (g_running) {
        SampleStartupB();
        usleep(10000);
    }
    return nullptr;
}

}  // namespace

void BeginStartupInput() {
    g_started = true;
    g_cancelled = false;
    // Catch an already-held controller before font and sound loading starts.
    SampleStartupB();
    g_running = true;
    if (LWP_CreateThread(&g_thread, SampleStartup, nullptr, g_stack, sizeof(g_stack), 70) < 0) {
        g_thread = LWP_THREAD_NULL;
        g_running = false;
        // Keep the first sample, and sample again before the menu opens.
        logf("Startup input: the button sampler could not start; B is checked before and after loading.\n");
    }
}

bool FinishStartupInput() {
    if (!g_started) return false;
    g_started = false;
    g_running = false;
    if (g_thread != LWP_THREAD_NULL) {
        LWP_JoinThread(g_thread, nullptr);
        g_thread = LWP_THREAD_NULL;
    }
    SampleStartupB();
    return g_cancelled;
}

}  // namespace riftwii::wii
