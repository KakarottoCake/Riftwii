// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The launch's own /dev/usb/hid handle (wii/hooks/hidsession.cpp) against a
// mocked IOS that reloads: a handle from before a reload must not be used
// after it (#284), so the next use really opens on the new IOS, and a stuck
// handle is never closed.
#include "hidsession.hpp"

#include <gccore.h>

#include <iostream>
#include <string>

namespace {

int failures = 0;
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "Failed: " #condition " at " << __LINE__ << '\n'; \
            ++failures; \
        } \
    } while (0)

constexpr s32 kIpcNotFound = -6;  // what IOS answers for a descriptor it does not know

// One IOS at a time. A reload kills every descriptor without a close, and
// descriptor numbers start over, so a stale number can match a live one:
// the mock tells them apart by the IOS they came from.
struct MockIos {
    unsigned generation = 1;
    s32 live_fd = -1;
    unsigned live_generation = 0;
    unsigned opens = 0;
    unsigned closes = 0;
    unsigned dead_handle_calls = 0;

    void reload() {
        ++generation;
        live_fd = -1;
    }
    bool is_live(s32 fd) const { return fd >= 0 && fd == live_fd && live_generation == generation; }
} ios;

void start_over() { ios = MockIos{}; }

// What padhook.cpp's use does: the held handle if there is one, else a real
// open (open_usb_hid in the app) on the running IOS.
bool use(riftwii::wii::OwnHidHandle& handle) {
    if (handle.is_open()) return true;
    ++ios.opens;
    CHECK(ios.live_fd == -1);  // one handle at a time
    ios.live_fd = 17;          // the new IOS may reuse the old number
    ios.live_generation = ios.generation;
    handle.fd = ios.live_fd;
    handle.version = 5;
    handle.device = 3;
    return true;
}

// The bug: open, a failed launch, reload to the menu's IOS, use again.
// Before the fix the second use found the first IOS's handle and never opened.
void test_reload_between_uses_opens_again() {
    start_over();
    riftwii::wii::OwnHidHandle handle;
    CHECK(use(handle));
    CHECK(ios.opens == 1 && ios.is_live(handle.fd));

    handle.forget_for_reload(false);  // reload_ios, old IOS still answering
    CHECK(ios.closes == 1);
    CHECK(!handle.is_open() && handle.version == 0 && handle.device == -1);
    ios.reload();

    CHECK(use(handle));
    CHECK(ios.opens == 2);
    CHECK(ios.is_live(handle.fd));
    CHECK(ios.dead_handle_calls == 0);
    std::cout << "use after a reload: IOS_Open calls=" << ios.opens << " fd=" << handle.fd << '\n';
}

// A handle with a request IOS never answered: the close would wait behind
// it, so it is dropped unclosed and the next use still opens fresh.
void test_stuck_handle_is_not_closed() {
    start_over();
    riftwii::wii::OwnHidHandle handle;
    CHECK(use(handle));
    handle.forget_for_reload(true);
    CHECK(ios.closes == 0 && ios.dead_handle_calls == 0);
    CHECK(!handle.is_open());
    ios.reload();
    CHECK(use(handle));
    CHECK(ios.opens == 2);
}

// No handle held (the common reload) and a second forget close nothing.
void test_forget_without_a_handle() {
    start_over();
    riftwii::wii::OwnHidHandle handle;
    handle.forget_for_reload(false);
    CHECK(ios.closes == 0);
    CHECK(use(handle));
    handle.forget_for_reload(false);
    handle.forget_for_reload(false);
    CHECK(ios.closes == 1);
}

// The handle given to the game is the game's: a later forget leaves it be.
void test_handed_over_handle_is_not_closed() {
    start_over();
    riftwii::wii::OwnHidHandle handle;
    CHECK(use(handle));
    handle.hand_over();
    handle.forget_for_reload(false);
    CHECK(ios.closes == 0);
    CHECK(!handle.is_open());
}

// A plain close (no game takes the handle) closes the handle and clears it.
void test_plain_close() {
    start_over();
    riftwii::wii::OwnHidHandle handle;
    CHECK(use(handle));
    handle.close(false);
    CHECK(ios.closes == 1 && !handle.is_open());
}

}  // namespace

s32 IOS_Close(s32 fd) {
    if (!ios.is_live(fd)) {
        ++ios.dead_handle_calls;
        return kIpcNotFound;
    }
    ++ios.closes;
    ios.live_fd = -1;
    return 0;
}

int main() {
    test_reload_between_uses_opens_again();
    test_stuck_handle_is_not_closed();
    test_forget_without_a_handle();
    test_handed_over_handle_is_not_closed();
    test_plain_close();
    if (failures == 0) std::cout << "the own /dev/usb/hid handle is opened again after every IOS reload\n";
    return failures == 0 ? 0 : 1;
}
