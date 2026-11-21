// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The CTGP-R-exact Yaz0 encoder (riftwii/ctgpyaz0.hpp). All inputs are made up
// here; exactness against the channel's own output was checked by hand on real
// files outside the repo, and the fixed digest at the end pins that behaviour.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/ctgpupdate.hpp"
#include "riftwii/ctgpyaz0.hpp"
#include "riftwii/sha1.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

std::vector<std::uint8_t> encode(const std::vector<std::uint8_t>& in) {
    std::vector<std::uint8_t> out;
    std::string error;
    EXPECT_TRUE(ctgp_yaz0_encode_exact(in.data(), in.size(), out, error));
    return out;
}

bool round_trips(const std::vector<std::uint8_t>& in) {
    const std::vector<std::uint8_t> packed = encode(in);
    if (packed.size() < 16 || std::memcmp(packed.data(), "Yaz0", 4) != 0)
        return false;
    const std::uint32_t size = (std::uint32_t(packed[4]) << 24) | (std::uint32_t(packed[5]) << 16) |
                               (std::uint32_t(packed[6]) << 8) | packed[7];
    if (size != in.size())
        return false;
    for (int i = 8; i < 16; ++i)
        if (packed[i] != 0)
            return false;
    std::vector<std::uint8_t> back;
    std::string error;
    if (!ctgp_yaz0_decode_nintendo(packed.data(), packed.size(), back, error)) {
        std::cerr << "decode: " << error << std::endl;
        return false;
    }
    return back == in;
}

struct Rng {
    std::uint32_t s;
    std::uint32_t next() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
};

// The channel's encoder can't take every start (see test_refused_starts), so
// the made-up data below begins with eight distinct bytes, as real files with
// a magic number do.
const std::uint8_t kPrefix[8] = {'T', 'e', 's', 't', 1, 2, 3, 4};

std::vector<std::uint8_t> prefixed(std::vector<std::uint8_t> body) {
    body.insert(body.begin(), kPrefix, kPrefix + 8);
    return body;
}

std::vector<std::uint8_t> random_bytes(std::size_t n, std::uint32_t seed) {
    Rng r{seed};
    std::vector<std::uint8_t> v(n);
    for (auto& b : v)
        b = static_cast<std::uint8_t>(r.next());
    return v;
}

// Random bytes from a small alphabet mixed with copies of earlier stretches
// (near and far, short and long) and long runs, so every encoder path runs.
std::vector<std::uint8_t> mixed_bytes(std::size_t n, std::uint32_t seed) {
    Rng r{seed};
    std::vector<std::uint8_t> v(kPrefix, kPrefix + 8);
    v.reserve(n);
    while (v.size() < n) {
        const std::uint32_t kind = r.next() % 8;
        if (kind < 3 || v.size() < 16) {
            const std::uint32_t len = 1 + r.next() % 12;
            for (std::uint32_t i = 0; i < len; ++i)
                v.push_back(static_cast<std::uint8_t>(r.next() % (kind == 0 ? 256 : 6)));
        } else if (kind < 7) {
            const std::uint32_t back = 1 + r.next() % (kind == 6 ? 8000 : 300);
            const std::uint32_t len = 2 + r.next() % (kind == 5 ? 400 : 30);
            const std::size_t from = v.size() > back ? v.size() - back : 0;
            for (std::uint32_t i = 0; i < len; ++i)
                v.push_back(v[from + i]);
        } else {
            const std::uint32_t len = 1 + r.next() % 600;
            const std::uint8_t b = static_cast<std::uint8_t>(r.next());
            v.insert(v.end(), len, b);
        }
    }
    v.resize(n);
    return v;
}

std::string hex(const Sha1Digest& d) {
    std::string s;
    char buf[3];
    for (std::uint8_t b : d) {
        std::snprintf(buf, sizeof buf, "%02x", b);
        s += buf;
    }
    return s;
}

