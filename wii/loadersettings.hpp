// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "riftwii/settingsfile.hpp"

// RiftWii's settings (sd:/riftwii/settings.txt, riftwii/settingsfile.hpp),
// read once when first asked for and written on every change.
namespace riftwii::wii {

LoaderSettings& Settings();
bool SaveSettings();

// The menu's language: the setting, or with "auto" the Wii's own when
// RiftWii has it (English otherwise). "en", "es", "ja", "pt" or "it".
std::string MenuLanguage();

}  // namespace riftwii::wii
