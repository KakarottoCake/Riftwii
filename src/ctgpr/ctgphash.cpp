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

constexpr std::size_t kHeader = 0x2C;   // passed raw to DSA (which truncates to 20 bytes)
constexpr std::size_t kBody = 0x60;

const std::uint8_t* reference_body(const std::string& package, std::uint32_t version) {
    // Small facts from locally verified lists, not signatures or key data.
    // The version label itself is unsigned. Bind each supported identity to
    // its complete body digest, so an older signed list cannot be relabelled.
    static constexpr std::uint8_t core_body[20] = {
        0x40, 0x0C, 0x76, 0x45, 0xC7, 0xC8, 0x02, 0x1E, 0xD6, 0x76,
        0x56, 0x00, 0xF0, 0x51, 0xF8, 0x3B, 0x07, 0x04, 0x7C, 0x0E,
    };
    static constexpr std::uint8_t ctgpr_body[20] = {
        0xA2, 0xC2, 0x58, 0x2E, 0x1D, 0xF3, 0xD1, 0x44, 0x13, 0xD8,
        0xBA, 0x40, 0xD3, 0xFE, 0x0A, 0x7C, 0xA9, 0x7E, 0x1F, 0x65,
    };
    if (package == "CORE" && version == 0x01030412) return core_body;
    // CTGP bumped PACKAGES.BIN's CTGPR version from 0x01031192 (build 1192) to
    // 0x01031196 (build 1196) without re-versioning CTGPR.HSH: its body, and its
    // own version label (still 0x01031192), are unchanged. Bind either
    // PACKAGES.BIN version to that one body, so both a 1192 card and a 1196 card
    // are accepted; the body-digest match still proves the list's identity, so
    // the extra key is a correctness fix with no loss of integrity (a body that
    // is not this one is refused whichever version keyed the lookup).
    if (package == "CTGPR" && (version == 0x01031192 || version == 0x01031196)) return ctgpr_body;
    return nullptr;
}

}  // namespace

bool ctgp_core_digest_matches(const CtgpHashList& hashes, const std::string& file,
                              const std::array<std::uint8_t, 20>& digest) {
    if (file == "BOOT.BIN") return hashes.matches_digest("/debug/bigfile1.big", digest);
    if (file == "BOAT.BIN") return hashes.matches_digest("/debug/bigfile7.big", digest);
    if (file == "BEAT.BIN") return hashes.matches_digest("/debug/bigfile6.big", digest);
    return false;
}

bool ctgp_core_code_is_tested(const std::string& file, const std::array<std::uint8_t, 20>& digest) {
    // SHA-1s of CORE 0x01030412's code as read from the package disk, the
    // same on a card from September 2026 and one CTGP-R updated in November.
    static constexpr std::uint8_t boot[20] = {
        0x79, 0x0E, 0x0F, 0xED, 0x88, 0x66, 0x36, 0x6D, 0x7B, 0xA0,
        0x07, 0xC6, 0xFF, 0x5E, 0x7A, 0x0D, 0x66, 0x67, 0x2F, 0x18,
    };
    static constexpr std::uint8_t boat[20] = {
        0x21, 0x00, 0x02, 0x02, 0xCD, 0x21, 0xAD, 0x99, 0x3E, 0x2C,
        0x8F, 0xBA, 0x1D, 0x7A, 0x8E, 0x46, 0x26, 0xAE, 0x68, 0xC6,
    };
    static constexpr std::uint8_t beat[20] = {
        0x9F, 0x4D, 0xA5, 0x13, 0x97, 0x99, 0xD8, 0xA9, 0x36, 0x05,
        0xC6, 0xF7, 0x98, 0xCE, 0xE3, 0xCC, 0x3E, 0x92, 0x25, 0x43,
    };
    const std::uint8_t* want = file == "BOOT.BIN" ? boot : file == "BOAT.BIN" ? boat : file == "BEAT.BIN" ? beat : nullptr;
    return want && std::memcmp(digest.data(), want, 20) == 0;
}

bool ctgp_has_hash_reference(const std::string& package, std::uint32_t version) {
    return reference_body(package, version) != nullptr;
}

bool ctgp_load_hash_list(const CtgpBytes& file, const std::string& package, std::uint32_t version,
                         CtgpHashList& out, std::string& error, const CtgpDsaKey* key,
                         const std::array<std::uint8_t, 20>* reference) {
    if (file.size() < kBody + 12 || package.empty() || package.size() > 8) {
        error = "the hash list is too short";
        return false;
    }
    const std::uint8_t* d = file.data();
    // The channel passes the raw 0x2C-byte header to DSA_verify; its DSA
    // method truncates that to q's 20 bytes. The name/version are outside
    // that range. After verification, bind them to a known body below.
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
    const Sha1Digest body_digest = sha1(d + kBody, file.size() - kBody);
    if (std::memcmp(body_digest.data(), d + 0x0C, 20) != 0) {
        error = "the hash list's body does not match its digest";
        return false;
    }
    // The signed reference for this (package, PACKAGES.BIN version) proves the
    // list's identity by its whole body, so its own version label (0x28) does
    // not have to equal `version`. (`reference` is a test-only override.)
    const std::uint8_t* const expected_body = reference ? reference->data() : reference_body(package, version);
    const bool known_identity = expected_body && std::memcmp(body_digest.data(), expected_body, 20) == 0;
    // Only when no reference vouches for the body do we fall back to the list's
    // own version label, which must then equal the requested version. This is
    // the test/dev path; a current production card reaches known_identity above
    // even though its CTGPR.HSH label (0x01031192) lags PACKAGES.BIN (0x01031196).
    if (!known_identity && be32(d + 0x28) != version) {
        error = "the hash list is for another version of the package";
        return false;
    }
    // CTGPR and CORE are accepted on CTGP-R's own signature with their label
    // equal to PACKAGES.BIN's version (checked above), whatever their body:
    // that is the rule the channel itself applies, and CTGP-R re-issues these
    // lists without changing the version number (a card updated in Nov 2026
    // has new CTGPR.HSH and CORE.HSH bodies under the same labels). A known
    // body still vouches for a list whose label lags PACKAGES.BIN
    // (known_identity). CORE's fixed addresses are protected by its code
    // itself (ctgp_core_code_is_tested), not by the list's body. The signature
    // has been checked by now, with whichever key is in use (CTGP-R's own
    // unless a test gives another), so this does not depend on `key`.
    if (!known_identity && package != "CTGPR" && package != "CORE" && !key) {
        error = "the signed hash-list body is not a known reference for this package version";
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

bool CtgpHashList::matches_digest(const std::string& path, const std::array<std::uint8_t, 20>& digest) const {
    std::array<std::uint8_t, 20> expected{};
    return find(path, expected) && expected == digest;
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
