// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "riftwii/symsearch.hpp"

namespace riftwii {

// Same order as runtime/gc/gc_card.h's gc_exi_fn and gc_entry.S's table.
constexpr unsigned kGcExiHooks = 11;
struct GcExiFunctions {
    std::uint32_t blocks = 0;
    std::array<std::uint32_t, kGcExiHooks> hooks{};
};

// The SDK's EXI control blocks and all the functions needed for slot A,
// or an empty result when any required function is missing or ambiguous.
// Recognises 0x40-byte blocks (slwi by 6) and the 2001 SDK's 0x38-byte
// blocks (mulli by 0x38); only the latter may omit EXIProbeEx.
GcExiFunctions find_gc_exi_functions(const std::vector<CodeRange>& text);

}  // namespace riftwii
