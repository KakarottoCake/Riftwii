// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "modplan.hpp"
#include "riftwii/launch.hpp"
#include "riftwii/profiles.hpp"
#include "usbcatalog.hpp"

namespace riftwii::wii {

// Riivolution reads pack XMLs from both folders, on every drive it mounts.
constexpr const char* kPackageDir = "sd:/riivolution";
constexpr const char* kPackageDir2 = "sd:/apps/riivolution";
constexpr const char* kChoicesDir = "sd:/riftwii/choices";
// ScanPackages says this when the card scanned cleanly; the mods screen
// shows its hotkey line instead (it says the same, and more).
constexpr const char* kScanReady = "ready";

struct FrontendState {
    std::string game_id;
    std::string disc_title;
    std::string disc_status;
    std::uint8_t game_revision = 0;
    std::uint8_t game_disc_number = 0;
    riftwii::LaunchModel model;
    std::string choices_path;
    // A profile's tile was opened (riftwii/profiles.hpp): its key and name,
    // and choices_path is its own choices file. Empty for the game's own tile.
    std::string profile_key;
    std::string profile_name;
    CompiledMod compiled;
    bool has_compiled = false;
    UsbCatalog usb_catalog;
    ImageCatalog sd_catalog;
    bool use_usb = false;
    bool use_sd = false;
    std::size_t usb_index = 0;
    std::size_t sd_index = 0;
    // Shown on the first press of Start for a game that plays at the
    // player's own risk (an RVZ outside what was tested); the second press
    // starts it.
    std::string launch_warning;
    bool warning_shown = false;
    // Picked at Start for a code mod: without RiftWii's extras (as the
    // mod's own loader would start it).
    bool code_build_plain = false;
    // Folders on the USB drive holding code builds for this game (usb_mod_folders),
    // found by ScanPackages. The game does not start while there are any.
    std::vector<std::string> usb_mods;
};

// Initializes source labels only. It performs no storage enumeration or DI I/O
// so the source screen appears before a slow/broken device is touched.
void InitializeFrontend(FrontendState& state);
// Explicit physical-disc probe, used by DISC selection and headless autorun.
void IdentifyDisc(FrontendState& state, void (*progress)(const char*) = nullptr);
bool SelectDisc(FrontendState& state, std::string& error);
bool SelectUsbGame(FrontendState& state, std::size_t index, std::string& error);
bool SelectSdGame(FrontendState& state, std::size_t index, std::string& error);
LaunchSource SelectedSource(const FrontendState& state);
std::string ScanPackages(FrontendState& state);
// Why the game cannot start with its mods where they are, or "": a code
// build on the USB drive, or one turned on with the game on the SD card. `brief` is
// the short form for the game page.
std::string ModPlaceProblem(const FrontendState& state, bool brief = false);
bool SaveChoices(const FrontendState& state, std::string& error);
// `model`'s choices written to `path` (a profile's new file), safely as SaveChoices.
bool SaveChoicesTo(const riftwii::LaunchModel& model, const std::string& path, std::string& error);
// The game just selected, opened as one of its profiles: its choices from
// then on are the profile's own file. Call after SelectUsbGame or
// SelectSdGame (which leave the game's own choices selected).
void SelectProfile(FrontendState& state, const riftwii::Profile& profile);
// What Home keys the open tile on (play history, favourites): the
// profile's key, or the game ID.
std::string TileKey(const FrontendState& state);

}  // namespace riftwii::wii
