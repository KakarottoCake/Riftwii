// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/sha256.hpp"

#include <cstring>

namespace riftwii {
namespace {

// FIPS 180-4, section 4.2.2: the first 32 bits of the fractional parts of
// the cube roots of the first 64 primes.
constexpr std::uint32_t kRound[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

std::uint32_t RotateRight(std::uint32_t value, int bits) { return (value >> bits) | (value << (32 - bits)); }

}  // namespace

// Section 5.3.3: the first 32 bits of the fractional parts of the square
// roots of the first 8 primes.
Sha256::Sha256()
    : h_{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u},
      buffer_{} {}

void Sha256::block(const std::uint8_t* p) {
    std::uint32_t schedule[64];
    for (int i = 0; i < 16; ++i) {
        schedule[i] = (std::uint32_t(p[4 * i]) << 24) | (std::uint32_t(p[4 * i + 1]) << 16) |
                      (std::uint32_t(p[4 * i + 2]) << 8) | p[4 * i + 3];
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t before15 = schedule[i - 15];
        const std::uint32_t before2 = schedule[i - 2];
        const std::uint32_t sigma0 = RotateRight(before15, 7) ^ RotateRight(before15, 18) ^ (before15 >> 3);
        const std::uint32_t sigma1 = RotateRight(before2, 17) ^ RotateRight(before2, 19) ^ (before2 >> 10);
        schedule[i] = schedule[i - 16] + sigma0 + schedule[i - 7] + sigma1;
    }
    std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
    std::uint32_t e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t sum1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
        const std::uint32_t choose = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + sum1 + choose + kRound[i] + schedule[i];
        const std::uint32_t sum0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += h;
}

void Sha256::update(const std::uint8_t* data, std::size_t length) {
    total_ += length;
    while (length > 0) {
        if (used_ == 0 && length >= 64) {
            block(data);
            data += 64;
            length -= 64;
            continue;
        }
        const std::size_t take = length < 64 - used_ ? length : 64 - used_;
        std::memcpy(buffer_ + used_, data, take);
        used_ += take;
        data += take;
        length -= take;
        if (used_ == 64) {
            block(buffer_);
            used_ = 0;
        }
    }
}

Sha256Digest Sha256::finish() {
    // Section 5.1.1: a 1 bit, zeros, then the length in bits as 64 bits.
    const std::uint64_t bits = total_ * 8;
    const std::uint8_t one = 0x80;
    update(&one, 1);
    const std::uint8_t zero = 0;
    while (used_ != 56) update(&zero, 1);
    std::uint8_t length[8];
    for (int i = 0; i < 8; ++i) length[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
    update(length, 8);
    Sha256Digest out{};
    for (int i = 0; i < 8; ++i) {
        out[4 * i] = static_cast<std::uint8_t>(h_[i] >> 24);
        out[4 * i + 1] = static_cast<std::uint8_t>(h_[i] >> 16);
        out[4 * i + 2] = static_cast<std::uint8_t>(h_[i] >> 8);
        out[4 * i + 3] = static_cast<std::uint8_t>(h_[i]);
    }
    return out;
}

Sha256Digest sha256(const std::uint8_t* data, std::size_t length) {
    Sha256 hasher;
    hasher.update(data, length);
    return hasher.finish();
}

}  // namespace riftwii
