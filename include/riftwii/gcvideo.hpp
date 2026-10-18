// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
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

// The GameCube OS keeps the console's settings (the SRAM: language,
// progressive scan, sound...) in a copy in memory, read once at start-up,
// and its getters read that copy after locking it: `lis rA,hi`, `addi
// rB,rA,lo` (the copy), then the lock word at 0x48 and `addi r4,rB,0x48`.
// The settings are bytes in the copy (YAGCD's SRAM layout): the language
// at 0x12, the flags at 0x13 with progressive scan in bit 7, and at 0x11
// the 60 Hz setting of a European console in bit 6 (libogc's
// SYS_GetEuRGB60 reads it there).
//
// The getters (OSGetLanguage, OSGetProgressiveMode, OSGetEuRgb60Mode)
// each read their byte from that register: the loads found here, for the
// loader to turn into a constant. The copy and the console's SRAM are
// never changed, so nothing the game writes back to the SRAM later (it
// does for its own 480p question) carries a choice made here.
//
// European games ask the 60 Hz question in one of the two flags. Later
// ones read bit 6 at 0x11; Luigi's Mansion's 2001 SDK has no such getter,
// and its own 60 Hz choice (a table of its PAL and 60 Hz modes) reads
// bit 7 at 0x13 through OSGetProgressiveMode instead.
struct GcSramReads {
    std::uint32_t copy = 0;                  // the settings' copy in memory
    std::vector<std::uint32_t> language;     // lbz rX,0x12(copy)
    std::vector<std::uint32_t> progressive;  // lbz rX,0x13(copy), then its bit 7 taken (>> 7 & 1)
    std::vector<std::uint32_t> sixty_hz;     // lbz rX,0x11(copy), then its bit 6 taken (>> 6 & 1)
};

// The copy most getters lock, and the reads of it; copy 0 when none.
GcSramReads find_gc_sram_reads(const std::vector<CodeRange>& text);

// Picture width: the blob's vi_width for a riftwii/videopatch.hpp width
// name (gc_picture_width's): 0 the game's own, else as gc_video.h's
// gc_video_pick takes it, which draws the picture that wide in the TV
// line, centred.
std::uint32_t gc_vi_width(const std::string& name);

}  // namespace riftwii