void test_exact_small() {
    // Empty: the header alone.
    const std::vector<std::uint8_t> empty_expect = {'Y', 'a', 'z', '0', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    EXPECT_TRUE(encode({}) == empty_expect);

    // One byte: a single literal in a one-item group.
    std::vector<std::uint8_t> one = empty_expect;
    one[7] = 1;
    one.push_back(0x80);
    one.push_back('A');
    EXPECT_TRUE(encode({'A'}) == one);

    // Sixteen equal bytes: a literal, a 13-byte copy at distance 1, then the
    // two look-ahead bytes still waiting at the end go out as literals.
    std::vector<std::uint8_t> run = empty_expect;
    run[7] = 16;
    const std::uint8_t body[] = {0xb0, 'A', 0xb0, 0x00, 'A', 'A'};
    run.insert(run.end(), body, body + sizeof body);
    EXPECT_TRUE(encode(std::vector<std::uint8_t>(16, 'A')) == run);

    // A three-byte repeat is not worth a copy: all literals.
    const std::vector<std::uint8_t> abc = {'a', 'b', 'c', 'x', 'a', 'b', 'c', 'y'};
    std::vector<std::uint8_t> lits = empty_expect;
    lits[7] = 8;
    lits.push_back(0xff);
    lits.insert(lits.end(), abc.begin(), abc.end());
    EXPECT_TRUE(encode(abc) == lits);
}

void test_round_trips() {
    EXPECT_TRUE(round_trips({}));
    EXPECT_TRUE(round_trips({1}));
    EXPECT_TRUE(round_trips({1, 2}));
    EXPECT_TRUE(round_trips({1, 2, 3}));
    EXPECT_TRUE(round_trips(std::vector<std::uint8_t>(5, 0)));
    // All-same bytes, long enough for many 273-byte copies and a tail.
    EXPECT_TRUE(round_trips(prefixed(std::vector<std::uint8_t>(100000, 0x11))));
    EXPECT_TRUE(round_trips(prefixed(std::vector<std::uint8_t>(100000, 0x00))));
    EXPECT_TRUE(round_trips(std::vector<std::uint8_t>(273 * 3 + 2, 0x5a)));
    EXPECT_TRUE(round_trips(std::vector<std::uint8_t>(273 + 3, 0x5a)));
    EXPECT_TRUE(round_trips(std::vector<std::uint8_t>(4101, 0x5a)));
    // Random data (no matches): sizes around and past the window and odd
    // group lengths.
    for (std::size_t n : {7u, 9u, 4095u, 4096u, 4097u, 4103u, 4104u, 4105u, 70001u})
        EXPECT_TRUE(round_trips(prefixed(random_bytes(n, static_cast<std::uint32_t>(n)))));
    // A block repeated far apart (distance close to the 4 KiB limit) and a
    // long repeat (> 273) at a short distance.
    std::vector<std::uint8_t> far = prefixed(random_bytes(4096 + 600, 3));
    for (std::size_t i = 0; i < 600; ++i)
        far[4096 - 10 + i] = far[i];
    EXPECT_TRUE(round_trips(far));
    std::vector<std::uint8_t> period = prefixed(random_bytes(37, 4));
    while (period.size() < 5000)
        period.push_back(period[period.size() - 37]);
    EXPECT_TRUE(round_trips(period));
    // Large mixed data: well past the window and many table rebuilds.
    EXPECT_TRUE(round_trips(mixed_bytes(3u << 20, 99)));
    for (std::uint32_t seed = 1; seed <= 20; ++seed)
        EXPECT_TRUE(round_trips(mixed_bytes(20000 + seed * 977, seed)));
}

void test_pinned_output() {
    // The encoder's exact bytes are a contract (the pack's hash list checks
    // them), so pin them for made-up data that exercises the table rebuild.
    const std::vector<std::uint8_t> packed = encode(mixed_bytes(2u << 20, 7));
    const std::string got = hex(sha1(packed.data(), packed.size()));
    EXPECT_TRUE(got == "edc6806ea3a413dbcd9b8696921f5e5a318b2419");
}

void test_refused_starts() {
    std::vector<std::uint8_t> out = {1, 2, 3};
    std::string error;
    EXPECT_TRUE(!ctgp_yaz0_encode_exact(nullptr, 5, out, error));
    EXPECT_TRUE(!error.empty());
    const std::uint8_t b = 0;
    EXPECT_TRUE(!ctgp_yaz0_encode_exact(&b, kCtgpYaz0MaxInput + 1, out, error));
    EXPECT_TRUE(ctgp_yaz0_encode_exact(nullptr, 0, out, error));
    EXPECT_TRUE(out.size() == 16);

    // Seven equal bytes at the start leave positions 4-7 out of the table;
    // the channel's encoder would search for them forever once they leave
    // the window (inputs past 4101 bytes): refused.
    const std::vector<std::uint8_t> same(5000, 0x5a);
    EXPECT_TRUE(!ctgp_yaz0_encode_exact(same.data(), same.size(), out, error));
    std::vector<std::uint8_t> repeat_start = random_bytes(6000, 5);
    repeat_start[4] = repeat_start[1];
    repeat_start[5] = repeat_start[2];
    repeat_start[6] = repeat_start[3];
    EXPECT_TRUE(!ctgp_yaz0_encode_exact(repeat_start.data(), repeat_start.size(), out, error));

    // Data starting "a 0 a 0 a" makes the channel's encoder copy from its
    // window before the input's first byte, which it never clears: refused.
    error.clear();
    const std::vector<std::uint8_t> zeros(64, 0);
    EXPECT_TRUE(!ctgp_yaz0_encode_exact(zeros.data(), zeros.size(), out, error));
    EXPECT_TRUE(!error.empty());
    const std::vector<std::uint8_t> alt = {7, 0, 7, 0, 7, 0, 7, 0, 1, 2};
    EXPECT_TRUE(!ctgp_yaz0_encode_exact(alt.data(), alt.size(), out, error));
    // A zero second byte alone is fine (the copy stays short or never starts).
    EXPECT_TRUE(round_trips({7, 0, 7, 0, 9, 9, 9, 9, 9, 9}));
    EXPECT_TRUE(round_trips({7, 0, 0, 0, 0, 0, 0, 0}));
}

}  // namespace

