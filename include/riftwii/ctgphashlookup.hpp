// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// One file's expected digest from a CTGP-R hash list
// (jelly.bin's /PACKAGES/HASH/<PACKAGE>.HSH, laid out as riftwii/ctgphash.hpp
// describes). The digest is the SHA-1 of the file as the pack stores it.
//
// The updater uses this to check every file it rebuilds before writing it
// into the pack: a file whose stored bytes do not match the list is never
// written. This reads the list's header, package name and body digest; it
// does not verify the list's signature (that code is not in the DOL), so it
// is a correctness check against the card's own list, not a security check.
namespace riftwii {

bool ctgp_hash_list_digest(const std::uint8_t* list, std::size_t size, const std::string& package,
                           const std::string& path, std::array<std::uint8_t, 20>& digest, std::string& error);

}  // namespace riftwii
