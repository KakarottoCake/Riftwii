// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/foldernames.hpp"

#include <algorithm>

#include "riftwii/checkedalloc.hpp"

namespace riftwii {

std::uint64_t folder_name_hash(const std::string& name) {
    std::uint64_t hash = 14695981039346656037ull;  // FNV-1a's offset basis
    for (const char c : name) {
        const unsigned char lowered =
            c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c - 'A' + 'a') : static_cast<unsigned char>(c);
        hash = (hash ^ lowered) * 1099511628211ull;
    }
    return hash;
}

bool FolderNames::add(const std::string& name) {
    if (hashes_.size() >= kMaxNames) return false;
    finished_ = false;
    return try_push_back(hashes_, folder_name_hash(name));
}

void FolderNames::finish() {
    std::sort(hashes_.begin(), hashes_.end());
    finished_ = true;
}

bool FolderNames::has(const std::string& name) const {
    const std::uint64_t hash = folder_name_hash(name);
    if (finished_) return std::binary_search(hashes_.begin(), hashes_.end(), hash);
    // Asked before finish(): still the right answer, only slower.
    return std::find(hashes_.begin(), hashes_.end(), hash) != hashes_.end();
}

void FolderNames::clear() {
    std::vector<std::uint64_t>().swap(hashes_);
    finished_ = false;
}

}  // namespace riftwii
