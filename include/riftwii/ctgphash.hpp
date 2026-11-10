// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <map>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/ctgparena.hpp"
#include "riftwii/ctgpdsa.hpp"
#include "riftwii/fst.hpp"

// CTGP-R's signed lists of file digests ("efb:/packages/HASH/<PACKAGE>.HSH").
// Each file's expected SHA-1 is written into that file's open-file record,
// where the game's own code compares it with what it reads. Loading a list
// here does the checks the channel does before it trusts a list: it verifies
// the list's DSA signature against CTGP-R's public list-signing key
// (riftwii/ctgpdsa.hpp), then the structural fields (the magic, the size, the
// package name, the version and the body's SHA-1). The signature check matters
// because in the native launch the CTGP-R channel never runs, so nothing else
// would catch a hand-edited list; with a verified list, the game's own
// open-file comparison then enforces every recorded digest.
//
// Layout: "Hah0", the file's size, 0, the SHA-1 of the body (20 bytes),
// the package name (8 bytes), the package's version, the signature's length
// (at most 48), a DER DSA signature (over the first 20 raw header bytes),
// then at 0x60 the body: a file system table whose file entries give the
// position of a 20-byte digest inside the body.
namespace riftwii {

// Whether RiftWii knows a hash-list body for this package version. The
// launch uses it for CORE as "the core this launch is made for"; a known body
// also lets a list whose label lags PACKAGES.BIN through (ctgp_load_hash_list).
bool ctgp_has_hash_reference(const std::string& package, std::uint32_t version);

class CtgpHashList {
public:
    // The expected digest of the file at `path` (as the package's own file
    // list names it, "/Scene/UI/HS/HealthSafety.szs"). False when the list
    // does not name it.
    bool find(const std::string& path, std::array<std::uint8_t, 20>& digest) const;

    // True when any entry has this digest. This establishes membership only,
    // not a file's role; use matches_digest for directly loaded core files.
    bool contains_digest(const std::array<std::uint8_t, 20>& digest) const;

    bool matches_digest(const std::string& path, const std::array<std::uint8_t, 20>& digest) const;

    std::size_t file_count() const { return files_ + direct_.size(); }

    // A list made of the given digests, with no signature behind it: for tests
    // of the code that uses a list. Lists the launch uses come from
    // ctgp_load_hash_list only.
    static CtgpHashList unverified_for_tests(const std::map<std::string, std::array<std::uint8_t, 20>>& digests) {
        CtgpHashList l;
        l.direct_ = digests;
        return l;
    }

private:
    friend bool ctgp_load_hash_list(const CtgpBytes&, const std::string&, std::uint32_t,
                                    CtgpHashList&, std::string&, const CtgpDsaKey*,
                                    const std::array<std::uint8_t, 20>*);
    CtgpBytes body_;
    Fst tree_;
    std::size_t files_ = 0;
    std::map<std::string, std::array<std::uint8_t, 20>> direct_;
};

// Bind a directly loaded CORE file to its role in the pinned CORE release.
// The debug-name mapping was checked against that release's as-read files.
bool ctgp_core_digest_matches(const CtgpHashList& hashes, const std::string& file,
                              const std::array<std::uint8_t, 20>& digest);

// True when `digest` is the SHA-1 of the core code file `file` (BOOT.BIN,
// BOAT.BIN or BEAT.BIN) of the CORE release this launch is made for. The
// launch's fixed addresses belong to that code, so it is pinned by the code
// itself; CORE.HSH, which CTGP-R re-issues, need only be signed and vouch for it.
bool ctgp_core_code_is_tested(const std::string& file, const std::array<std::uint8_t, 20>& digest);

// Checks `file` as the list of package `package` (at most 8 characters) in the
// version `version` (from PACKAGES.BIN): the DSA signature against CTGP-R's
// public key, then the magic, the size, the name and the body's SHA-1. The
// list's version label (at 0x28) must equal `version`, as the channel
// requires, unless its body is one RiftWii knows for that version (an earlier
// 1196 card kept CTGPR.HSH labelled 0x01031192). CTGPR and CORE lists are
// accepted whatever their body: CTGP-R re-signs them under the same version.
// The name and label are not signed, so they prove neither which package a
// signed list was made for nor how recent it is: an older signed list may be
// accepted, and it still only vouches for the files CTGP-R signed in it.
// CORE's code is pinned separately (ctgp_core_code_is_tested).
// On any failure `error` says which and `out` is untouched. `key` and
// `reference` are only for tests (`key` default is CTGP-R's own public
// list-signing key; `reference`, when set, stands in for the built-in
// body/version binding so the relaxation can be exercised without shipping
// real list bytes).
bool ctgp_load_hash_list(const CtgpBytes& file, const std::string& package, std::uint32_t version,
                         CtgpHashList& out, std::string& error, const CtgpDsaKey* key = nullptr,
                         const std::array<std::uint8_t, 20>* reference = nullptr);

}  // namespace riftwii
