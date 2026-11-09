// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// DSA verification (riftwii/ctgpdsa.hpp), with a key made for the test.
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/ctgpdsa.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

static std::vector<std::uint8_t> hex(const char* s) {
    std::vector<std::uint8_t> out;
    for (; s[0] && s[1]; s += 2) {
        auto nib = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
        out.push_back(static_cast<std::uint8_t>(nib(s[0]) * 16 + nib(s[1])));
    }
    return out;
}

// A key made for this test (not CTGP's): p and q are those of RiftWii's
// unusable ghost key, h a proper generator of order q, y = h^x. Two
// 20-byte digests and their DSA signatures, made with Python.
static const char* kKeyP = "8325cb6682d46629271ee6d7783f76bf2c1a818f571171d531215309cbb4f5265a159e0dbd0866c4cb8071ea022154c19e37da39332f94a0ce42e9144e4a30bf";
static const char* kKeyQ = "93a1cb73a4780f8822782e8c0cc0cee96c1a9b5f";
static const char* kKeyG = "151d5db5e0c133a3151299e14c5248ce278c5eff31c439adae6f9b153167d90e83d14839667d00da54d0c1fbb5852e0683410ea429f49438bce96595baaa4724";
static const char* kKeyY = "7e12004cf2871fc91e9cf7ec0c7919cd88d09c5c4109aecc483cf71ccc47ee5c916c3ed44959c7903c5fbf2623d9547c93ddb6cde5159151d98ae88d4ef2ffa6";
static const char* kZ1 = "d7a5db9fc16a5a9087577ad66e067fa9a7e5a4e4";
static const char* kS1 = "302c02140ac7217ab3a6de798560ffdadb18932a5557d5ca02147fa313f56df6b31df0fc3ea8039073adc12b3528";
static const char* kZ2 = "0758a7319e8142d2accc209f710a0f6332ad2835";
static const char* kS2 = "302c0214541553936e30d9203a2ab075eaaedd752ad9c4c702141c42517c830cb8c007df2d57bab3a9881595b29c";

static CtgpDsaKey test_key() {
    CtgpDsaKey k{};
    const auto p = hex(kKeyP), q = hex(kKeyQ), g = hex(kKeyG), y = hex(kKeyY);
    std::memcpy(k.p, p.data(), 64);
    std::memcpy(k.q, q.data(), 20);
    std::memcpy(k.g, g.data(), 64);
    std::memcpy(k.y, y.data(), 64);
    return k;
}

int main() {
    const CtgpDsaKey key = test_key();
    const auto z1 = hex(kZ1), s1 = hex(kS1), z2 = hex(kZ2), s2 = hex(kS2);
    EXPECT_TRUE(ctgp_dsa_verify(key, z1.data(), z1.size(), s1.data(), s1.size()));
    EXPECT_TRUE(ctgp_dsa_verify(key, z2.data(), z2.size(), s2.data(), s2.size()));
    // each signature belongs to its own header only
    EXPECT_TRUE(!ctgp_dsa_verify(key, z2.data(), z2.size(), s1.data(), s1.size()));
    EXPECT_TRUE(!ctgp_dsa_verify(key, z1.data(), z1.size(), s2.data(), s2.size()));
    // any changed digest byte, in the part that counts, fails
    for (std::size_t i = 0; i < z1.size(); ++i) {
        auto bad = z1;
        bad[i] ^= 0x01;
        EXPECT_TRUE(!ctgp_dsa_verify(key, bad.data(), bad.size(), s1.data(), s1.size()));
    }
    // only the leftmost 20 bytes count: a longer digest with the same start verifies
    auto longer = z1;
    longer.insert(longer.end(), 24, 0x5A);
    EXPECT_TRUE(ctgp_dsa_verify(key, longer.data(), longer.size(), s1.data(), s1.size()));
    // any changed signature byte fails (DER bytes included)
    for (std::size_t i = 0; i < s1.size(); ++i) {
        auto bad = s1;
        bad[i] ^= 0x01;
        EXPECT_TRUE(!ctgp_dsa_verify(key, z1.data(), z1.size(), bad.data(), bad.size()));
    }
    // malformed signatures
    EXPECT_TRUE(!ctgp_dsa_verify(key, z1.data(), z1.size(), s1.data(), s1.size() - 1));
    EXPECT_TRUE(!ctgp_dsa_verify(key, z1.data(), z1.size(), s1.data(), 4));
    EXPECT_TRUE(!ctgp_dsa_verify(key, z1.data(), z1.size(), nullptr, 0));
    EXPECT_TRUE(!ctgp_dsa_verify(key, z1.data(), 0, s1.data(), s1.size()));
    // r = 0
    auto zero_r = hex("3006020100020101");
    EXPECT_TRUE(!ctgp_dsa_verify(key, z1.data(), z1.size(), zero_r.data(), zero_r.size()));
    // a different key fails
    CtgpDsaKey other = key;
    other.y[63] ^= 1;
    EXPECT_TRUE(!ctgp_dsa_verify(other, z1.data(), z1.size(), s1.data(), s1.size()));
    if (g_failures) {
        std::cerr << g_failures << " failures" << std::endl;
        return 1;
    }
    std::cout << "ctgpdsa tests passed" << std::endl;
    return 0;
}
