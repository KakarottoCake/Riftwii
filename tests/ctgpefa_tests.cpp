// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The package disk's decryption (riftwii/ctgpefa.hpp), with published AES vectors.
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/ctgpefa.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

static std::vector<std::uint8_t> hex(const std::string& s) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i + 1 < s.size(); i += 2) out.push_back(static_cast<std::uint8_t>(std::stoi(s.substr(i, 2), nullptr, 16)));
    return out;
}

static void test_fips_197() {
    // FIPS-197 appendix C.1
    std::array<std::uint8_t, 16> key;
    const auto k = hex("000102030405060708090a0b0c0d0e0f");
    std::memcpy(key.data(), k.data(), 16);
    const Aes128Decrypt aes(key);
    const auto c = hex("69c4e0d86a7b0430d8cdb78070b4c55a");
    std::uint8_t p[16];
    aes.decrypt_block(c.data(), p);
    EXPECT_TRUE(std::memcmp(p, hex("00112233445566778899aabbccddeeff").data(), 16) == 0);
}

static void test_cbc_sp800_38a() {
    std::array<std::uint8_t, 16> key;
    const auto k = hex("2b7e151628aed2a6abf7158809cf4f3c");
    std::memcpy(key.data(), k.data(), 16);
    const Aes128Decrypt aes(key);
    const auto iv = hex("000102030405060708090a0b0c0d0e0f");
    const auto c = hex("7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2");
    std::vector<std::uint8_t> p(32);
    aes.decrypt_cbc(iv.data(), c.data(), p.data(), 32);
    EXPECT_TRUE(p == hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51"));
    // in place gives the same
    std::vector<std::uint8_t> q = c;
    aes.decrypt_cbc(iv.data(), q.data(), q.data(), 32);
    EXPECT_TRUE(q == p);
}

static void test_reader() {
    // Made-up fixed IV words (CTGP's own are ctgp_blob_chunk_iv, ctgptables.hpp).
    CtgpChunkIv fixed;
    fixed.w0 = 0x11223344;
    fixed.w1 = 0x55667788;
    fixed.w3 = 0x99AABBCC;
    const auto iv = ctgp_efa_chunk_iv(fixed, 0x0102);
    EXPECT_EQ(iv[0], 0x11);
    EXPECT_EQ(iv[7], 0x88);
    EXPECT_EQ(iv[8], 0x00);
    EXPECT_EQ(iv[10], 0x01);
    EXPECT_EQ(iv[11], 0x02);
    EXPECT_EQ(iv[15], 0xCC);

    std::array<std::uint8_t, 16> key{};
    for (int i = 0; i < 16; ++i) key[i] = static_cast<std::uint8_t>(0xA0 + i);
    const Aes128Decrypt aes(key);
    // 2.5 chunks of arbitrary "ciphertext"
    const std::size_t size = 2 * kCtgpEfaChunkBytes + 40 * 512;
    std::vector<std::uint8_t> cipher(size);
    for (std::size_t i = 0; i < size; ++i) cipher[i] = static_cast<std::uint8_t>((i * 131 + (i >> 8) * 7) & 0xFF);
    std::vector<std::uint8_t> plain(size);
    for (std::size_t c = 0; c * kCtgpEfaChunkBytes < size; ++c) {
        const std::size_t at = c * kCtgpEfaChunkBytes;
        const std::size_t len = std::min<std::size_t>(kCtgpEfaChunkBytes, size - at);
        const auto chunk_iv = ctgp_efa_chunk_iv(fixed, static_cast<std::uint32_t>(c * kCtgpEfaChunkSectors));
        aes.decrypt_cbc(chunk_iv.data(), cipher.data() + at, plain.data() + at, len);
    }
    int raw_reads = 0;
    BlockReader reader = ctgp_efa_block_reader(
        [&](std::uint64_t offset, std::uint32_t length, std::uint8_t* out) {
            ++raw_reads;
            if (offset + length > cipher.size()) return false;
            std::memcpy(out, cipher.data() + offset, length);
            return true;
        },
        size, key, fixed);
    std::vector<std::uint8_t> got(512 * 3);
    EXPECT_TRUE(reader(0, 1, got.data()));
    EXPECT_TRUE(std::memcmp(got.data(), plain.data(), 512) == 0);
    // blocks that straddle a chunk boundary
    EXPECT_TRUE(reader(63, 3, got.data()));
    EXPECT_TRUE(std::memcmp(got.data(), plain.data() + 63 * 512, 3 * 512) == 0);
    // the last, partial chunk
    EXPECT_TRUE(reader(2 * 64 + 39, 1, got.data()));
    EXPECT_TRUE(std::memcmp(got.data(), plain.data() + (2 * 64 + 39) * 512, 512) == 0);
    // past the end
    EXPECT_TRUE(!reader(2 * 64 + 40, 1, got.data()));
    EXPECT_TRUE(!reader(2 * 64 + 39, 2, got.data()));
    // a chunk read twice in a row comes from the cache
    const int before = raw_reads;
    EXPECT_TRUE(reader(0, 1, got.data()));
    EXPECT_EQ(raw_reads, before);
}

static void test_efb_key() {
    // FAT date: (year-1980)<<9 | month<<5 | day, time: hour<<11 | minute<<5 | second/2
    const std::uint16_t date = static_cast<std::uint16_t>(((2026 - 1980) << 9) | (9 << 5) | 24);
    const std::uint16_t time = static_cast<std::uint16_t>((20 << 11) | (46 << 5) | (2 / 2));
    EXPECT_EQ(ctgp_fat_seconds(time, date), 1790282762u);    // 2026-09-24 20:46:02, taken as UTC
    EXPECT_EQ(ctgp_fat_seconds(0, static_cast<std::uint16_t>((0 << 9) | (1 << 5) | 1)), 315532800u);   // 1980-01-01
    // Made-up fixed words 1 and 3 (CTGP's own are ctgp_jelly_key_words, ctgptables.hpp).
    CtgpEfbKeyWords words;
    words.w1 = 0x0A0B0C0D;
    words.w3 = 0x01020304;
    const auto key = ctgp_efb_key(words, time, date, 0, static_cast<std::uint16_t>((0 << 9) | (1 << 5) | 1));
    const std::uint8_t want[16] = {0x6A, 0xB5, 0x8C, 0x0A, 0x0A, 0x0B, 0x0C, 0x0D, 0x12, 0xCE, 0xA6, 0x00, 0x01, 0x02, 0x03, 0x04};
    for (int i = 0; i < 16; ++i) EXPECT_EQ((unsigned)key[i], (unsigned)want[i]);
}

int main() {
    test_fips_197();
    test_cbc_sp800_38a();
    test_reader();
    test_efb_key();
    if (g_failures == 0) std::cout << "ctgpefa tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
