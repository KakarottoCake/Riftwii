// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "riftwii/apply.hpp"
#include "riftwii/fst.hpp"
#include "riftwii/patch.hpp"

namespace riftwii {

// Turns a plan's <folder> patches into <file> patches against a disc's FST,
// keeping document order with the plan's own <file> patches (memory and
// savegame steps are left to their own runtimes). Semantics, from the
// public patch-format documentation:
//   - A rooted `disc` ("/Stage") pairs every file of the external folder
//     with the disc file of the same name in that directory, matched
//     case-insensitively and reported with the disc's own spelling; with
//     `recursive` (the default) subfolders that exist on the disc are
//     walked the same way. A file with no disc counterpart is skipped, or
//     with `create` becomes a created file (and its missing directories
//     with it). A rooted disc folder that does not exist is skipped unless
//     `create` is set; multi-region packages use this for other regions.
//   - An empty `disc` is a filename search: every file of the external
//     folder replaces every disc file of that name, wherever it is;
//     subfolders are not entered. A bare name (not rooted) is treated the
//     same way, since the documentation gives it no folder meaning.
//   - `resize` and `length` carry over to each file patch.
// External entries are visited in name order (ASCII case folded) so the
// result does not depend on the card's directory order. `notes` receives
// one line per folder saying what it expanded to.
bool expand_plan(const Plan& plan, const Fst& fst, ContentProvider& provider, std::vector<FilePatch>& out,
                 std::vector<std::string>& notes, std::string& error);

// User-visible diagnosis for a package whose plan produced nothing at all.
// `folder_notes` are the per-folder summary lines expand_plan appended for
// this package ("<folder ..>: R replaced, C created, S skipped"). A plan
// with no steps means no choice was on; folders that matched nothing name
// themselves so a misplaced external folder or a missing create="true" is
// fixable instead of looking like a wrong-game refusal.
std::string describe_empty_plan(const std::string& xml_sd_path, const std::string& game_id, const Plan& plan,
                                const std::vector<std::string>& folder_notes);

}  // namespace riftwii
