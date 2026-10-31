// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include "rthid.h"

// The launch's own /dev/usb/hid handle (padhook.cpp opens it before libogc's
// USB touches the device, and hands it to the game). The state is here, apart
// from padhook.cpp's IOS plumbing, so a host test can run it against a mocked
// IOS across a reload.
//
// A handle belongs to the IOS that gave it out. After an IOS reload it is a
// dead descriptor, and reusing it would skip the open on the new IOS.
namespace riftwii::wii {

// IOS v5 sends a list per device change, rather than per opened handle.
// Keep the list read just before libogc shuts down for the game. A new
// manager's empty cache must not erase it; a real IOS reload must.
struct HidDeviceHandoff {
    rthid_device devices[RTHID_DEVICE_MAX]{};
    unsigned count = 0;

    void clear() { count = 0; }

    void remember(const rthid_device* list, unsigned listed) {
        count = list && listed <= RTHID_DEVICE_MAX ? listed : 0;
        for (unsigned i = 0; i < count; ++i) devices[i] = list[i];
    }

    unsigned restore_if_empty(rthid_device* list, unsigned listed) const {
        if (listed) return listed;
        for (unsigned i = 0; i < count; ++i) list[i] = devices[i];
        return count;
    }
};

struct OwnHidHandle {
    std::int32_t fd = -1;        // -1: none
    std::uint32_t version = 0;   // 4 or 5, 0: none
    std::int32_t device = -1;    // the adapter's v5 device id, -1: not listed

    bool is_open() const { return fd >= 0; }

    // Gives up the handle by closing it. `stuck`: an IOS request on
    // /dev/usb/hid never came back, so a close would queue behind it and
    // wait too; the handle is then dropped without a close. Either way the
    // fields are cleared.
    void close(bool stuck);

    // Before an IOS reload, while the old IOS still runs: the handle is
    // closed (unless stuck) and forgotten, so the next use opens a fresh one
    // on the new IOS. The caller's stuck flag is left as it is.
    void forget_for_reload(bool stuck) { close(stuck); }

    // The handle goes to the game: it is no longer ours to close.
    void hand_over() { *this = OwnHidHandle{}; }
};

}  // namespace riftwii::wii
