// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/symsearch.hpp"

// A GameCube game's picture settings that are found in its code before it
// starts (plugins/gamecube/gcnative.cpp applies them; the blob's VIConfigure hook
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

// GXInitTlutObj's palette address. Two SDK generations build the TLUT
// load word (the GXTlutObj word at offset 4, BP register 0x64) from the
// old contents of that word. The older one (Mario Party 4's SDK) reads
// the old word with `lwz rA,4(r3)`, then `rlwinm rA,rA,0,0,10` keeps the
// old word's top 11 bits and ORs the new address's low 21 in. The newer
// one (Sonic Gems Collection's SDK) instead follows `lwz rA,4(r3)` with
// `rlwimi rA,rB,27,11,31`, which keeps the old word's low 11 bits, then
// merges 0x64 into the top byte with `rlwimi rX,rY,24,0,7`. Those kept
// bits are whatever the caller's stack object held (Mario Party 4's copy
// keeps 0x54840014 there), so bits 8-10 of the old word stay in the
// palette's address, adding 0x04000000 to 0x1C000000 to it. A GameCube
// ignores address bits above its 24 MB, so the wrong address still
// reaches the right palette; Dolphin in GameCube mode masks them with
// 0x01FFFFFF for the same reason (its note names Wind Waker and Mario
// Kart: Double Dash as games that set those bits). In Wii mode the high
// bits are honoured, so the GPU loads the palette from MEM2 or unmapped
// memory and every colour-indexed (C4/C8/C14X2) texture comes out wrong:
// Mario Party 4 (GMPE01) shows silver/swirly stars, a green text window
// instead of dark blue, noise and streaks, while Dolphin's texture dumps
// prove the texture data itself is identical and only the palette
// differs; Sonic Gems Collection (G2XE8P) drew its Yes/No prompt without
// its capital letters. Nintendont replaces this function for every game
// for the same reason (named here only as provenance; the fix below is
// RiftWii's own).
struct GcTlutSites {
    std::vector<std::uint32_t> stale;  // words that keep the old word's bits: first shape's `rlwinm`, second's `lwz`
    std::vector<std::uint32_t> clean;  // already `li rA,0`: nothing to change
};

// The GXInitTlutObj sites in `text`, matched by instruction field so
// another compiler's registers still match; a shape missing any part is
// refused. First shape: an `lwz rA,4(r3)` followed within the next 2
// words by `rlwinm rA,rA,0,0,10` (same register, shift 0, mask 0-10),
// with an `oris rX,rX,0x6400` (any register, same source and target)
// and an `sth r6,8(r3)` within the following 12 words. Second shape: an
// `lwz rA,4(r3)` immediately followed by `rlwimi rA,rB,27,11,31` (same
// first register, any second), with an `rlwimi rX,rY,24,0,7` (any
// registers) and an `sth r6,8(r3)` within the next 8 words. The stale
// list holds the addresses to rebuild from the new pointer alone (the
// `rlwinm` for the first shape, the `lwz` itself for the second); the
// clean list holds the `li` addresses where that was already done.
GcTlutSites find_gc_tlut_sites(const std::vector<CodeRange>& text);

// `li rA,0` (`addi rA,0,0`) for the register the stale word keeps: what
// the loader writes over each stale address.
std::uint32_t gc_tlut_li_for(std::uint32_t stale_word);

// Patches the stale sites in place (`text` points at writable code, as
// the loader's sections are) by writing each `li`, and returns the
// addresses changed. Host tests use this; the loader itself finds the
// sites and writes through its own Code so the change is flushed with
// the rest.
std::vector<std::uint32_t> patch_gc_tlut(const std::vector<CodeRange>& text);

}  // namespace riftwii
