// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "gc_controls.h"

// The Wii Remote's buttons in a GameCube game, as the player may change
// them in text files on the SD card (the format is in docs/GUIDE.md):
//   sd:/riftwii/gc/controls/default.txt   every GameCube game
//   sd:/riftwii/gc/controls/<ID6>.txt     one game, over default.txt
// Each names a button and what it presses, under a heading for how the
// controller is held:
//   [remote]    # a Wii Remote alone, held sideways
//   2 = A
//   shake = B
// A line changes only the button it names; every other button keeps the
// built-in map (gc_controls_default) or what an earlier file gave it.
// The loader reads the files at a launch (plugins/gamecube/gcnative.cpp) and
// hands the map to the blob (runtime/gc/gc_bt.c).
namespace riftwii {

constexpr std::size_t kMaxGcControlsFile = 16 * 1024;  // far more than every button of all three kinds
constexpr std::size_t kMaxGcControlsLines = 400;
constexpr std::size_t kMaxGcControlsLine = 160;        // bytes; a longer line is skipped
constexpr std::size_t kMaxGcControlsTargets = 8;       // names after one '='
constexpr std::size_t kMaxGcControlsNotes = 24;        // log lines from one file

// The files a GameCube game's controls come from, in the order they apply.
std::vector<std::string> gc_controls_paths(const std::string& game_id);

// The built-in map: the Wii Remote sideways, with a Nunchuk and the
// Classic Controller as RiftWii has always set them, and no shake.
void gc_controls_default(gc_map& map);

// One file's text applied over `map`. `name` is the file as the notes call
// it. What the file asks for that RiftWii cannot do (an unknown heading or
// name, a button the controller does not have, a line too long) is left
// out and said in `notes`, a plain sentence each, at most
// kMaxGcControlsNotes of them. Returns how many buttons it changed.
std::size_t gc_controls_apply(const std::string& text, const std::string& name, gc_map& map,
                              std::vector<std::string>& notes);

// What a button presses, as a file would say it ("A StickUp", "nothing"),
// for the log.
std::string gc_controls_describe(std::uint32_t targets);

}  // namespace riftwii
