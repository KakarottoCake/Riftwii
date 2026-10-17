// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "riftwii/symsearch.hpp"

// A GameCube game's picture settings that are found in its code before it
// starts (wii/launch/gcnative.cpp applies them; the blob's VIConfigure hook
// does the rest, runtime/gc/gc_video.h).
namespace riftwii {

// Sharper picture. A GameCube game blends neighbouring lines when it
// copies its picture to the frame buffer, to hide an interlaced TV's
// flicker: GXSetCopyFilter(aa, sample_pattern, vf, vfilter) takes the
// seven weights of that "vertical filter" when vf is set, and otherwise
// writes the weights that copy each line as it is (0, 0, 21, 22, 21, 0, 0
// out of 64, its two BP registers 0x53595000 and 0x54000015). Games pass
// their render mode's weights, or make their own at run time (Luigi's
// Mansion's PAL picture), so the switch is made in the function: its test
// of vf (`beq` to where it loads those two words) becomes a `b`, and every
// copy is sharp. A game that blurs a copy for an effect of its own loses
// that blur too.
struct GcCopyFilterSwitch {
    std::uint32_t branch = 0;  // the `beq` to turn into a `b`; 0: not found
    std::uint32_t sharp = 0;   // where it goes: the sharp weights
};

// The one GXSetCopyFilter in `text`; branch 0 when there is none or more
// than one.
GcCopyFilterSwitch find_gc_copy_filter_switch(const std::vector<CodeRange>& text);

}  // namespace riftwii
