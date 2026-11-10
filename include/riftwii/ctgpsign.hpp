// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "riftwii/ctgpfields.hpp"

// RiftWii supplies an unusable key instead of the card's ghost-signing key.
//
// The channel hands the game a DSA private key (each card's own, kept on
// jelly.bin) that CTGP-R's code signs every saved ghost with, plus a tag made
// from the console's wireless MAC. A launch from RiftWii must not sign
// ghosts: CTGP's ghost server has to reject what it saves. RiftWii does not read the card's key or the MAC at all.
//
// The game still needs a key in that place: with none, its ghost signer reads
// a null pointer (BOAT 0x81779418) and CTGP's crash handler reboots it. So the
// launch hands over a key RiftWii made for this, laid out as the channel lays
// out a real one (OpenSSL's DSA and BIGNUM structures, the private value's
// words masked), with generator 2 and deliberately mismatched public and
// private values. This is intended to prevent accepted signatures.
// The 20 bytes that name the card's key (from BANNER.BIN) and the console tag
// stay zero. tests/ctgpsign_tests.cpp checks that one signature made with
// this key fails our DSA verifier under the supplied public value and CTGP's
// list-signing public key (not the ghost server's key). It does not run the
// in-game signer or prove universal rejection. Saving/replaying with this key and
// rejection by the server have not been confirmed on a real Wii.
namespace riftwii {

constexpr std::uint32_t kCtgpConsoleTag = 0x816ff4e8;    // 2 words, left 0
constexpr std::uint32_t kCtgpBannerDigest = 0x816ff4f0;  // 20 bytes, left 0
constexpr std::uint32_t kCtgpSigningKey = 0x816ff52c;    // the key's words, up to 0x816ff9d4

// The loader words of the unusable signing key, ready to place.
std::vector<CtgpWordWrite> ctgp_unsigned_key_writes();

// The key's numbers, big endian, for the tests.
struct CtgpUnsignedKey {
    const std::uint8_t* p;  // 64 bytes
    const std::uint8_t* q;  // 20 bytes
    const std::uint8_t* g;  // 64 bytes
    const std::uint8_t* y;  // 64 bytes
    const std::uint8_t* x;  // 20 bytes
};
CtgpUnsignedKey ctgp_unsigned_key();

}  // namespace riftwii
