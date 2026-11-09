// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

// DSA signature verification, for CTGP-R's signed lists of file digests
// (riftwii/ctgphash.hpp). Verification only. The public key the channel
// checks its lists with is CTGP-R's, read at run time from tables.bin
// (riftwii/ctgptables.hpp), never part of RiftWii's source. Plain
// schoolbook arithmetic: the numbers are 512 bits.
namespace riftwii {

struct CtgpDsaKey {
    std::uint8_t p[64];   // prime modulus, big endian
    std::uint8_t q[20];   // prime order of the subgroup
    std::uint8_t g[64];   // generator
    std::uint8_t y[64];   // public value
};

// True when `signature` (DER: SEQUENCE { INTEGER r, INTEGER s }) is a valid
// DSA signature of `digest` under `key`. Only the leftmost 20 bytes of the
// digest count, as in DSA_verify.
bool ctgp_dsa_verify(const CtgpDsaKey& key, const std::uint8_t* digest, std::size_t digest_length,
                     const std::uint8_t* signature, std::size_t signature_length);

}  // namespace riftwii
