// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/symsearch.hpp"

namespace riftwii {

struct GcFontPatch {
    std::uint32_t address = 0;      // the VI DTV halfword load
    std::uint32_t instruction = 0;  // li <that load's destination>,2
};

// OSGetFontEncode and its inlined copies cache 0 (ANSI) or 1 (Shift-JIS).
// On NTSC they choose from VI DTV bit 0x2, the Japanese console strap,
// which is clear on Western Wiis. Recognize the cache guard, boot TV
// format, strap test and both stores by instruction fields and branches.
// Only Japanese game IDs receive patches; other formats and cached values
// keep their original paths. Both original CC00 and relocated CD00 VI
// bases are accepted. Unknown or incomplete shapes are left alone.
std::vector<GcFontPatch> find_gc_font_patches(const std::vector<CodeRange>& text, const std::string& game_id);

}  // namespace riftwii
