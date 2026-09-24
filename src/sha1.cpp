// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/sha1.hpp"

#include <algorithm>
#include <cstring>

namespace riftwii {
namespace {

std::uint32_t rol(std::uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

}

Sha1::Sha1() : h_{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u}, buffer_{} {}

void Sha1::block(const std::uint8_t* p) {
    std::uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = (std::uint32_t(p[4 * i]) << 24) | (std::uint32_t(p[4 * i + 1]) << 16) |
               (std::uint32_t(p[4 * i + 2]) << 8) | p[4 * i + 3];
    }
    for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
    for (int i = 0; i < 80; ++i) {
        std::uint32_t f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        const std::uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol(b, 30);
        b = a;
        a = t;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
}

void Sha1::update(const std::uint8_t* data, std::size_t length) {
    total_ += length;
    while (length > 0) {
        const std::size_t take = std::min<std::size_t>(64 - used_, length);
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

Sha1Digest Sha1::finish() {
    const std::uint64_t bits = total_ * 8;
    const std::uint8_t one = 0x80;
    update(&one, 1);
    const std::uint8_t zero = 0;
    while (used_ != 56) update(&zero, 1);
    std::uint8_t length[8];
    for (int i = 0; i < 8; ++i) length[i] = std::uint8_t(bits >> (56 - 8 * i));
    update(length, 8);
    Sha1Digest out{};
    for (int i = 0; i < 5; ++i) {
        out[4 * i] = std::uint8_t(h_[i] >> 24);
        out[4 * i + 1] = std::uint8_t(h_[i] >> 16);
        out[4 * i + 2] = std::uint8_t(h_[i] >> 8);
        out[4 * i + 3] = std::uint8_t(h_[i]);
    }
    return out;
}

Sha1Digest sha1(const std::uint8_t* data, std::size_t length) {
    Sha1 s;
    s.update(data, length);
    return s.finish();
}

}
