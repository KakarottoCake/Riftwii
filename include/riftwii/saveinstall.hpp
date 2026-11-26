// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include "riftwii/gccard.hpp"
#include "riftwii/savelist.hpp"

namespace riftwii {

// Read a loose GCI or the single GCI named by member in a pinned ZIP.
// The caller checks the download's size and SHA-256 before calling this.
bool save_read_gci(const SaveListing& save, const ZipReadAt& read, ZipInflateStream* inflater,
                   std::vector<std::uint8_t>& gci, std::string& error);

// Work on a copy; failure (including a full card) leaves card unchanged.
// Only a matching game/maker/file-name entry is replaced. Other saves,
// including a second save belonging to this game, are retained.
bool save_apply_gci(const SaveListing& save, const std::vector<std::uint8_t>& gci,
                     std::vector<std::uint8_t>& card, bool replace, std::string& error);

// The two filesystem operations needed to publish a staged file or folder.
// A mock can fail either move to check rollback without touching real saves.
struct SavePublishOps {
    std::function<bool(const std::string&)> exists;
    std::function<bool(const std::string&, const std::string&)> move;
};
bool save_publish(const std::string& staged, const std::string& target, const std::string& backup,
                   bool replace, const SavePublishOps& ops, std::string& error);

using SaveInstallProgress = std::function<bool(std::uint64_t, std::uint64_t)>;
// root is a checked, writable emulated NAND mount, never a real NAND path.
// The only archive destination is root + save_wii_title_path(game_id) +
// /data. A backup is a uniquely named sibling of data, retained indefinitely.
// An abandoned staging folder is retained and reported, never deleted blindly.
// title_id is the game's full title ID from its TMD when the menu knows it
// (0 when not: a disc or an RVZ image); see save_wii_title_type.
bool save_install_wii_zip(const SaveListing& save, const std::string& root, const ZipReadAt& read,
                           ZipInflateStream* inflater, bool replace, const std::string& timestamp,
                           std::string& backup, std::string& error,
                           const SaveInstallProgress& progress = {}, std::uint64_t title_id = 0);

// The title type the game reads its save from, in this order: the TMD's
// title ID when it belongs to this game, a save already under 00010004 (and
// none under 00010000) in the emulated NAND at root, Mario Kart Wii (whose
// launch uses 00010004, see riftwii/ctgprsave.hpp), else 00010000.
std::uint32_t save_wii_title_type(const std::string& root, const std::string& game_id, std::uint64_t title_id);

}  // namespace riftwii
