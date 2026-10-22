// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// A two-disc GameCube game's second disc, as the gamecube plugin takes it
// (runtime/gc/gc_disc.h changes discs during play): whether an image is
// the second disc of the game started, and how much room its FST needs
// where the game reads FSTs to.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace riftwii {

// A disc header's first 0x440 bytes (boot.bin), as both checks take them.
constexpr std::size_t kGcDiscHeaderBytes = 0x440;

// The largest FST a second disc may bring: the game reads it into the
// room at MEM1's top the loader keeps for disc 1's, which grows to fit
// it. 2 MB is far past any game's (a few hundred KB at most) and keeps
// the arena's top above RiftWii's own part.
constexpr std::uint32_t kGcSecondDiscFstMax = 0x00200000;

// Whether `second` (an image of `second_bytes` bytes) is the second disc
// of the game whose first disc's header is `first`: both GameCube discs,
// the same game ID (six bytes: game code and maker), disc numbers 0 and
// 1, and an FST inside the image and no larger than kGcSecondDiscFstMax.
// False with the reason in `why`, a phrase for the log.
bool gc_second_disc_matches(const std::uint8_t* first, const std::uint8_t* second, std::uint64_t second_bytes,
                            std::string& why);

// The FST room both discs need, from disc 1's (`first_room`, its FST's
// size or the larger "FST max" its header gives for a multi-disc game)
// and the second disc's header: the SDK refuses a disc whose FST is
// larger than the room the boot info gives (0x8000003C).
std::uint32_t gc_two_disc_fst_room(std::uint32_t first_room, const std::uint8_t* second);

}  // namespace riftwii
