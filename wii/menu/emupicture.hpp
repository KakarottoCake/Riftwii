// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "riftwii/picturepick.hpp"

namespace riftwii::wii {

// Network-worker only. Returns metadata, never downloads an image.
bool FindEmulatorPictures(const std::string& system, const std::string& game, bool steamgrid,
                          std::vector<PictureCandidate>& candidates, std::string& error);
enum class OwnPicture { Missing, Picker, Hand };
OwnPicture OwnPictureState(const std::string& path);
// The picker has already decoded and checked this original PNG.
bool SavePickedPicture(const std::string& path, const std::vector<std::uint8_t>& png);
bool RemovePickedPicture(const std::string& path, bool allow_hand = false);

}  // namespace riftwii::wii
