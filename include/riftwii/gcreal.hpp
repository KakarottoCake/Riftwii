// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2004 Michael Wiedenbauer (shagkur)
// SPDX-FileCopyrightText: 2004 Dave Murphy (WinterMute)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "riftwii/gcimport.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>

namespace riftwii {

// EXI DMA addresses only MEM1. A MEM2 heap pointer aliases unrelated MEM1
// after libogc applies the hardware's 0x03FFFFE0 address mask.
bool gc_real_dma_range(std::uintptr_t address, std::size_t bytes);

// A preview never changes the destination. The same plan is committed only
// after the hardware reader confirms that its snapshot still belongs to the card.
struct GcRealPlan {
    unsigned selected = 0;
    GcImportCount count;
    std::vector<std::uint8_t> image;
};

bool gc_real_plan(const std::vector<std::uint8_t>& source, const std::vector<std::uint8_t>& target,
                  const std::string& game_id, bool every_save, GcRealPlan& out, std::string& error);

// Stage and close the new SD image, then check the source immediately before
// committing it. Cancellation leaves the destination byte-for-byte unchanged.
bool gc_real_commit(const std::string& path, const std::vector<std::uint8_t>& image,
                    const std::function<bool()>& source_unchanged, std::string& error);

}  // namespace riftwii
