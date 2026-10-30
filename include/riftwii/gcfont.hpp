// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/symsearch.hpp"

namespace riftwii {

struct GcFontPatch {
    std::uint32_t address = 0;      // the VI DTV halfword load
    std::uint32_t instruction = 0;  // li <that load's destination>,2
};

struct GcFontRead {
    std::uint32_t address = 0;
    std::array<std::uint32_t, 5> words{};  // load and up to four following words
    unsigned word_count = 0;
};

// OSGetFontEncode and its inlined copies cache 0 (ANSI) or 1 (Shift-JIS).
// On NTSC they choose from VI DTV bit 0x2, the Japanese console strap,
// which is clear on Western Wiis. Trace a nearby constant VI address and
// require the loaded register to be reduced to that bit before any other
// use, followed by a conditional test. Cache and TV-format layouts may vary.
// Only Japanese IDs receive patches. Uncertain reads stay unchanged and
// can be collected for the boot log; no instruction crosses a section edge.
std::vector<GcFontPatch> find_gc_font_patches(const std::vector<CodeRange>& text, const std::string& game_id,
                                           std::vector<GcFontRead>* unclassified = nullptr);

}  // namespace riftwii
