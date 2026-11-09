// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/mempatch.hpp"
#include "riftwii/patch.hpp"

namespace riftwii {

// A GameCube pack's <memory> patches (GUIDE.md, "Mods"). The GameCube
// loader (plugins/gamecube/gcnative.cpp) puts the game together in its own
// buffers, with RiftWii's own changes to its code, and copies it into
// MEM1 only in its last moments, with the log already closed. So the
// patches are worked out first, against the game as it will be, here
// (where the log still says what was skipped and why), and the writes
// they come to are copied in last, after RiftWii's own (as for a Wii
// game, so a pack wins over the low-memory globals).

// One of the game's sections as the loader holds it: where it goes, its
// bytes.
struct GcStagedSection {
    std::uint32_t address = 0;
    const std::uint8_t* bytes = nullptr;
    std::size_t size = 0;
};

// One write the patches come to: `bytes` at `address`.
struct GcMemoryWrite {
    std::uint32_t address = 0;
    std::vector<std::uint8_t> bytes;
};

// Works out `patches` (apply_memory_patches' rules, in order) against
// `sections` (also where a search or an ocarina patch looks), memory
// outside them read as zeros (a GameCube start-up clears low memory and
// the BSS), each patch seeing the writes of those before it. The writes go
// to `writes`; anything outside `writable` is an error. `notes` says what
// was skipped (an original that differs, a pattern not found).
bool plan_gc_memory_writes(const std::vector<MemoryPatch>& patches, const std::vector<GcStagedSection>& sections,
                           const std::vector<MemoryRegion>& writable, std::vector<GcMemoryWrite>& writes,
                           std::vector<std::string>& notes, std::string& error);

}  // namespace riftwii
