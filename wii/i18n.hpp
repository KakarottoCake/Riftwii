// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <initializer_list>
#include <string>

// The menu in the player's language. libgui's GuiText passes every string
// it shows through gettext(), so fixed English text is translated where it
// is drawn; text built from pieces goes through tr() with {1}, {2}...
// placeholders, so a translation can move them. The catalogs are .po files
// built into RiftWii (wii/lang); sd:/riftwii/lang/<lang>.po, when present,
// is read over the built-in one, so a player can fix or finish one.
namespace riftwii::wii {

// Loads the catalog of `lang` ("en", "es", "ja", "pt", "it").
void SetMenuLanguage(const std::string& lang);

const char* tr(const char* english);
std::string tr(const char* english, std::initializer_list<std::string> args);

}  // namespace riftwii::wii
