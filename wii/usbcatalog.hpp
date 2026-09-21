// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/usbgame.hpp"

namespace riftwii::wii {

struct UsbGame {
    std::string path;               // primary usb:/ path
    std::string id;
    std::string title;
    UsbImageFormat format = UsbImageFormat::Iso;
    D2xFragmentList fragments;
};

struct UsbCatalog {
    std::vector<UsbGame> games;
    std::string status;
    // Empty when at least one candidate cIOS slot holds a ticket; otherwise
    // a user-visible warning that USB games cannot boot yet. Presence only:
    // identity is proven by the launch-time d2x probe, not here.
    std::string cios_note;
};

struct LaunchSource {
    bool usb = false;
    UsbGame game;
    int cios_slot = 0;  // 0 selects 249, then 250, then 251
};

// Starts libogc USB storage, mounts usb: read-only from Riftwii's point of
// view, and scans usb:/wbfs (flat and one nested game folder) and usb:/games.
// Entries that cannot be proven to be Wii images are skipped with their first
// failure retained in status. The USB volume must expose 512-byte sectors.
bool scan_usb_games(UsbCatalog& out, std::string& error);
void unmount_usb_games();

// The transition after the GUI has stopped. It leaves d2x owning USB and
// remounts SD only, so XML and redirect files remain available. `storage` is
// caller-owned memory that must outlive d2x configuration and the game boot.
bool activate_usb_game(const UsbGame& game, int cios_slot, void*& storage, std::size_t& storage_bytes,
                       const char* log_path, std::string& error);

}  // namespace riftwii::wii
