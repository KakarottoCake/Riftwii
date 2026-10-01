// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/redirect.hpp"
#include "riftwii/rvz.hpp"
#include "riftwii/usbgame.hpp"
#include "resident.hpp"

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
    // The IOS its TMD asks for, read when it is opened (0: not known).
    std::uint32_t required_ios = 0;
    UsbImageFormat format = UsbImageFormat::Iso;
    // A disc of a USB drive formatted as WBFS (riftwii/wbfspart.hpp): its
    // slot there. -1 for image files.
    int wbfs_slot = -1;
    D2xFragmentList fragments;
    // Header read and fragment list built. An image whose name carries its
    // ID is listed without being opened (a drive can hold hundreds);
    // check_image_game opens it when it is picked.
    bool checked = false;
    // An RVZ image: whether RiftWii plays it (riftwii/rvz.hpp's check_rvz),
    // filled when it is opened. Unsupported ones are listed, and picking
    // one says why it cannot play.
    RvzSupport rvz_support = RvzSupport::Supported;
    std::vector<std::string> rvz_reasons;
    std::uint64_t rvz_disc_bytes = 0;
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

// Starts libogc USB storage, mounts usb: read-only from RiftWii's point of
// view, and scans usb:/wbfs (flat and one nested game folder) and usb:/games,
// or lists the slots of a drive formatted as WBFS.
// Entries that cannot be proven to be Wii images are skipped with their first
// failure retained in status. The USB volume must expose 512-byte sectors.
bool scan_usb_games(UsbCatalog& out, std::string& error);
// The top folders that pick a USB drive's partition when it has several:
// the user's game_folders, then wbfs, games and riivolution. Every mount
// of the drive uses it, so the menu and the launch read the same one.
std::vector<std::string> usb_wanted_folders();
bool scan_sd_games(ImageCatalog& out, std::string& error);
void unmount_usb_games();
// Unmounts and stops libogc's USB driver when it was started in this IOS
// (before an IOS reload, and before d2x's own USB device takes the drive).
void release_usb_driver();
// Opens a listed game that is not checked yet: its pieces, disc header and
// d2x fragment list, filling title, revision and disc number (and the ID
// from the header). Needs the catalog's drive still mounted.
bool check_image_game(ImageGame& game, std::string& error);
// For an RVZ game that plays at the player's own risk: the warning to show
// before it starts. Empty otherwise.
std::string rvz_warning(const ImageGame& game);
// Code builds for `game_id` left on the USB drive (FAT32 or NTFS): a
// <game ID>.gct in a top folder or its codes folder. A code build reads
// the SD card itself while the game runs, so the menu will not start the
// game while one is there. The folders to move, like "usb:/Project+";
// empty with no drive.
std::vector<std::string> usb_mod_folders(const std::string& game_id);
// Pack XMLs on the USB drive (FAT32 or NTFS), for the menu: the .xml
// names directly in `folder` ("/riivolution"), sorted, hidden ones left
// out; and a whole XML by "usb:/..." path (1 MiB at most). Nothing with no
// drive mounted.
std::vector<std::string> usb_xml_names(const std::string& folder);
bool read_usb_text(const std::string& usb_path, std::string& out);
// Bytes of a file on the USB drive (the menu's view), by "usb:/..." path;
// `size` gets the file's size. False past its end or when there is no
// such file.
bool read_usb_range(const std::string& usb_path, std::uint64_t offset, std::uint8_t* out, std::size_t length,
                    std::uint64_t& size);
// Whether the menu's view of the USB drive is up (no drive is started for it).
bool usb_volume_ready();
// Development aid for Dolphin, which has no d2x: partition reads of the
// disc Dolphin boots (the RVZ's stub, made by tools/rvz) are answered from
// the RVZ at `sd_path` instead.
bool serve_disc_from_rvz(const std::string& sd_path, std::string& error);
// The RVZ game about to start (its game partition opened through the
// loader's partition reads): what the in-game runtime needs, with the
// group table written to sd:/riftwii/rvz/<ID>.groups. Needs the card
// mounted.
bool rvz_resident_options(RvzResidentOptions& out, std::string& error);
// A file on the SD card as a fresh view of the card sees it (what libfat
// wrote is there once it has flushed): its size and its pieces as
// absolute sectors of the card. Needs the card mounted.
bool sd_file_pieces(const std::string& sd_path, std::uint64_t& size, std::vector<Fragment>& out, std::string& error);
// A game's name from the title list (GameTDB, in the menu's language),
// else `internal`, the disc header's.
std::string GameDisplayName(const std::string& id, const std::string& internal);
// Reads the title list again (after a language change or a download).
void ReloadTitles();
// Names (and sorts) a scanned catalog's games again from the title list.
void RenameGames(ImageCatalog& catalog);
// Whether a cIOS slot holds a launchable (non-stub) title.
bool slot_has_ticket(int slot);

// The cIOS slots to try for an image game, in order. `chosen` (the
// game's or the global setting) alone when set; on automatic, USB Loader
// GX's choice first (riftwii/gxpatches.hpp's gx_pick_cios: the d2x slot
// whose base is the IOS the game asks for), then 249, 250 and 251.
std::vector<int> image_cios_order(const ImageGame& game, int chosen);

// The transition after the GUI has stopped. It leaves d2x owning the selected
// image device and remounts SD, so XML and redirect files remain available. `storage` is
// caller-owned memory that must outlive d2x configuration and the game boot.
// `block_ios_reload`: d2x keeps its cIOS (and so the virtual disc) when the
// program started next reloads IOS itself (see di::set_ios_reload_block).
bool activate_image_game(const ImageGame& game, int cios_slot, void*& storage, std::size_t& storage_bytes,
                         const char* log_path, std::string& error, bool block_ios_reload = false);
// For a disc whose packs are on the USB drive while the menu runs a non-d2x
// IOS: reloads the cIOS (`cios_slot`, or 249, then 250, then 251) with the
// same teardown as activate_image_game, and brings the SD card and the log
// back. The game then runs under it (d2x's USB device is read in game).
// `purpose` names what the reload is for in the log.
bool activate_disc_cios(int cios_slot, const char* log_path, std::string& error,
                        const char* purpose = "the packs on the USB drive");

}  // namespace riftwii::wii