// The writer fed in pieces gives the same bytes as the one-shot encoder, and
// refuses input of the wrong length.
void test_writer_pieces() {
    std::vector<std::uint8_t> input;
    std::uint32_t seed = 99;
    for (int i = 0; i < 150000; ++i) {
        seed = seed * 1103515245u + 12345u;
        if ((seed >> 20) % 4 == 0 && input.size() > 300) {
            const std::size_t back = 1 + (seed >> 8) % 4000 % input.size();
            const std::size_t len = 3 + (seed >> 4) % 300;
            for (std::size_t k = 0; k < len; ++k) input.push_back(input[input.size() - back]);
        } else {
            input.push_back(std::uint8_t('a' + (seed >> 27)));
        }
    }
    std::vector<std::uint8_t> whole;
    std::string error;
    EXPECT_TRUE(ctgp_yaz0_encode_exact(input.data(), input.size(), whole, error));
    for (std::size_t piece : {std::size_t(1), std::size_t(3), std::size_t(4099), std::size_t(65536)}) {
        std::vector<std::uint8_t> out;
        std::size_t calls = 0;
        CtgpYaz0ExactWriter writer(std::uint32_t(input.size()), [&](const std::uint8_t* d, std::size_t n) {
            out.insert(out.end(), d, d + n);
            ++calls;
            return true;
        });
        for (std::size_t at = 0; at < input.size(); at += piece) {
            EXPECT_TRUE(writer.push(input.data() + at, std::min(piece, input.size() - at), error));
        }
        EXPECT_TRUE(writer.finish(error));
        EXPECT_TRUE(out == whole);
        EXPECT_TRUE(calls > 1);  // handed over as it goes, not at the end
    }
    {
        CtgpYaz0ExactWriter writer(10, [](const std::uint8_t*, std::size_t) { return true; });
        EXPECT_TRUE(writer.push(input.data(), 9, error));
        EXPECT_TRUE(!writer.finish(error));
    }
    {
        CtgpYaz0ExactWriter writer(10, [](const std::uint8_t*, std::size_t) { return true; });
        EXPECT_TRUE(!writer.push(input.data(), 11, error));
    }
    {
        // A sink that can't keep the bytes stops the writer.
        CtgpYaz0ExactWriter writer(std::uint32_t(input.size()), [](const std::uint8_t*, std::size_t) { return false; });
        EXPECT_TRUE(!writer.push(input.data(), input.size(), error));
    }
}

int main() {
    test_writer_pieces();
    test_exact_small();
    test_round_trips();
    test_pinned_output();
    test_refused_starts();
    if (g_failures == 0)
        std::cout << "ctgpyaz0 tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
