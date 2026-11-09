// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpdsa.hpp"

#include <algorithm>
#include <vector>

namespace riftwii {
namespace {

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
