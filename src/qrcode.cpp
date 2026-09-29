// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/qrcode.hpp"

#include <algorithm>
#include <cstdlib>

namespace riftwii {
namespace {

// Error correction L, one block each: data and error-correction codewords.
struct Capacity {
    int data, ec;
};
constexpr Capacity kCapacity[5] = {{0, 0}, {19, 7}, {34, 10}, {55, 15}, {80, 20}};

// Multiplication in GF(2^8) modulo x^8 + x^4 + x^3 + x^2 + 1 (0x11D).
std::uint8_t gf_mul(std::uint8_t a, std::uint8_t b) {
    int z = 0;
    for (int i = 7; i >= 0; --i) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((b >> i) & 1) * a;
    }
    return static_cast<std::uint8_t>(z);
}

// The generator polynomial's coefficients (highest first, the leading 1
// left out) for `degree` error-correction codewords, roots a^0..a^(n-1).
std::vector<std::uint8_t> rs_divisor(int degree) {
    std::vector<std::uint8_t> result(static_cast<std::size_t>(degree), 0);
    result.back() = 1;
    std::uint8_t root = 1;
    for (int i = 0; i < degree; ++i) {
        for (int j = 0; j < degree; ++j) {
            result[j] = gf_mul(result[j], root);
            if (j + 1 < degree) result[j] ^= result[j + 1];
        }
        root = gf_mul(root, 0x02);
    }
    return result;
}

std::vector<std::uint8_t> rs_remainder(const std::vector<std::uint8_t>& data, const std::vector<std::uint8_t>& divisor) {
    std::vector<std::uint8_t> result(divisor.size(), 0);
    for (std::uint8_t b : data) {
        const std::uint8_t factor = b ^ result[0];
        result.erase(result.begin());
        result.push_back(0);
        for (std::size_t i = 0; i < result.size(); ++i) result[i] ^= gf_mul(divisor[i], factor);
    }
    return result;
}

// The byte-mode bit stream, terminated and padded to the version's data
// codewords.
std::vector<std::uint8_t> data_codewords(const std::string& text, int data_bytes) {
    std::vector<bool> bits;
    const auto put = [&](unsigned value, int count) {
        for (int i = count - 1; i >= 0; --i) bits.push_back(((value >> i) & 1) != 0);
    };
    put(0x4, 4);  // byte mode
    put(static_cast<unsigned>(text.size()), 8);
    for (unsigned char c : text) put(c, 8);
    const std::size_t capacity = static_cast<std::size_t>(data_bytes) * 8;
    put(0, static_cast<int>(std::min<std::size_t>(4, capacity - bits.size())));
    while (bits.size() % 8 != 0) bits.push_back(false);
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i < bits.size(); i += 8) {
        std::uint8_t b = 0;
        for (std::size_t j = 0; j < 8; ++j) b = static_cast<std::uint8_t>((b << 1) | (bits[i + j] ? 1 : 0));
        out.push_back(b);
    }
    for (std::uint8_t pad = 0xEC; out.size() < static_cast<std::size_t>(data_bytes); pad ^= 0xEC ^ 0x11) out.push_back(pad);
    return out;
}

bool masked(int mask, int x, int y) {
    switch (mask) {
        case 0: return (x + y) % 2 == 0;
        case 1: return y % 2 == 0;
        case 2: return x % 3 == 0;
        case 3: return (x + y) % 3 == 0;
        case 4: return (x / 3 + y / 2) % 2 == 0;
        case 5: return x * y % 2 + x * y % 3 == 0;
        case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
        default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
    }
}

struct Grid {
    int size;
    std::vector<std::uint8_t> dark, function;
    explicit Grid(int n) : size(n), dark(static_cast<std::size_t>(n) * n, 0), function(static_cast<std::size_t>(n) * n, 0) {}
    std::size_t at(int x, int y) const { return static_cast<std::size_t>(y) * size + x; }
    void set_function(int x, int y, bool on) {
        dark[at(x, y)] = on ? 1 : 0;
        function[at(x, y)] = 1;
    }
};

void draw_finder(Grid& g, int cx, int cy) {
    for (int dy = -4; dy <= 4; ++dy) {
        for (int dx = -4; dx <= 4; ++dx) {
            const int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= g.size || y >= g.size) continue;
            const int dist = std::max(std::abs(dx), std::abs(dy));
            g.set_function(x, y, dist != 2 && dist != 4);
        }
    }
}

// Error correction L is 01; BCH(15,5) with x^10+x^8+x^5+x^4+x^2+x+1,
// masked with 101010000010010.
void draw_format(Grid& g, int mask) {
    const int data = (1 << 3) | mask;
    int rem = data;
    for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    const int bits = ((data << 10) | rem) ^ 0x5412;
    const auto bit = [bits](int i) { return ((bits >> i) & 1) != 0; };
    for (int i = 0; i <= 5; ++i) g.set_function(8, i, bit(i));
    g.set_function(8, 7, bit(6));
    g.set_function(8, 8, bit(7));
    g.set_function(7, 8, bit(8));
    for (int i = 9; i < 15; ++i) g.set_function(14 - i, 8, bit(i));
    for (int i = 0; i < 8; ++i) g.set_function(g.size - 1 - i, 8, bit(i));
    for (int i = 8; i < 15; ++i) g.set_function(8, g.size - 15 + i, bit(i));
    g.set_function(8, g.size - 8, true);  // the dark module
}

