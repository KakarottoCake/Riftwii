// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgphashlookup.hpp"

#include <cstring>

#include "riftwii/fst.hpp"
#include "riftwii/sha1.hpp"

namespace riftwii {

namespace {

constexpr std::size_t kBody = 0x60;
constexpr std::uint32_t kMagic = 0x48616830u;  // "Hah0"

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

}  // namespace

bool ctgp_hash_list_digest(const std::uint8_t* list, std::size_t size, const std::string& package,
                           const std::string& path, std::array<std::uint8_t, 20>& digest, std::string& error,
                           bool* unlisted) {
    if (unlisted != nullptr) *unlisted = false;
    if (list == nullptr || size < kBody + 12 || package.empty() || package.size() > 8) {
        error = "the hash list is too short";
        return false;
    }
    if (be32(list) != kMagic || be32(list + 4) != size || be32(list + 8) != 0) {
        error = "the hash list's header is wrong";
        return false;
    }
    char name[8] = {};
    std::memcpy(name, package.data(), package.size());
    if (std::memcmp(list + 0x20, name, 8) != 0) {
        error = "the hash list is for another package";
        return false;
    }
    const Sha1Digest body = sha1(list + kBody, size - kBody);
    if (std::memcmp(body.data(), list + 0x0C, 20) != 0) {
        error = "the hash list's body does not match its digest";
        return false;
    }
    // The body is a file system table whose file entries hold the position of
    // their 20-byte digest where a disc offset would be.
    Fst tree;
    if (!Fst::parse(list + kBody, size - kBody, false, tree, error)) {
        error = "the hash list's table is wrong: " + error;
        return false;
    }
    const std::uint32_t index = tree.find(path, false);
    if (index == Fst::npos) {
        error = "the hash list does not name " + path;
        if (unlisted != nullptr) *unlisted = true;
        return false;
    }
    const FstEntry& e = tree.entries()[index];
    if (e.is_directory || e.size != 20 || e.offset > size - kBody || size - kBody - e.offset < 20) {
        error = "the hash list's entry for " + path + " is wrong";
        return false;
    }
    std::memcpy(digest.data(), list + kBody + e.offset, 20);
    return true;
}

}  // namespace riftwii
