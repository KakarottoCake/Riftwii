// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "riftwii/launch.hpp"

namespace riftwii {

// The packs a GameCube game keeps across a switch to another program on
// its disc. RiftWii carries out the switch by starting the image again
// with that program (runtime/gc's gc_on_boot_dol, sd:/riftwii/gc/exec.txt,
// which names only the image), so the launch writes its packs and their
// choices to sd:/riftwii/gc/exec-packs.txt and the restart reads them
// back: the second program gets the same files and memory patches as the
// first.
//
// The file: the game and the image it is for, then each pack and its
// choices, one per line:
//
//   game=GLME01
//   path=sd:/games/GLME01/game.iso
//   xml=sd:/riivolution/mod.xml
//   choice=Section/Option<TAB>Choice
//
constexpr const char* kGcExecPacksFile = "sd:/riftwii/gc/exec-packs.txt";

// The file's text for `packages` of the game `game` at `path`. False when a
// name holds a line break or a tab, which the file can't carry.
bool gc_exec_packs_text(const std::string& game, const std::string& path, const std::vector<PackageChoices>& packages,
                        std::string& text);

// The packs back from `text`, when it was written for `game` at `path`;
// false (and `packages` empty) for another game or image, or a file that
// is not whole: a pack's choice before any pack, more than kMaxGcExecPacks
// packs or kMaxGcExecChoices choices, an unknown line.
bool gc_exec_packs_parse(const std::string& text, const std::string& game, const std::string& path,
                         std::vector<PackageChoices>& packages);

constexpr std::size_t kMaxGcExecPacks = 64;
constexpr std::size_t kMaxGcExecChoices = 4096;
// The longest file the restart reads.
constexpr std::size_t kMaxGcExecPacksBytes = 256u << 10;

}  // namespace riftwii
