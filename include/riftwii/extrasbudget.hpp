// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// How much of a game's own memory RiftWii's extras take on a launch (the
// mods' runtime, the SD card passed through, the crash recorder,
// screenshots, the GameCube adapter), for the game page: each extra's code
// sits below the MEM1 arena's top and its state in the MEM2 arena, memory
// the game would otherwise have for its heaps. Most games never miss it;
// a game that sizes its heaps to the last byte can fail to start or crash
// (Newer Super Mario Bros. Wii and the Kirby games stopped with 0.8 MB of
// MEM2 taken by an early screenshots build).
namespace riftwii {

// Past these the game page calls it a lot.
constexpr std::uint32_t kExtrasHeavyMem1 = 128 * 1024;
constexpr std::uint32_t kExtrasHeavyMem2 = 640 * 1024;

struct ExtraPiece {
    std::string name;
    std::uint32_t mem1 = 0;
    std::uint32_t mem2 = 0;
};

struct ExtrasBudget {
    std::vector<ExtraPiece> pieces;

    // Ignores an extra that takes nothing.
    void add(const std::string& name, std::uint32_t mem1, std::uint32_t mem2);
    std::uint32_t mem1() const;
    std::uint32_t mem2() const;
    std::uint32_t total() const { return mem1() + mem2(); }
    bool heavy() const { return mem1() > kExtrasHeavyMem1 || mem2() > kExtrasHeavyMem2; }
    // "mods 95 KB, crash recorder 8 KB": the biggest first, in whole KB
    // (rounded up); "" when there are none.
    std::string list() const;
};

// Whole KB, rounded up.
std::uint32_t whole_kb(std::uint32_t bytes);

}  // namespace riftwii
