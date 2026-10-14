// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Wii Remotes' command queues, in MEM2 (linked in place of libogc's
// wiiuse_init_cmd_queue with -Wl,--wrap, Makefile.wii).
//
// libogc 3.1.0 gives each of its six Bluetooth slots (four Wii Remotes, the
// Balance Board, one more) a queue of 256 commands, 18 KB, taken from its
// kernel heap: 64 KB in all, shared with its threads. Only the first three
// fit; slots 3-5 kept an empty queue (first = NULL), and the first command
// to a device there (its LEDs, as it connects) read address 0. A Balance
// Board still on from Wii Fit Plus or We Ski reconnects as RiftWii starts:
// a tester's crash on every return from those games (PC in __lwp_queue_get,
// DAR 0, from wiiuse_handshake). A fourth Wii Remote would do the same.
// Reproduced in Dolphin with its emulated Balance Board: slots 3-5 zero.
#include <ogc/lwp_queue.h>
#include <wiiuse/wiiuse.h>

#include <cstdlib>

#include "skin.hpp"

namespace {

constexpr unsigned kSlots = 6;          // WPAD_MAX_DEVICES in libogc 3.1.0
constexpr unsigned kCommands = 256;     // libogc's own queue length
u8* g_buffers[kSlots] = {};             // once per slot: kept across WPAD_Shutdown/WPAD_Init

}  // namespace

extern "C" int __real_wiiuse_init_cmd_queue(struct wiimote_t* wm);

extern "C" int __wrap_wiiuse_init_cmd_queue(struct wiimote_t* wm) {
    const int slot = wm->unid;
    if (slot < 0 || slot >= static_cast<int>(kSlots)) return __real_wiiuse_init_cmd_queue(wm);
    constexpr std::size_t bytes = std::size_t(kCommands) * sizeof(struct cmd_blk_t);
    if (g_buffers[slot] == nullptr) {
        g_buffers[slot] = riftwii::wii::skin::Mem2Alloc(bytes);
        if (g_buffers[slot] == nullptr) g_buffers[slot] = static_cast<u8*>(std::malloc(bytes));
        if (g_buffers[slot] == nullptr) return __real_wiiuse_init_cmd_queue(wm);
    }
    __lwp_queue_initialize(const_cast<lwp_queue*>(&wm->cmdq), g_buffers[slot], kCommands, sizeof(struct cmd_blk_t));
    return 0;
}
