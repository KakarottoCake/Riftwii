// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

#include <string>

#include "covers.hpp"

// GameTDB's disc art (riftwii/coverart.hpp) under the box on a game's page:
// fetched as GameTDB serves it into sd:/riftwii/discs/<ID>.png, and made a
// texture only while the page shows it.
namespace riftwii::wii {

// True when the game has no stored disc picture and GameTDB was not found
// without one in the last week.
bool DiscArtWanted(const std::string& game_id);

// Downloads and stores the game's disc picture on the network's background
// thread, as StartCoverFetch does; one at a time.
bool StartDiscArtFetch(const std::string& game_id);
const std::string& DiscArtFetchGame();
bool TakeDiscArtFetch(std::string& game_id, CoverFetch& got, std::string& error);

// The stored picture as a GX RGB5A3 texture of w x h, in memory of its own
// (32-byte aligned, flushed for the GPU).
struct DiscArt {
    u8* data = nullptr;
    int w = 0, h = 0;
    DiscArt() = default;
    DiscArt(const DiscArt&) = delete;
    DiscArt& operator=(const DiscArt&) = delete;
    ~DiscArt();
};
// Reads and decodes the stored picture (a few milliseconds: the menu
// thread, not the GUI's). False when there is none, or it is unusable: a
// damaged one is removed, so DiscArtWanted asks for it again.
bool LoadDiscArt(const std::string& game_id, DiscArt& out);

}  // namespace riftwii::wii
