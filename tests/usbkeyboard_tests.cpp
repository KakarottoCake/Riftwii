// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The USB keyboard's thread (wii/controllers/usbkeyboard.cpp) stopped while
// its read waits in IOS, as an idle keyboard's does: turned off and on again
// in Settings, it comes back once the read does; with an IOS reload next,
// it is parked for the reload. libogc's threads, mutexes and libwiikeyboard
// are mocked here (tests/usbkeyboard_mock), and the test decides when a scan
// comes back.
#include <chrono>
#include <cstdlib>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <ogc/lwp.h>
#include <ogc/mutex.h>
#include <wiikeyboard/usbkeyboard.h>

#include "usbkeyboard.hpp"

namespace {

int g_failures = 0;
#define EXPECT_TRUE(cond)                                                                        \
    do {                                                                                         \
        if (!(cond)) {                                                                           \
            std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl;                  \
            ++g_failures;                                                                        \
        }                                                                                        \
    } while (0)

// Threads: each LWP thread is a host thread, kept to be joined. A suspended
// one is detached and never comes back from its scan, as on a Wii, where
// the IOS reload takes its read; the test ends with _Exit, past it.
std::map<lwp_t, std::thread> g_threads;
lwp_t g_next_thread = 1;
int g_suspended = 0;
std::set<std::thread::id> g_parked;

// Mutexes.
std::vector<std::unique_ptr<std::mutex>> g_mutexes;

// The keyboard: a scan waits until the test lets one report through.
std::mutex g_scan_lock;
std::condition_variable g_scan_wake;
int g_reports_allowed = 0;
std::set<std::thread::id> g_scanning;  // threads waiting in a scan
eventcallback g_callback = nullptr;
bool g_open = false;
int g_initialized = 0, g_deinitialized = 0, g_opened = 0, g_closed = 0;

bool WaitFor(const std::function<bool()>& done) {
    for (int ms = 0; ms < 2000; ++ms) {
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return done();
}

bool InScan() {
    std::lock_guard<std::mutex> hold(g_scan_lock);
    for (const std::thread::id& thread : g_scanning)
        if (!g_parked.count(thread)) return true;
    return false;
}

// A key pressed on the idle keyboard: the waiting scan comes back with it.
void PressKey() {
    std::lock_guard<std::mutex> hold(g_scan_lock);
    ++g_reports_allowed;
    g_scan_wake.notify_all();
}

}  // namespace

s32 LWP_CreateThread(lwp_t* thread, void* (*entry)(void*), void* arg, void*, u32, u8) {
    const lwp_t id = g_next_thread++;
    g_threads[id] = std::thread([entry, arg] { entry(arg); });
    *thread = id;
    return 0;
}

s32 LWP_JoinThread(lwp_t thread, void**) {
    auto found = g_threads.find(thread);
    if (found == g_threads.end()) return -1;
    found->second.join();
    g_threads.erase(found);
    return 0;
}

s32 LWP_SuspendThread(lwp_t thread) {
    auto found = g_threads.find(thread);
    if (found == g_threads.end()) return -1;
    {
        std::lock_guard<std::mutex> hold(g_scan_lock);
        g_parked.insert(found->second.get_id());
    }
    found->second.detach();
    g_threads.erase(found);
    ++g_suspended;
    return 0;
}

s32 LWP_MutexInit(mutex_t* mutex, bool) {
    g_mutexes.push_back(std::make_unique<std::mutex>());
    *mutex = static_cast<mutex_t>(g_mutexes.size() - 1);
    return 0;
}

s32 LWP_MutexLock(mutex_t mutex) {
    g_mutexes.at(mutex)->lock();
    return 0;
}

s32 LWP_MutexUnlock(mutex_t mutex) {
    g_mutexes.at(mutex)->unlock();
    return 0;
}

s32 USBKeyboard_Initialize() {
    ++g_initialized;
    return 0;
}

s32 USBKeyboard_Deinitialize() {
    ++g_deinitialized;
    return 0;
}

s32 USBKeyboard_Open(const eventcallback cb) {
    g_callback = cb;
    g_open = true;
    ++g_opened;
    return 1;
}

void USBKeyboard_Close() {
    g_open = false;
    ++g_closed;
}

bool USBKeyboard_IsConnected() { return g_open; }

s32 USBKeyboard_Scan() {
    {
        std::unique_lock<std::mutex> hold(g_scan_lock);
        const std::thread::id me = std::this_thread::get_id();
        g_scanning.insert(me);
        g_scan_wake.wait(hold, [me] { return g_reports_allowed > 0 && !g_parked.count(me); });
        --g_reports_allowed;
        g_scanning.erase(me);
    }
    g_callback(USBKeyboard_event{USBKEYBOARD_PRESSED, 0x04});  // A
    g_callback(USBKeyboard_event{USBKEYBOARD_RELEASED, 0x04});
    return 0;
}

s32 USBKeyboard_SetLed(const u32, bool) { return 0; }

int main() {
    using namespace riftwii::wii;

    // Plugged in and idle: the thread waits in its scan.
    EXPECT_TRUE(UsbKeyboardStart());
    EXPECT_TRUE(WaitFor(InScan));
    EXPECT_TRUE(g_opened == 1);

    // USB input turned off in Settings: the read is still out, so the
    // keyboard is not closed under it, and nothing is parked.
    EXPECT_TRUE(!UsbKeyboardStop(false));
    EXPECT_TRUE(UsbKeyboardDraining());
    EXPECT_TRUE(g_closed == 0 && g_suspended == 0);

    // Turned on again before a key is pressed: not yet.
    EXPECT_TRUE(!UsbKeyboardStart());
    EXPECT_TRUE(UsbKeyboardDraining());

    // A key comes: the old thread ends, and the next start closes the
    // keyboard it read and starts afresh, where before it stayed dead
    // until an IOS reload.
    PressKey();
    EXPECT_TRUE(WaitFor([] { return UsbKeyboardStart(); }));
    EXPECT_TRUE(!UsbKeyboardDraining());
    EXPECT_TRUE(g_closed == 1 && g_deinitialized == 1 && g_initialized == 2);
    EXPECT_TRUE(UsbKeyboardRunning());
    EXPECT_TRUE(WaitFor(InScan));

    // Keys still arrive on the new thread.
    PressKey();
    EXPECT_TRUE(WaitFor([] {
        UsbKeyboardPoll();
        KeyPress press;
        return UsbKeyboardPop(press) && press.code == 0x04 && press.ch == 'a';
    }));
    EXPECT_TRUE(WaitFor(InScan));

    // Off again, then a launch's IOS reload while that read is out: the
    // thread is parked for the reload, and nothing starts until it is done.
    EXPECT_TRUE(!UsbKeyboardStop(false));
    EXPECT_TRUE(!UsbKeyboardStop(true));
    EXPECT_TRUE(g_suspended == 1 && !UsbKeyboardDraining());
    EXPECT_TRUE(!UsbKeyboardStart());
    UsbKeyboardIosReloaded();
    EXPECT_TRUE(UsbKeyboardStart());
    EXPECT_TRUE(WaitFor(InScan));

    // A key pressed while it stops: the read comes back within the wait,
    // and the keyboard is closed at once.
    std::thread key([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        PressKey();
    });
    const int closed_before = g_closed;
    EXPECT_TRUE(UsbKeyboardStop(true));
    key.join();
    EXPECT_TRUE(!UsbKeyboardRunning() && !UsbKeyboardDraining());
    EXPECT_TRUE(g_closed == closed_before + 1);

    // The parked thread still waits, as it would until the reload.
    if (g_failures == 0) std::cout << "usbkeyboard: all passed" << std::endl;
    std::cout.flush();
    std::_Exit(g_failures == 0 ? 0 : 1);
}
