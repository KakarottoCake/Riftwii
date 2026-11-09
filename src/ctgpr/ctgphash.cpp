// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgphash.hpp"

#include <cstring>

#include "riftwii/ctgpdsa.hpp"
#include "riftwii/sha1.hpp"

namespace riftwii {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

constexpr std::size_t kHeader = 0x2C;   // the part the signature covers
constexpr std::size_t kBody = 0x60;

}  // namespace

bool ctgp_load_hash_list(const CtgpBytes& file, const std::string& package, std::uint32_t version,
                         CtgpHashList& out, std::string& error, const CtgpDsaKey* key) {
    if (file.size() < kBody + 12 || package.empty() || package.size() > 8) {
        error = "the hash list is too short";
        return false;
    }
    const std::uint8_t* d = file.data();
    // The list's DSA signature (length at 0x2C, DER signature at 0x30) is
    // checked against CTGP-R's public list-signing key, over the header the
    // channel signs (bytes 0x00..0x2C). In the native launch the channel never
    // runs, so this is the only thing that proves the list was not hand-edited.
    // With a trusted list, the body's own SHA-1 and each file's recorded digest
    // (which the game compares) then enforce every file's bytes.
    const std::uint32_t sig_length = be32(d + 0x2C);
    if (sig_length == 0 || sig_length > 0x30 || 0x30 + sig_length > kBody) {
        error = "the hash list's signature has a bad length";
        return false;
    }
    if (!ctgp_dsa_verify(key ? *key : ctgp_signing_key(), d, kHeader, d + 0x30, sig_length)) {
        error = "the hash list's signature does not verify";
        return false;
    }
    if (be32(d) != 0x48616830u || be32(d + 4) != file.size() || be32(d + 8) != 0) {
        error = "the hash list's header is wrong";
        return false;
    }
    char name[8] = {};
    std::memcpy(name, package.data(), package.size());
    if (std::memcmp(d + 0x20, name, 8) != 0) {
        error = "the hash list is for another package";
        return false;
    }
    if (be32(d + 0x28) != version) {
        error = "the hash list is for another version of the package";
        return false;
    }
    const Sha1Digest body_digest = sha1(d + kBody, file.size() - kBody);
    if (std::memcmp(body_digest.data(), d + 0x0C, 20) != 0) {
        error = "the hash list's body does not match its digest";
        return false;
    }
    CtgpHashList list;
    list.body_.assign(file.begin() + kBody, file.end());
    // File entries hold the position of their digest where a disc offset would
    // be, and its length (20) where the size would be.
    if (!Fst::parse(list.body_.data(), list.body_.size(), false, list.tree_, error)) {
        error = "the hash list's table is wrong: " + error;
        return false;
    }
    for (const FstEntry& e : list.tree_.entries()) {
        if (!e.is_directory) ++list.files_;
    }
    out = std::move(list);
    return true;
}

bool CtgpHashList::find(const std::string& path, std::array<std::uint8_t, 20>& digest) const {
    const auto it = direct_.find(path);
    if (it != direct_.end()) {
        digest = it->second;
        return true;
    }
    const std::uint32_t index = tree_.find(path, false);
    if (index == Fst::npos) return false;
    const FstEntry& e = tree_.entries()[index];
    if (e.is_directory || e.size != 20 || e.offset > body_.size() || body_.size() - e.offset < 20) return false;
    std::memcpy(digest.data(), body_.data() + e.offset, 20);
    return true;
}

bool CtgpHashList::contains_digest(const std::array<std::uint8_t, 20>& digest) const {
    // The test-only direct lists hold their digests by value.
    for (const auto& named : direct_) {
        if (named.second == digest) return true;
    }
    // A real list points each file entry at a 20-byte digest inside the body.
    for (const FstEntry& e : tree_.entries()) {
        if (e.is_directory || e.size != 20 || e.offset > body_.size() || body_.size() - e.offset < 20) continue;
        if (std::memcmp(body_.data() + e.offset, digest.data(), 20) == 0) return true;
    }
    return false;
}

}  // namespace riftwii
