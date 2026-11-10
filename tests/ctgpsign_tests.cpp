// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The unusable ghost-signing key the launcher hands to CTGP-R (riftwii/ctgpsign.hpp):
// layout and a fixed negative DSA verification sample; no in-game signer runs here.
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>

#include "riftwii/ctgpsign.hpp"
#include "riftwii/ctgpdsa.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

// 2^x mod p: the public value that would belong to the key's private value x,
// made with Python outside the build. The stored public value differs;
// this mismatch alone does not prove universal rejection of signatures.
static const std::uint8_t kYTrue[64] = {
    0x45, 0xBF, 0x6E, 0x4C, 0xF2, 0x47, 0x60, 0x81, 0xD1, 0x12, 0x08, 0x51,
    0xCF, 0x14, 0x06, 0xD5, 0xCC, 0x9C, 0x43, 0x44, 0xC9, 0x4E, 0x14, 0x41,
    0x30, 0xE8, 0x7B, 0xA7, 0x3C, 0x6A, 0xF5, 0xCB, 0xA5, 0xFD, 0xB4, 0x78,
    0x08, 0xE4, 0x0F, 0xD5, 0x50, 0x9D, 0x96, 0x53, 0xD4, 0xDB, 0x9E, 0xC4,
    0xDE, 0x91, 0xF2, 0x42, 0xF5, 0x47, 0x5C, 0x23, 0xB4, 0xBF, 0x35, 0xC2,
    0x9F, 0x97, 0x88, 0xE6,
};

int main() {
    const CtgpUnsignedKey k = ctgp_unsigned_key();

    // A fixed sample produced offline by DSA's signing equations with the
    // supplied p/q/g/x and nonce 123456789. This exercises RiftWii's real DSA
    // verifier, not CTGP's in-game signer or its ghost server's unknown key.
    static const std::uint8_t digest[] = "RiftWii ghost test!!!";
    static const std::uint8_t signature[] = {
        0x30, 0x2C, 0x02, 0x14, 0x1B, 0xDC, 0xC1, 0x61, 0xDF, 0x8B, 0xD4, 0x30,
        0x01, 0xB7, 0x31, 0x6A, 0x05, 0x29, 0x9B, 0x87, 0xC6, 0xDF, 0xC2, 0x5E,
        0x02, 0x14, 0x3E, 0x27, 0x64, 0xE2, 0x9C, 0xEF, 0xD3, 0xAD, 0x3B, 0x1F,
        0xA6, 0xA2, 0xF8, 0x6C, 0x3A, 0x5C, 0x67, 0xF4, 0x2D, 0x2A,
    };
    CtgpDsaKey supplied{};
    std::memcpy(supplied.p, k.p, sizeof supplied.p);
    std::memcpy(supplied.q, k.q, sizeof supplied.q);
    std::memcpy(supplied.g, k.g, sizeof supplied.g);
    std::memcpy(supplied.y, k.y, sizeof supplied.y);
    EXPECT_TRUE(!ctgp_dsa_verify(supplied, digest, sizeof digest - 1, signature, sizeof signature));
    EXPECT_TRUE(!ctgp_dsa_verify(ctgp_signing_key(), digest, sizeof digest - 1, signature, sizeof signature));
    // Structural mismatches support the intent, but do not prove rejection
    // of every possible signature or successful saving/replay in the game.
    EXPECT_TRUE(std::memcmp(k.y, kYTrue, 64) != 0);
    // g is 2.
    EXPECT_TRUE(k.g[63] == 2);
    for (int i = 0; i < 63; ++i) EXPECT_TRUE(k.g[i] == 0);

    std::map<std::uint32_t, std::uint32_t> m;
    for (const CtgpWordWrite& w : ctgp_unsigned_key_writes()) m[w.address] = w.value;
    // No card's key named, no console tag.
    for (std::uint32_t a = kCtgpBannerDigest; a < kCtgpBannerDigest + 20; a += 4) EXPECT_TRUE(m.count(a) && m[a] == 0);
    EXPECT_TRUE(m.count(kCtgpConsoleTag) && m[kCtgpConsoleTag] == 0 && m[kCtgpConsoleTag + 4] == 0);
    // The DSA structure: pad, version, write_params, references.
    EXPECT_TRUE(m[0x816ff52c] == 0 && m[0x816ff530] == 0 && m[0x816ff534] == 1);
    EXPECT_TRUE(m.count(0x816ff554) && m[0x816ff554] == 0 && m[0x816ff55c] == 1);
    // The BIGNUM pointers, and p's BIGNUM: 16 words from 0x816ff5d4.
    EXPECT_TRUE(m[0x816ff538] == 0x816ff570 && m[0x816ff548] == 0x816ff5c0);
    EXPECT_TRUE(m[0x816ff570] == 0x816ff5d4 && m[0x816ff574] == 16 && m[0x816ff578] == 16 && m[0x816ff580] == 1);
    auto be = [](const std::uint8_t* b) {
        return std::uint32_t(b[0]) << 24 | std::uint32_t(b[1]) << 16 | std::uint32_t(b[2]) << 8 | b[3];
    };
    EXPECT_TRUE(m[0x816ff5d4] == be(k.p + 60));  // p's least significant word first
    // q: 5 words after p's 16; g: one word (2); y: 16; x: 5, masked.
    EXPECT_TRUE(m[0x816ff584] == 0x816ff5d4 + 4 * 16 && m[0x816ff588] == 5);
    EXPECT_TRUE(m[0x816ff598] == 0x816ff5d4 + 4 * 21 && m[0x816ff59c] == 1 && m[0x816ff5d4 + 4 * 21] == 2);
    EXPECT_TRUE(m[0x816ff5ac] == 0x816ff5d4 + 4 * 22 && m[0x816ff5b0] == 16);
    EXPECT_TRUE(m[0x816ff5c0] == 0x816ff5d4 + 4 * 38 && m[0x816ff5c4] == 5);
    EXPECT_TRUE(m[0x816ff5d4 + 4 * 38] == (be(k.x + 16) ^ ~0u));
    EXPECT_TRUE(m[0x816ff5d4 + 4 * 39] == (be(k.x + 12) ^ ~1u));
    // Everything stays inside the loader block's key area.
    for (const auto& [address, value] : m) EXPECT_TRUE(address >= 0x816ff4e8 && address < 0x816ff9d4);

    if (g_failures == 0) std::cout << "ctgpsign tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
