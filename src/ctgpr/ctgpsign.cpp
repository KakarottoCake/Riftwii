// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpsign.hpp"

#include <cstddef>

namespace riftwii {
namespace {

// RiftWii's own key, made for this file (tests/ctgpsign_tests.cpp): p is a
// 512-bit prime and q a 160-bit prime dividing p - 1, as in a real DSA key,
// to provide populated inputs to the signer; g = 2 does not have order q, and y is
// 2^(x+1) mod p, not 2^x. This is intended to prevent accepted signatures;
// a fixed local negative sample is tested, not universal server rejection.
const std::uint8_t kP[64] = {
    0x83, 0x25, 0xCB, 0x66, 0x82, 0xD4, 0x66, 0x29, 0x27, 0x1E, 0xE6, 0xD7,
    0x78, 0x3F, 0x76, 0xBF, 0x2C, 0x1A, 0x81, 0x8F, 0x57, 0x11, 0x71, 0xD5,
    0x31, 0x21, 0x53, 0x09, 0xCB, 0xB4, 0xF5, 0x26, 0x5A, 0x15, 0x9E, 0x0D,
    0xBD, 0x08, 0x66, 0xC4, 0xCB, 0x80, 0x71, 0xEA, 0x02, 0x21, 0x54, 0xC1,
    0x9E, 0x37, 0xDA, 0x39, 0x33, 0x2F, 0x94, 0xA0, 0xCE, 0x42, 0xE9, 0x14,
    0x4E, 0x4A, 0x30, 0xBF,
};
const std::uint8_t kQ[20] = {
    0x93, 0xA1, 0xCB, 0x73, 0xA4, 0x78, 0x0F, 0x88, 0x22, 0x78, 0x2E, 0x8C,
    0x0C, 0xC0, 0xCE, 0xE9, 0x6C, 0x1A, 0x9B, 0x5F,
};
const std::uint8_t kG[64] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x02,
};
const std::uint8_t kY[64] = {
    0x08, 0x59, 0x11, 0x33, 0x61, 0xBA, 0x5A, 0xDA, 0x7B, 0x05, 0x29, 0xCC,
    0x25, 0xE8, 0x96, 0xEC, 0x6D, 0x1E, 0x04, 0xFA, 0x3B, 0x8A, 0xB6, 0xAD,
    0x30, 0xAF, 0xA4, 0x44, 0xAD, 0x20, 0xF6, 0x70, 0xF1, 0xE5, 0xCA, 0xE2,
    0x54, 0xBF, 0xB8, 0xE5, 0xD5, 0xBA, 0xBA, 0xBD, 0xA7, 0x95, 0xE8, 0xC8,
    0x1E, 0xEC, 0x0A, 0x4C, 0xB7, 0x5F, 0x23, 0xA6, 0x9B, 0x3B, 0x82, 0x70,
    0xF0, 0xE4, 0xE1, 0x0D,
};
const std::uint8_t kX[20] = {
    0x8B, 0x8B, 0x30, 0xD7, 0x89, 0x81, 0xAD, 0xEE, 0xE7, 0xFF, 0xCF, 0xC1,
    0x34, 0x33, 0x54, 0x59, 0xC8, 0x62, 0xFA, 0x2A,
};

// Where the channel puts a key (chan_internal FUN_808be90c): OpenSSL's DSA
// fields pad, version and write_params at 0x816ff52c; pointers to the five
// BIGNUMs (p, q, g, y, x) at 0x816ff538; the BIGNUMs themselves (d, top,
// dmax, neg, flags) at 0x816ff570; their words from 0x816ff5d4, at most 0x100.
constexpr std::uint32_t kBignumPointers = 0x816ff538;
constexpr std::uint32_t kBignums = 0x816ff570;
constexpr std::uint32_t kBignumWords = 0x816ff5d4;
constexpr std::uint32_t kBnFlagMalloced = 1;  // what BN_new leaves, which the DER decoder uses

// A big-endian number as BIGNUM words, least significant first, without
// leading zero words (BN_bin2bn).
std::vector<std::uint32_t> bignum_words(const std::uint8_t* bytes, std::size_t length) {
    std::vector<std::uint32_t> words((length + 3) / 4, 0);
    for (std::size_t i = 0; i < length; ++i) {
        const std::size_t from_end = length - 1 - i;
        words[from_end / 4] |= static_cast<std::uint32_t>(bytes[i]) << (8 * (from_end % 4));
    }
    while (!words.empty() && words.back() == 0) words.pop_back();
    return words;
}

}  // namespace

CtgpUnsignedKey ctgp_unsigned_key() { return {kP, kQ, kG, kY, kX}; }

std::vector<CtgpWordWrite> ctgp_unsigned_key_writes() {
    const std::vector<std::uint32_t> numbers[5] = {bignum_words(kP, sizeof kP), bignum_words(kQ, sizeof kQ),
                                                   bignum_words(kG, sizeof kG), bignum_words(kY, sizeof kY),
                                                   bignum_words(kX, sizeof kX)};
    std::vector<CtgpWordWrite> out;
    // No card's key is named: the 20 bytes BANNER.BIN would give stay zero.
    for (std::uint32_t i = 0; i < 5; ++i) out.push_back({kCtgpBannerDigest + 4 * i, 0});
    out.push_back({kCtgpConsoleTag, 0});
    out.push_back({kCtgpConsoleTag + 4, 0});
    out.push_back({kCtgpSigningKey + 0, 0});  // pad
    out.push_back({kCtgpSigningKey + 4, 0});  // version
    out.push_back({kCtgpSigningKey + 8, 1});  // write_params
    std::uint32_t used = 0;
    for (std::uint32_t i = 0; i < 5; ++i) {
        const std::vector<std::uint32_t>& words = numbers[i];
        const auto count = static_cast<std::uint32_t>(words.size());
        const std::uint32_t bn = kBignums + 20 * i;
        out.push_back({kBignumPointers + 4 * i, bn});
        out.push_back({bn + 0, kBignumWords + 4 * used});
        out.push_back({bn + 4, count});  // top
        out.push_back({bn + 8, count});  // dmax
        out.push_back({bn + 12, 0});     // neg
        out.push_back({bn + 16, kBnFlagMalloced});
        for (std::uint32_t w = 0; w < count; ++w) {
            // the private value (x, the last) is stored masked, as the channel does
            const std::uint32_t value = i == 4 ? words[w] ^ ~w : words[w];
            out.push_back({kBignumWords + 4 * (used + w), value});
        }
        used += count;
    }
    // The rest of the DSA structure: kinv, r, flags (the default method's, 0),
    // the Montgomery context, references, ex_data, method, engine.
    out.push_back({0x816ff54c, 0});
    out.push_back({0x816ff550, 0});
    out.push_back({0x816ff554, 0});
    out.push_back({0x816ff558, 0});
    out.push_back({0x816ff55c, 1});
    out.push_back({0x816ff560, 0});
    out.push_back({0x816ff564, 0});
    out.push_back({0x816ff568, 0});
    out.push_back({0x816ff56c, 0});
    return out;
}

}  // namespace riftwii
