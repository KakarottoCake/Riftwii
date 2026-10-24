// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The SD card's presence as the menu watches it (wii/menu/rift_menu.cpp):
// a small debounce so one missed poll does not unmount the card, and one
// lucky poll does not remount it. Pure data so the host tests cover it;
// the Wii side owns the mounting itself.
namespace riftwii {

enum class SdHotplugEvent {
    None,
    Removed,
    Inserted,
};

class SdHotplug {
public:
    explicit SdHotplug(bool mounted = true);

    bool mounted() const { return mounted_; }

    // One poll's answer from the card slot (`__io_wiisd.isInserted()` on
    // the Wii). Two in a row the other way confirm the change; a single
    // miss or hit leaves the state alone.
    SdHotplugEvent update(bool inserted);

private:
    bool mounted_ = true;
    int away_ = 0;
    int back_ = 0;
};

}  // namespace riftwii
