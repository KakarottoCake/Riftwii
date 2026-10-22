// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "riftwii/symsearch.hpp"

namespace riftwii {

struct GcLoadHeaps {
    bool found = false;
    // Signed r13 displacements: the game's two main heaps, then JKRHeap's
    // system and root heaps. Read the globals at callback time, not launch.
    std::array<std::int32_t, 4> offsets{};
};

// Luigi's Mansion's group function changes the group of its two main
// heaps. JKRHeap's constructor compares both system and current against
// root, replacing either through its matching exchange setter. All four
// globals must agree with the loader's current-heap displacement.
// Missing, incomplete or ambiguous code returns found == false.
GcLoadHeaps find_gc_load_heaps(const std::vector<CodeRange>& text, std::uint32_t group,
                              std::int32_t current_offset);

}  // namespace riftwii
