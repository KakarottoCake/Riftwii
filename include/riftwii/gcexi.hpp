// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/symsearch.hpp"

namespace riftwii {

// Same order as runtime/gc/gc_card.h's gc_exi_fn and gc_entry.S's table.
constexpr unsigned kGcExiHooks = 11;
struct GcExiFunctions {
    std::uint32_t blocks = 0;
    std::array<std::uint32_t, kGcExiHooks> hooks{};
    // Only for the log, never to hook: on a rejection, what the better of
    // the two layouts did find (0 for each function it did not), so a
    // problem report says which function kept the memory card off.
    std::array<std::uint32_t, kGcExiHooks> partial{};
    // How many control-block tables resolved every function: 1 when the
    // result was taken, more when the finder refused to choose between them.
    unsigned resolving_tables = 0;
    // The result leaves EXIProbeEx 0 because the game's SDK has none (the
    // 2001 SDK, or a listed game's), not because it was missed.
    bool probe_ex_absent = false;
};

// The SDK's EXI control blocks and all the functions needed for slot A,
// or an empty result when any required function is missing or ambiguous.
// Recognises 0x40-byte blocks (slwi by 6) and the 2001 SDK's 0x38-byte
// blocks (mulli by 0x38). Exactly one table must resolve every function.
// Only the 2001 SDK, and the 0x40-byte SDK of the few games listed by ID
// (`game_id`, the disc's six-character ID) whose code shows no room for
// one, may omit EXIProbeEx.
GcExiFunctions find_gc_exi_functions(const std::vector<CodeRange>& text, const std::string& game_id = {});

// The SDK name of hook `index` (gc_card.h's gc_exi_fn order), for the log:
// a game whose card is off then says which function the finder missed.
const char* gc_exi_function_name(unsigned index);

}  // namespace riftwii
