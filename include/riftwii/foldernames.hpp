// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The names of the files in one folder, listed once, to answer "is this
// file there?" for many files without asking the card each time.
//
// Home asks that for every game it lists: is its cover stored, was it
// missed, is a second set chosen, and the same for its box. Each question
// was a stat(), which walks the folder from its first entry, so a view of a
// few hundred games walked the covers' folder over a thousand times and
// held the menu for seconds (All games after the Mods view). The folder
// is walked once instead, and the questions are answered from its names.
//
// Only the names are kept, as 64-bit hashes (eight bytes a file whatever
// its name's length: the menu's heap is small), compared without regard
// to ASCII case, as FAT compares names. Two different names sharing a hash
// would make a missing file look present; with 64 bits that does not
// happen among a folder's few thousand names.
namespace riftwii {

class FolderNames {
public:
    // A folder with more files than this is not kept: its listing would
    // take 128 KiB or more, and the caller asks the card file by file, as
    // it did before.
    static constexpr std::size_t kMaxNames = 16384;

    // One name from the folder's listing. False when the folder has too
    // many files, or there is no memory for another name: the listing is
    // then of no use and must not be asked.
    bool add(const std::string& name);
    // After the last add(): sorts the names, so has() is a binary search.
    void finish();
    // Whether the folder has a file of this name (ASCII case ignored).
    bool has(const std::string& name) const;
    std::size_t size() const { return hashes_.size(); }
    void clear();

private:
    std::vector<std::uint64_t> hashes_;
    bool finished_ = false;
};

// The hash FolderNames keeps for a name: FNV-1a over its bytes, ASCII
// letters lowered first.
std::uint64_t folder_name_hash(const std::string& name);

}  // namespace riftwii
