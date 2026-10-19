// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

// Randomness for TLS. The Wii gives homebrew no random source, and seeding
// from the clock alone left a handshake's secrets within reach of anyone
// who saw its first message (SEC-01). So the seed is a SHA-256 pool that
// gathers, all session long, what an onlooker cannot know:
// - a secret seed file on the SD card (sd:/riftwii/seed.bin), read at start
//   and replaced at once, and again after every seeding, so no two starts
//   or connections share it;
// - the console's own ID, which never goes over the network;
// - the time base at events with real jitter: every frame's Wii Remote
//   data (sensor noise, IR, the moment it came), and each seeding.
// A fresh install's very first start has only the clock, the console ID
// and what the Remotes said before its first connection.
namespace riftwii::wii {

// At start, once the card is mounted: the seed file and the console's ID.
void EntropyStart();
// Mixes `size` bytes of `data` into the pool, with the time base now.
void EntropyAdd(const void* data, std::size_t size);
// `size` bytes from the pool for a seeding; the pool moves on (what was
// given cannot be worked out from it later) and the seed file is renewed.
void EntropyTake(void* out, std::size_t size);

}  // namespace riftwii::wii
