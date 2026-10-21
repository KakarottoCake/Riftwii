// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2004 Michael Wiedenbauer (shagkur)
// SPDX-FileCopyrightText: 2004 Dave Murphy (WinterMute)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "riftwii/gcimport.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace riftwii {

// EXI DMA addresses only MEM1. A MEM2 heap pointer aliases unrelated MEM1
// after libogc applies the hardware's 0x03FFFFE0 address mask.
bool gc_real_dma_range(std::uintptr_t address, std::size_t bytes);

// The Wii's build has no exceptions, so a std::vector that cannot grow
// aborts the menu. This asks for every block a job will need at once (the
// card's copy, the SD image, the plan's working image) and gives them all
// back, so the job is refused with a message instead of aborting half way.
// `allocate` is malloc when null; a test passes one that says no.
using GcRealAllocate = void* (*)(std::size_t);
constexpr std::size_t kGcRealSlack = 1u << 20;  // saves being exported, strings, file buffers
// At most kGcRealMaxBlocks sizes (plain arrays: the DOL has little room to spare).
constexpr std::size_t kGcRealMaxBlocks = 6;
bool gc_real_have_room(const std::size_t* sizes, std::size_t count, GcRealAllocate allocate = nullptr);

// A preview never changes the destination. The same plan is committed only
// after the hardware reader confirms that its snapshot still belongs to the card.
struct GcRealPlan {
    unsigned selected = 0;
    GcImportCount count;
    bool out_of_memory = false;  // not planned: no room for the working image (error says so)
    std::vector<std::uint8_t> image;
};

bool gc_real_plan(const std::vector<std::uint8_t>& source, const std::vector<std::uint8_t>& target,
                  const std::string& game_id, bool every_save, GcRealPlan& out, std::string& error,
                  GcRealAllocate allocate = nullptr);

// Stage and close the new SD image, then check the source immediately before
// committing it. Cancellation leaves the destination byte-for-byte unchanged.
bool gc_real_commit(const std::string& path, const std::vector<std::uint8_t>& image,
                    const std::function<bool()>& source_unchanged, std::string& error,
                    GcRealAllocate allocate = nullptr);

}  // namespace riftwii
