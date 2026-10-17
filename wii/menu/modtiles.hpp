// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "frontend.hpp"
#include "riftwii/profiles.hpp"

// A game's extra Home tiles with mods of their own (riftwii/profiles.hpp),
// kept on the SD card: the list in sd:/riftwii/profiles.txt, each one's
// choices in sd:/riftwii/choices/<key>.txt.
namespace riftwii::wii {

constexpr const char* kProfilesPath = "sd:/riftwii/profiles.txt";

// The list, read from the card once.
ProfileList& Profiles();

// A new tile for the open game (one on a drive: Home shows a profile's
// tile only while it lists its game), named `name`, starting with the choices on
// its page now (mods, cheats, saves and options); its picture is the first
// mod turned on that has one. `key` is set to the new tile's key. False
// with `error` (said to the player) when it can't be made.
bool CreateProfile(const FrontendState& state, const std::string& name, std::string& key, std::string& error);
// The open profile's tile renamed (its files keep their names).
bool RenameProfile(FrontendState& state, const std::string& name, std::string& error);
// The open profile's tile removed with its choices file, its favourite and
// its plays; the page goes back to being the game's own (its choices are
// read again by the caller, ScanPackages).
bool RemoveProfile(FrontendState& state, std::string& error);

}  // namespace riftwii::wii
