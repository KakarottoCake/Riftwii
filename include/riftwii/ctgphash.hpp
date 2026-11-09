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
// (at most 48), a DER DSA signature (over the header, taken as the digest),
// then at 0x60 the body: a file system table whose file entries give the
// position of a 20-byte digest inside the body.
namespace riftwii {

class CtgpHashList {
public:
    // The expected digest of the file at `path` (as the package's own file
    // list names it, "/Scene/UI/HS/HealthSafety.szs"). False when the list
    // does not name it.
    bool find(const std::string& path, std::array<std::uint8_t, 20>& digest) const;
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
                                    CtgpHashList&, std::string&, const CtgpDsaKey*);
    CtgpBytes body_;
    Fst tree_;
    std::size_t files_ = 0;
    std::map<std::string, std::array<std::uint8_t, 20>> direct_;
};

// Checks `file` as the list of package `package` (at most 8 characters) in the
// version `version` (from PACKAGES.BIN): the DSA signature against CTGP-R's
// public key, then the magic, the size, the name, the version and the body's
// SHA-1. On any failure `error` says which and `out` is untouched. `key` is
// only for tests; the default is CTGP-R's own public list-signing key.
bool ctgp_load_hash_list(const CtgpBytes& file, const std::string& package, std::uint32_t version,
                         CtgpHashList& out, std::string& error, const CtgpDsaKey* key = nullptr);

}  // namespace riftwii
