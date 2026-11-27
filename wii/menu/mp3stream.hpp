// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

// The menu's MP3 music (menumusic.cpp): libmad decodes, ASND voice 0 plays,
// as the Ogg player and the Wii Menu's music do. Its decoder, buffers and
// thread stack are one MEM2 block taken at the first MP3 and kept, so a
// menu that plays no MP3 spends no memory on it (libogc's own MP3 player
// kept 120 KiB of MEM1 for it in every menu).
namespace riftwii::wii {

// Plays `bytes` from `data`, which stays valid until Mp3Stop. Once through:
// Mp3Playing turns false at its end. False when it does not start.
bool Mp3Start(const std::uint8_t* data, std::size_t bytes);
void Mp3Stop();
// Started and not yet at its end.
bool Mp3Playing();
// 0 to 255.
void Mp3Volume(int volume);

}  // namespace riftwii::wii