// The specification's penalty: runs of five or more, 2x2 blocks, finder-
// like 1:1:3:1:1 runs with four light modules beside them (outside the
// symbol counts as light), and the dark/light balance.
long penalty(const Grid& g) {
    const int n = g.size;
    long score = 0;
    const auto dark = [&](int x, int y) { return x >= 0 && y >= 0 && x < n && y < n && g.dark[g.at(x, y)] != 0; };
    for (int pass = 0; pass < 2; ++pass) {
        for (int a = 0; a < n; ++a) {
            const auto cell = [&](int b) { return pass == 0 ? dark(b, a) : dark(a, b); };
            int run = 1;
            for (int b = 1; b <= n; ++b) {
                if (b < n && cell(b) == cell(b - 1)) {
                    ++run;
                    continue;
                }
                if (run >= 5) score += 3 + (run - 5);
                run = 1;
            }
            static const bool kFinder[7] = {true, false, true, true, true, false, true};
            for (int b = -4; b < n + 4 - 6; ++b) {
                bool core = true;
                for (int k = 0; k < 7 && core; ++k) core = cell(b + k) == kFinder[k];
                if (!core) continue;
                bool before = true, after = true;
                for (int k = 1; k <= 4; ++k) {
                    before = before && !cell(b - k);
                    after = after && !cell(b + 6 + k);
                }
                if (before) score += 40;
                if (after) score += 40;
            }
        }
    }
    for (int y = 0; y + 1 < n; ++y)
        for (int x = 0; x + 1 < n; ++x) {
            const bool c = dark(x, y);
            if (dark(x + 1, y) == c && dark(x, y + 1) == c && dark(x + 1, y + 1) == c) score += 3;
        }
    long darks = 0;
    for (std::uint8_t d : g.dark) darks += d;
    const long total = static_cast<long>(n) * n;
    const long k = (std::labs(darks * 20 - total * 10) + total - 1) / total - 1;
    score += std::max(0L, k) * 10;
    return score;
}

}  // namespace

bool make_qr(const std::string& text, QrCode& out, int force_mask) {
    int version = 1;
    while (version <= 4 && static_cast<int>(text.size()) + 2 > kCapacity[version].data) ++version;
    if (version > 4) return false;
    const int size = 17 + 4 * version;
    const Capacity cap = kCapacity[version];

    std::vector<std::uint8_t> codewords = data_codewords(text, cap.data);
    const std::vector<std::uint8_t> ec = rs_remainder(codewords, rs_divisor(cap.ec));
    codewords.insert(codewords.end(), ec.begin(), ec.end());

    Grid base(size);
    for (int i = 0; i < size; ++i) {
        base.set_function(6, i, i % 2 == 0);
        base.set_function(i, 6, i % 2 == 0);
    }
    draw_finder(base, 3, 3);
    draw_finder(base, size - 4, 3);
    draw_finder(base, 3, size - 4);
    if (version >= 2) {
        const int c = size - 7;  // the one alignment pattern clear of the finders
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) base.set_function(c + dx, c + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
    }
    draw_format(base, 0);  // reserves the format areas

    // The codewords' bits in the zigzag: two columns at a time from the
    // right, up then down, stepping over the timing column.
    std::size_t bit = 0;
    const std::size_t bits = codewords.size() * 8;
    for (int right = size - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int vert = 0; vert < size; ++vert) {
            for (int j = 0; j < 2; ++j) {
                const int x = right - j;
                const bool upward = ((right + 1) & 2) == 0;
                const int y = upward ? size - 1 - vert : vert;
                if (base.function[base.at(x, y)] || bit >= bits) continue;
                base.dark[base.at(x, y)] = (codewords[bit >> 3] >> (7 - (bit & 7))) & 1;
                ++bit;
            }
        }
    }

    int best = -1;
    long best_score = 0;
    Grid chosen(size);
    for (int mask = 0; mask < 8; ++mask) {
        if (force_mask >= 0 && mask != force_mask) continue;
        Grid g = base;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                if (!g.function[g.at(x, y)] && masked(mask, x, y)) g.dark[g.at(x, y)] ^= 1;
        draw_format(g, mask);
        const long score = penalty(g);
        if (best < 0 || score < best_score) {
            best = mask;
            best_score = score;
            chosen = g;
        }
    }
    out.size = size;
    out.dark = chosen.dark;
    out.version = version;
    out.mask = best;
    return true;
}

}  // namespace riftwii
