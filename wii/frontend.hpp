// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "modplan.hpp"
#include "riftwii/launch.hpp"
#include "usbcatalog.hpp"

namespace riftwii::wii {

constexpr const char* kPackageDir = "sd:/riivolution";
constexpr const char* kChoicesDir = "sd:/riftwii/choices";

struct FrontendState {
    std::string game_id;
    std::string disc_title;
    std::string disc_status;
    riftwii::LaunchModel model;
    std::string choices_path;
    CompiledMod compiled;
    bool has_compiled = false;
    UsbCatalog usb_catalog;
    bool use_usb = false;
    std::size_t usb_index = 0;
};

void IdentifyDisc(FrontendState& state);
void CycleSource(FrontendState& state);
LaunchSource SelectedSource(const FrontendState& state);
std::string ScanPackages(FrontendState& state);
void SaveChoices(const FrontendState& state);

}  // namespace riftwii::wii
