// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/usbgame.hpp"

namespace riftwii::wii {

enum class ImageDevice { Usb, Sd };

struct ImageGame {
    ImageDevice device = ImageDevice::Usb;
    std::string path;               // primary usb:/ or sd:/ path
    std::string id;
    std::string title;              // the disc header's internal name
    std::string display;            // what the list shows (see riftwii/titles.hpp)
    std::uint8_t revision = 0;
    std::uint8_t disc_number = 0;
    UsbImageFormat format = UsbImageFormat::Iso;
    D2xFragmentList fragments;
    // Header read and fragment list built. An image whose name carries its
    // ID is listed without being opened (a drive can hold hundreds);
    // check_image_game opens it when it is picked.
    bool checked = false;
};

struct ImageCatalog {
    ImageDevice device = ImageDevice::Usb;
    std::vector<ImageGame> games;
    std::string status;
    // Empty when at least one candidate cIOS slot holds a ticket; otherwise
    // a user-visible warning that image games cannot boot yet. Presence only:
    // identity is proven by the launch-time d2x probe, not here.
    std::string cios_note;
};

struct LaunchSource {
    enum class Kind { Disc, Usb, Sd } kind = Kind::Disc;
    ImageGame game;
    int cios_slot = 0;  // 0 selects 249, then 250, then 251
};

using UsbGame = ImageGame;
using UsbCatalog = ImageCatalog;

// Starts libogc USB storage, mounts usb: read-only from Riftwii's point of
// view, and scans usb:/wbfs (flat and one nested game folder) and usb:/games.
// Entries that cannot be proven to be Wii images are skipped with their first
// failure retained in status. The USB volume must expose 512-byte sectors.
bool scan_usb_games(UsbCatalog& out, std::string& error);
bool scan_sd_games(ImageCatalog& out, std::string& error);
void unmount_usb_games();
// Opens a listed game that is not checked yet: its pieces, disc header and
// d2x fragment list, filling title, revision and disc number (and the ID
// from the header). Needs the catalog's drive still mounted.
bool check_image_game(ImageGame& game, std::string& error);
// Whether a cIOS slot holds a launchable (non-stub) title.
bool slot_has_ticket(int slot);

// The transition after the GUI has stopped. It leaves d2x owning the selected
// image device and remounts SD, so XML and redirect files remain available. `storage` is
// caller-owned memory that must outlive d2x configuration and the game boot.
bool activate_image_game(const ImageGame& game, int cios_slot, void*& storage, std::size_t& storage_bytes,
                         const char* log_path, std::string& error);
inline bool activate_usb_game(const UsbGame& game, int cios_slot, void*& storage, std::size_t& storage_bytes,
                              const char* log_path, std::string& error) {
    return activate_image_game(game, cios_slot, storage, storage_bytes, log_path, error);
}

}  // namespace riftwii::wii
