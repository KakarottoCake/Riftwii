// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpdsa.hpp"

#include <algorithm>
#include <vector>

namespace riftwii {
namespace {

// CTGP-R's list-signing public key (the PEM at 0x809da1e0 in the channel). It
// is a public DSA key, not a secret: p/q/g are the domain parameters and y is
// the public value, so it can only check a signature, never produce one. Like
// the row table, it is plain CTGP-R data RiftWii may carry in its own build.
const std::uint8_t kP[64] = {
    0xCE, 0x0C, 0xCD, 0x73, 0x6E, 0xA6, 0x4C, 0x83, 0xFC, 0xCD, 0xD0, 0x4C,
    0xFD, 0x3B, 0x18, 0x56, 0x19, 0x5B, 0xCC, 0x09, 0x95, 0xDF, 0x10, 0x66,
    0x22, 0xA2, 0x50, 0xAE, 0x20, 0x3F, 0x55, 0x49, 0x5E, 0x11, 0x8F, 0xB0,
    0x68, 0x8E, 0x5D, 0x7F, 0x6F, 0x57, 0x2E, 0x1E, 0x27, 0x5F, 0x7B, 0xE4,
    0x3A, 0xA3, 0x77, 0xFB, 0xC3, 0xA8, 0xB4, 0x3A, 0x66, 0xFD, 0x95, 0xBD,
    0xAC, 0x50, 0xEB, 0x95,
};
const std::uint8_t kQ[20] = {
    0xDE, 0xB6, 0x60, 0xB6, 0xB1, 0x6A, 0x75, 0xFD, 0xD6, 0x88, 0x0F, 0xA1,
    0x50, 0xD6, 0x8F, 0x5A, 0xA4, 0x04, 0x7B, 0x95,
};
const std::uint8_t kG[64] = {
    0x86, 0xB0, 0x5D, 0x5C, 0xC4, 0xA9, 0xDE, 0x62, 0x91, 0x64, 0x85, 0x24,
    0x99, 0x3D, 0x77, 0xC6, 0x10, 0xDF, 0x4E, 0x46, 0xF0, 0xCD, 0xBB, 0xF7,
    0x14, 0x41, 0x6F, 0xDD, 0x25, 0xA5, 0x08, 0xE4, 0xB5, 0x8F, 0xE6, 0x6D,
    0xB8, 0x35, 0xCA, 0x17, 0x2F, 0xCE, 0x3C, 0x12, 0x77, 0x7D, 0x17, 0x53,
    0x63, 0x7E, 0xB1, 0xD6, 0xA7, 0xEA, 0x19, 0xA1, 0x39, 0x58, 0x3E, 0xC5,
    0xD7, 0xC7, 0xD7, 0xD2,
};
const std::uint8_t kY[64] = {
    0xC6, 0xD6, 0x8C, 0x03, 0x97, 0xE3, 0x8A, 0x9E, 0xC4, 0x74, 0x2E, 0xAA,
    0x84, 0xD5, 0xF1, 0x87, 0x9B, 0x43, 0x2A, 0x67, 0x08, 0x3C, 0x21, 0xA7,
    0xAA, 0x8B, 0xA6, 0x43, 0x49, 0xE3, 0xCD, 0x81, 0x50, 0x75, 0x93, 0xA4,
    0xDB, 0x1A, 0x91, 0xC6, 0xE8, 0x12, 0x46, 0x1D, 0xBD, 0xEA, 0xFC, 0xF4,
    0xC7, 0x8C, 0x6E, 0xA0, 0xDD, 0xCC, 0xDE, 0x15, 0x1E, 0xC1, 0xA1, 0x74,
    0xBA, 0x13, 0x8E, 0xEA,
};

// Unsigned big numbers, little-endian 32-bit limbs, no leading zero limbs.
using Big = std::vector<std::uint32_t>;

void trim(Big& a) {
    while (!a.empty() && a.back() == 0) a.pop_back();
}

Big from_bytes(const std::uint8_t* d, std::size_t n) {
    Big r((n + 3) / 4, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t from_end = n - 1 - i;
        r[from_end / 4] |= static_cast<std::uint32_t>(d[i]) << (8 * (from_end % 4));
    }
    trim(r);
    return r;
}

int compare(const Big& a, const Big& b) {
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    for (std::size_t i = a.size(); i-- > 0;) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

Big sub(const Big& a, const Big& b) {  // a >= b
    Big r(a.size(), 0);
    std::int64_t borrow = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::int64_t v = static_cast<std::int64_t>(a[i]) - (i < b.size() ? b[i] : 0) - borrow;
        borrow = v < 0;
        if (v < 0) v += 0x100000000ll;
        r[i] = static_cast<std::uint32_t>(v);
    }
    trim(r);
    return r;
}

Big mul(const Big& a, const Big& b) {
    if (a.empty() || b.empty()) return Big();
    Big r(a.size() + b.size(), 0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        std::uint64_t carry = 0;
        for (std::size_t j = 0; j < b.size(); ++j) {
            const std::uint64_t cur = r[i + j] + static_cast<std::uint64_t>(a[i]) * b[j] + carry;
            r[i + j] = static_cast<std::uint32_t>(cur);
            carry = cur >> 32;
        }
        r[i + b.size()] += static_cast<std::uint32_t>(carry);
    }
    trim(r);
    return r;
}

// a mod m by binary long division (slow, simple, fine for 1024-bit by 512-bit).
Big mod(const Big& a, const Big& m) {
    Big r;
    for (std::size_t i = a.size() * 32; i-- > 0;) {
        // r = r * 2 + bit i of a
        std::uint32_t carry = (a[i / 32] >> (i % 32)) & 1u;
        for (std::size_t k = 0; k < r.size(); ++k) {
            const std::uint32_t next = r[k] >> 31;
            r[k] = (r[k] << 1) | carry;
            carry = next;
        }
        if (carry) r.push_back(carry);
        if (compare(r, m) >= 0) r = sub(r, m);
    }
    trim(r);
    return r;
}

Big mulmod(const Big& a, const Big& b, const Big& m) { return mod(mul(a, b), m); }

// base^exponent mod m, square and multiply from the top bit.
Big powmod(const Big& base, const Big& exponent, const Big& m) {
    Big result{1};
    const Big b = mod(base, m);
    for (std::size_t i = exponent.size() * 32; i-- > 0;) {
        result = mulmod(result, result, m);
        if ((exponent[i / 32] >> (i % 32)) & 1u) result = mulmod(result, b, m);
    }
    return result;
}

// One DER INTEGER at `at`; advances `at`. False on anything malformed.
bool read_der_integer(const std::uint8_t* d, std::size_t n, std::size_t& at, Big& out) {
    if (at + 2 > n || d[at] != 0x02) return false;
    const std::size_t len = d[at + 1];
    if (len == 0 || len >= 0x80 || at + 2 + len > n) return false;
    if (d[at + 2] & 0x80) return false;  // negative
    out = from_bytes(d + at + 2, len);
    at += 2 + len;
    return true;
}

}  // namespace

const CtgpDsaKey& ctgp_signing_key() {
    static const CtgpDsaKey key = [] {
        CtgpDsaKey k;
        std::copy(kP, kP + 64, k.p);
        std::copy(kQ, kQ + 20, k.q);
        std::copy(kG, kG + 64, k.g);
        std::copy(kY, kY + 64, k.y);
        return k;
    }();
    return key;
}

bool ctgp_dsa_verify(const CtgpDsaKey& key, const std::uint8_t* digest, std::size_t digest_length,
                     const std::uint8_t* signature, std::size_t signature_length) {
    if (!digest || !signature || digest_length == 0) return false;
    // SEQUENCE { INTEGER r, INTEGER s }
    if (signature_length < 8 || signature[0] != 0x30) return false;
    const std::size_t body = signature[1];
    if (body >= 0x80 || body + 2 != signature_length) return false;
    std::size_t at = 2;
    Big r, s;
    if (!read_der_integer(signature, signature_length, at, r) || !read_der_integer(signature, signature_length, at, s) ||
        at != signature_length)
        return false;

    const Big p = from_bytes(key.p, sizeof key.p), q = from_bytes(key.q, sizeof key.q);
    const Big g = from_bytes(key.g, sizeof key.g), y = from_bytes(key.y, sizeof key.y);
    if (r.empty() || s.empty() || compare(r, q) >= 0 || compare(s, q) >= 0) return false;
    const Big z = from_bytes(digest, std::min<std::size_t>(digest_length, 20));
    // w = s^-1 mod q (q is prime: s^(q-2))
    const Big w = powmod(s, sub(q, Big{2}), q);
    const Big u1 = mulmod(mod(z, q), w, q);
    const Big u2 = mulmod(r, w, q);
    const Big v = mod(mulmod(powmod(g, u1, p), powmod(y, u2, p), p), q);
    return compare(v, r) == 0;
}

}  // namespace riftwii
