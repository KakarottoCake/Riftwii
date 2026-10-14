// SPDX-FileCopyrightText: 2026 RiftWii contributors
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

// Whether the menu draws for a 16:9 TV: the setting, or with "auto" the
// Wii's own widescreen setting.
bool MenuWidescreen();
// The menu's display scale from the widescreen and screen size settings
// (vendor-libgui video.cpp's Menu_SetDisplayScale); again after a change.
void ApplyMenuDisplay();

}  // namespace riftwii::wii
