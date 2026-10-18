// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct gc_crash_strip;  // runtime/gc/gc_crash.h

namespace riftwii {

// A GameCube game's crash, from the trace its loader's runtime writes
// (sd:/riftwii/gc/trace.bin, runtime/gc/gc_rt.h's records): the menu turns
// it into sd:/riftwii/gamecrash.txt at its next start, for a problem
// report, as it does a Wii game's crash record.

// The newest crash record in `trace` as text; false when the trace holds
// no crash (or is not a trace).
bool describe_gc_crash(const std::vector<std::uint8_t>& trace, const std::string& game_id, std::string& text);

// The Wii Remotes in the last GameCube game, from the trace's newest record
// (runtime/gc/gc_bt.h's progress, gc_rt.h's GC_TR_BT_* words), as plain
// sentences for a problem report: how far the Bluetooth got and where it
// stopped. False when `trace` holds no record.
bool describe_gc_remotes(const std::vector<std::uint8_t>& trace, std::string& text);

// The crash screen the menu draws for the blob (runtime/gc/gc_crash.h):
// a card with the header and the hint, and a strip of pieces the blob
// puts the dump together from at the crash.

// A fixed piece's text: a hex digit, "x", an exception's name or a part
// of a register's name; empty for the menu's own words (from
// GC_CRASH_PIECE_TEXT on: the game's ID, the version, "Called from").
std::string gc_crash_piece_text(unsigned piece);

// 8-bit coverage to the card's 2 bits a pixel (four levels; the leftmost
// of four pixels in a byte's top bits). `packed` holds (pixels + 3) / 4.
void gc_crash_pack(const std::uint8_t* coverage, std::size_t pixels, std::uint8_t* packed);

// Adds a piece drawn at 8-bit coverage (`width` x GC_CRASH_STRIP_H, its
// pen starting at x 0 and moving `advance`) to the strip, at `next`, which
// moves past it. False when the strip has no room left: the piece stays
// missing (advance 0), so the line goes on without it.
bool gc_crash_strip_add(gc_crash_strip& strip, unsigned piece, const std::uint8_t* coverage, unsigned width,
                        unsigned advance, unsigned& next);

}  // namespace riftwii
