// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "riftwii/apply.hpp"
#include "riftwii/fst.hpp"
#include "riftwii/patch.hpp"

namespace riftwii {

enum class MissingModInputKind { File, Memory, Folder };

struct MissingModInput {
    std::string pack;      // XML path, so equally named packs remain distinguishable
    std::string external;
    MissingModInputKind kind = MissingModInputKind::File;
};

// Missing external files only matter when their selected patch replaces a
// file on this disc. Other-region targets remain ordinary skips, and so does
// a file the patch would only add (create="true" with nothing on the disc
// there): Riivolution skips it without a word, and packs list such files for
// content they don't ship, like Retro Rewind's Korean font.
bool file_patch_replaces_disc_file(const FilePatch& patch, const Fst& fst);
// "not on the USB drive" for a path on usb:, "not on the card" for any
// other (the SD card), for the notes on a pack's missing files and folders.
std::string not_on_device(const std::string& external);
// Whether a note says a pack's file or folder was missing (not_on_device's
// words), so the caller can point at what the card or drive holds instead.
bool note_names_missing(const std::string& note);

// Keeps one entry per pack/path/kind. Returns false if the list cannot grow.
bool record_missing_mod_input(std::vector<MissingModInput>& out, const std::string& pack,
                              const std::string& external, MissingModInputKind kind);

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
//   - An external folder that is not on the card skips the patch (as
//     Riivolution does) with a note; any other listing failure is an error.
// External entries are visited in name order (ASCII case folded) so the
// result does not depend on the card's directory order. `notes` receives
// one line per folder saying what it expanded to. `missing_folders`, when
// given, receives each external folder that is not there although this
// disc would take its files (its disc folder exists, or it is a by-name
// search): a folder for another region's directory, or one that would only
// add files (it creates its disc folder, or creates into one that holds no
// file it could replace), is not counted, as file_patch_replaces_disc_file
// leaves out such files.
bool expand_plan(const Plan& plan, const Fst& fst, ContentProvider& provider, std::vector<FilePatch>& out,
                 std::vector<std::string>& notes, std::string& error,
                 std::vector<std::string>* missing_folders = nullptr);

// User-visible diagnosis for a package whose plan produced nothing at all.
// `folder_notes` are the per-folder summary lines expand_plan appended for
// this package ("<folder ..>: R replaced, C created, S skipped"). A plan
// with no steps means no choice was on; folders that matched nothing name
// themselves so a misplaced external folder or a missing create="true" is
// fixable instead of looking like a wrong-game refusal.
std::string describe_empty_plan(const std::string& xml_sd_path, const std::string& game_id, const Plan& plan,
                                const std::vector<std::string>& folder_notes);

}  // namespace riftwii
