// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2026 quatric (libwiiinput) <https://github.com/quatric/libwiiinput>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "usbkeyboard.hpp"

#include <gccore.h>
#include <ogc/lwp.h>
#include <ogc/mutex.h>
#include <unistd.h>
#include <wiikeyboard/usbkeyboard.h>

#include <cstring>

namespace riftwii::wii {
namespace {

constexpr int kKeyBytes = 256 / 8;
constexpr int kQueueLength = 64;
constexpr u8 kCapsLock = 0x39;

bool BitGet(const u8* bits, u8 k) { return (bits[k >> 3] & (1 << (k & 7))) != 0; }
void BitSet(u8* bits, u8 k) { bits[k >> 3] |= static_cast<u8>(1 << (k & 7)); }
void BitClear(u8* bits, u8 k) { bits[k >> 3] &= static_cast<u8>(~(1 << (k & 7))); }

// Written by the scan thread (through the event callback) under g_lock.
u8 g_live[kKeyBytes];
u8 g_pressed_acc[kKeyBytes];
KeyPress g_queue[kQueueLength];
int g_queue_head = 0, g_queue_tail = 0;
bool g_caps = false;
volatile bool g_lost = false;  // the keyboard went: its keys are let go by the thread

// The frame's view, made by UsbKeyboardPoll.
u8 g_held[kKeyBytes];
u8 g_pressed[kKeyBytes];

mutex_t g_lock = LWP_MUTEX_NULL;
lwp_t g_thread = LWP_THREAD_NULL;
volatile bool g_should_run = false;
volatile bool g_exited = true;
bool g_running = false;
bool g_stuck = false;  // a stopped thread is suspended in its read until the next IOS reload

const char kPlain[] = "abcdefghijklmnopqrstuvwxyz1234567890\n\x1b\b\t -=[]\\#;'`,./";
const char kShifted[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\n\x1b\b\t _+{}|~:\"~<>?";

u8 LiveMods() {
    u8 mods = 0;
    if (BitGet(g_live, 0xE1) || BitGet(g_live, 0xE5)) mods |= kModShift;
    if (BitGet(g_live, 0xE0) || BitGet(g_live, 0xE4)) mods |= kModCtrl;
    if (BitGet(g_live, 0xE2) || BitGet(g_live, 0xE6)) mods |= kModAlt;
    if (BitGet(g_live, 0xE3) || BitGet(g_live, 0xE7)) mods |= kModGui;
    return mods;
}

// The US layout, as libwiiinput reads it; the keypad types its digits and
// operators whatever Num Lock says.
char KeyChar(u8 code, bool shift) {
    char c = 0;
    if (code >= 0x04 && code <= 0x38) {
        c = (shift ? kShifted : kPlain)[code - 0x04];
        const bool letter = code <= 0x1D;
        if (letter && g_caps) c = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : static_cast<char>(c + 32);
    } else if (code >= 0x59 && code <= 0x61) {
        c = static_cast<char>('1' + (code - 0x59));
    } else {
        switch (code) {
            case 0x54: c = '/'; break;
            case 0x55: c = '*'; break;
            case 0x56: c = '-'; break;
            case 0x57: c = '+'; break;
            case 0x62: c = '0'; break;
            case 0x63: c = '.'; break;
            default: break;
        }
    }
    return (c >= 0x20 && c < 0x7F) ? c : 0;
}

// On the scan thread, inside USBKeyboard_Scan, with g_lock held.
void OnKeyLocked(const USBKeyboard_event& event, bool& caps_changed) {
    const u8 k = event.keyCode;
    if (event.type == USBKEYBOARD_PRESSED) {
        const bool fresh = !BitGet(g_live, k);
        BitSet(g_live, k);
        if (!fresh) return;
        BitSet(g_pressed_acc, k);
        if (k == kCapsLock) {
            g_caps = !g_caps;
            caps_changed = true;
            return;
        }
        if (k >= 0xE0 && k <= 0xE7) return;  // a modifier alone
        const int next = (g_queue_head + 1) % kQueueLength;
        if (next == g_queue_tail) return;  // full: the oldest are kept
        KeyPress& press = g_queue[g_queue_head];
        press.code = k;
        press.mods = LiveMods();
        press.ch = (press.mods & kModCtrl) ? 0 : KeyChar(k, (press.mods & kModShift) != 0);
        g_queue_head = next;
    } else if (event.type == USBKEYBOARD_RELEASED) {
        BitClear(g_live, k);
    }
}

void OnKey(USBKeyboard_event event) {
    // The disconnect comes from an IPC callback, where no mutex may be
    // taken: the scan thread lets the keys go.
    if (event.type == USBKEYBOARD_DISCONNECTED) {
        g_lost = true;
        return;
    }
    bool caps_changed = false;
    LWP_MutexLock(g_lock);
    OnKeyLocked(event, caps_changed);
    LWP_MutexUnlock(g_lock);
    if (caps_changed) USBKeyboard_SetLed(USBKEYBOARD_LEDCAPS, g_caps);
}

void* ScanThread(void*) {
    while (g_should_run) {
        if (g_lost) {
            g_lost = false;
            LWP_MutexLock(g_lock);
            std::memset(g_live, 0, sizeof(g_live));
            LWP_MutexUnlock(g_lock);
        }
        if (!USBKeyboard_IsConnected()) {
            // Looking for a keyboard walks the USB device list through
            // IOS: a few times a second is plenty.
            if (USBKeyboard_Open(OnKey) <= 0) {
                for (int ms = 0; ms < 250 && g_should_run; ms += 10) usleep(10000);
                continue;
            }
            g_caps = false;
        }
        // Scan waits in IOS for the keyboard's next report, so it runs
        // without g_lock: OnKey takes it for each key.
        if (USBKeyboard_Scan() < 0) usleep(10000);
        else usleep(400);
    }
    g_exited = true;
    return nullptr;
}

}  // namespace

bool UsbKeyboardStart() {
    if (g_running) return true;
    if (g_stuck) return false;
    if (USBKeyboard_Initialize() < 0) return false;
    if (g_lock == LWP_MUTEX_NULL && LWP_MutexInit(&g_lock, false) < 0) {
        USBKeyboard_Deinitialize();
        return false;
    }
    std::memset(g_live, 0, sizeof(g_live));
    std::memset(g_pressed_acc, 0, sizeof(g_pressed_acc));
    std::memset(g_held, 0, sizeof(g_held));
    std::memset(g_pressed, 0, sizeof(g_pressed));
    g_queue_head = g_queue_tail = 0;
    g_caps = false;
    g_lost = false;
    g_should_run = true;
    g_exited = false;
    // Below the GUI thread: the scan only sleeps or waits in IOS.
    if (LWP_CreateThread(&g_thread, ScanThread, nullptr, nullptr, 16384, 60) < 0) {
        g_thread = LWP_THREAD_NULL;
        g_should_run = false;
        g_exited = true;
        USBKeyboard_Deinitialize();
        return false;
    }
    g_running = true;
    return true;
}

bool UsbKeyboardStop() {
    if (!g_running) return true;
    g_running = false;
    g_should_run = false;
    for (int ms = 0; ms < 500 && !g_exited; ms += 10) usleep(10000);
    if (!g_exited) {
        // Waiting in USBKeyboard_Scan for a report that has not come.
        // Closing the keyboard now would free what that read returns into,
        // so the thread stays parked and the keyboard open until the IOS
        // that holds the read is reloaded.
        LWP_SuspendThread(g_thread);
        g_thread = LWP_THREAD_NULL;
        g_stuck = true;
        return false;
    }
    LWP_JoinThread(g_thread, nullptr);
    g_thread = LWP_THREAD_NULL;
    USBKeyboard_Close();
    USBKeyboard_Deinitialize();
    std::memset(g_held, 0, sizeof(g_held));
    std::memset(g_pressed, 0, sizeof(g_pressed));
    return true;
}

bool UsbKeyboardRunning() { return g_running; }

bool UsbKeyboardConnected() { return g_running && USBKeyboard_IsConnected(); }

void UsbKeyboardIosReloaded() {
    if (!g_stuck) return;
    // The parked thread may have held the lock when it stopped: a new one
    // for the next start, this one left with it.
    g_lock = LWP_MUTEX_NULL;
    g_stuck = false;
}

void UsbKeyboardPoll() {
    if (!g_running) {
        std::memset(g_held, 0, sizeof(g_held));
        std::memset(g_pressed, 0, sizeof(g_pressed));
        return;
    }
    LWP_MutexLock(g_lock);
    std::memcpy(g_held, g_live, sizeof(g_held));
    std::memcpy(g_pressed, g_pressed_acc, sizeof(g_pressed));
    std::memset(g_pressed_acc, 0, sizeof(g_pressed_acc));
    LWP_MutexUnlock(g_lock);
}

bool UsbKeyHeld(u8 code) { return BitGet(g_held, code); }

bool UsbKeyPressed(u8 code) { return BitGet(g_pressed, code); }

bool UsbKeyboardPop(KeyPress& out) {
    if (!g_running) return false;
    bool popped = false;
    LWP_MutexLock(g_lock);
    if (g_queue_tail != g_queue_head) {
        out = g_queue[g_queue_tail];
        g_queue_tail = (g_queue_tail + 1) % kQueueLength;
        popped = true;
    }
    LWP_MutexUnlock(g_lock);
    return popped;
}

void UsbKeyboardFlush() {
    if (!g_running) return;
    LWP_MutexLock(g_lock);
    g_queue_tail = g_queue_head;
    LWP_MutexUnlock(g_lock);
}

}  // namespace riftwii::wii
