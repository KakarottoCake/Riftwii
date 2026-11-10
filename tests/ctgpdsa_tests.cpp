// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// DSA verification with CTGP-R's list-signing key (riftwii/ctgpdsa.hpp).
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

// The first 20 bytes of the headers of CTGPR.HSH and CORE.HSH from a real CTGP-R 1.03 card,
// with the signatures that follow them.
static const char* kZ1 = "4861683000005cf300000000a2c2582e1df3d144";
static const char* kS1 = "302d021427ea05002958b9b6258ed01e5a3ef78a53431a4e021500c6ebe3fd98060b885a6c4faa360bd4bb24c74717";
static const char* kZ2 = "486168300000107800000000400c7645c7c8021e";
static const char* kS2 = "302d02145b0d089debee07f7c8ddc02c93b8cb53bd91f7c0021500d3ff0b7cbd6efbdee77779f1ac68bd06a6eee284";

int main() {
    const CtgpDsaKey& key = ctgp_signing_key();
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
