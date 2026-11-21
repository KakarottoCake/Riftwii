// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gcrealcheck.hpp"

#include <gccore.h>
#include <ogc/card.h>
#include <ogc/lwp_watchdog.h>
#include <unistd.h>

#include <atomic>
#include <utility>

#include "gcrealcard.hpp"
#include "log.hpp"

namespace riftwii::wii {
namespace {

// One check at a time. libogc allocates the thread's stack and frees it
// when the thread is joined: a fixed one would take its room from MEM1 for
// the whole session, and the menu runs out of MEM1 without those 32 KiB
// (Dolphin: Play on a GameCube game page, 2611-211). A check that never
// returns keeps its stack, and nothing is freed under it.
enum class CheckState { Idle, Running, Done };
// Written by the check thread, read by the menu thread: atomic. g_result is
// read only after the thread is joined.
std::atomic<CheckState> g_state{CheckState::Idle};
lwp_t g_thread = LWP_THREAD_NULL;
constexpr u32 kStackBytes = 16 * 1024;
int g_slot = 0;
GcRealCardCheck g_result;

void* CheckMain(void*) {
    GcRealCardCheck result;
    {
        GcRealCard card;
        result.result = card.Open(g_slot);
        if (result.result >= 0) {
            result.bytes = card.Bytes();
            logf("Memory card: reading the system area of the card in slot %c\n", 'A' + g_slot);
            result.result = card.ReadSystem(result.system);
        }
    }
    g_result = std::move(result);
    g_state = CheckState::Done;
    return nullptr;
}

}  // namespace

GcRealCardCheck CheckGcRealCard(int slot, unsigned timeout_ms, bool& timed_out, void (*waiting)(void*),
                                void* context) {
    timed_out = false;
    GcRealCardCheck result;
    const char slot_name = static_cast<char>('A' + slot);
    if (g_state == CheckState::Running) {
        logf("Memory card: slot %c was not checked, as an earlier check of slot %c still has not finished.\n",
             slot_name, 'A' + g_slot);
        result.result = CARD_ERROR_BUSY;
        return result;
    }
    if (g_state == CheckState::Done) {
        LWP_JoinThread(g_thread, nullptr);
        g_state = CheckState::Idle;
    }
    g_slot = slot;
    g_result = GcRealCardCheck();
    g_state = CheckState::Running;
    logf("Memory card: checking the card in slot %c before the game starts\n", slot_name);
    if (LWP_CreateThread(&g_thread, CheckMain, nullptr, nullptr, kStackBytes, 60) < 0) {
        // No thread: no check either. Checking on the menu's own thread
        // would bring back the freeze the time limit is there for.
        g_state = CheckState::Idle;
        logf("Memory card: slot %c was not checked, as no thread could be made for the check (out of memory?).\n",
             slot_name);
        result.result = CARD_ERROR_INSSPACE;
        return result;
    }
    const u64 started = gettime();
    while (g_state == CheckState::Running) {
        const u32 waited = ticks_to_millisecs(gettime() - started);
        if (waited >= timeout_ms) {
            timed_out = true;
            logf("Memory card: the card in slot %c did not answer within %u seconds, so RiftWii stopped waiting for it (libogc's probe, mount or read never returned). While that check still waits, the slot is not read again.\n",
                 slot_name, timeout_ms / 1000);
            result.result = CARD_ERROR_NOCARD;
            return result;
        }
        if (waited >= 1000 && waiting) waiting(context);
        usleep(100 * 1000);
    }
    LWP_JoinThread(g_thread, nullptr);
    g_state = CheckState::Idle;
    result = std::move(g_result);
    g_result = GcRealCardCheck();
    return result;
}

}  // namespace riftwii::wii
