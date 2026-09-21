// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "modplan.hpp"
#include "riftwii/launch.hpp"
#include "usbcatalog.hpp"

namespace riftwii::wii {

constexpr const char* kPackageDir = "sd:/riivolution";
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
    CompiledMod compiled;
    bool has_compiled = false;
    UsbCatalog usb_catalog;
    ImageCatalog sd_catalog;
    bool use_usb = false;
    bool use_sd = false;
    std::size_t usb_index = 0;
    std::size_t sd_index = 0;
};

void IdentifyDisc(FrontendState& state);
bool SelectDisc(FrontendState& state, std::string& error);
bool SelectUsbGame(FrontendState& state, std::size_t index, std::string& error);
bool SelectSdGame(FrontendState& state, std::size_t index, std::string& error);
LaunchSource SelectedSource(const FrontendState& state);
std::string ScanPackages(FrontendState& state);
bool SaveChoices(const FrontendState& state, std::string& error);

}  // namespace riftwii::wii
