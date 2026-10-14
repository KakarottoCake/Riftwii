// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/wiicrypt.hpp"

#include <algorithm>
#include <cstring>

#include "riftwii/disc.hpp"

namespace riftwii {
namespace {

// GF(2^8) with the AES polynomial x^8 + x^4 + x^3 + x + 1.
std::uint8_t xtime(std::uint8_t a) { return static_cast<std::uint8_t>((a << 1) ^ ((a & 0x80) ? 0x1B : 0)); }

std::uint8_t gmul(std::uint8_t a, std::uint8_t b) {
    std::uint8_t r = 0;
    while (b) {
        if (b & 1) r ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return r;
}

struct Boxes {
    std::uint8_t s[256], inv[256];
    // Products for (Inv)MixColumns, so a block costs table reads, not loops.
    std::uint8_t m2[256], m3[256], m9[256], m11[256], m13[256], m14[256];
    Boxes() {
        for (int x = 0; x < 256; ++x) {
            const std::uint8_t v = static_cast<std::uint8_t>(x);
            m2[x] = gmul(v, 2);
            m3[x] = gmul(v, 3);
            m9[x] = gmul(v, 9);
            m11[x] = gmul(v, 11);
            m13[x] = gmul(v, 13);
            m14[x] = gmul(v, 14);
        }
        // The S-box: the multiplicative inverse, then the affine map.
        for (int x = 0; x < 256; ++x) {
            std::uint8_t inv_x = 0;
            if (x) {
                for (int y = 1; y < 256; ++y)
                    if (gmul(static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y)) == 1) {
                        inv_x = static_cast<std::uint8_t>(y);
                        break;
                    }
            }
            std::uint8_t b = inv_x, r = inv_x;
            for (int k = 0; k < 4; ++k) {
                b = static_cast<std::uint8_t>((b << 1) | (b >> 7));
                r ^= b;
            }
            r ^= 0x63;
            s[x] = r;
            inv[r] = static_cast<std::uint8_t>(x);
        }
    }
};

const Boxes& boxes() {
    static const Boxes b;
    return b;
}

void add_key(std::uint8_t st[16], const std::uint8_t k[16]) {
    for (int i = 0; i < 16; ++i) st[i] ^= k[i];
}

// The state is column-major: byte 4 * c + r is row r of column c.
void shift_rows(std::uint8_t st[16], bool inverse) {
    std::uint8_t t[16];
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            const int from = inverse ? (c - r + 4) % 4 : (c + r) % 4;
            t[4 * c + r] = st[4 * from + r];
        }
    std::memcpy(st, t, 16);
}

void mix_columns(std::uint8_t st[16], bool inverse) {
    const Boxes& b = boxes();
    for (int c = 0; c < 4; ++c) {
        std::uint8_t* col = st + 4 * c;
        const std::uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        if (!inverse) {
            col[0] = static_cast<std::uint8_t>(b.m2[a0] ^ b.m3[a1] ^ a2 ^ a3);
            col[1] = static_cast<std::uint8_t>(a0 ^ b.m2[a1] ^ b.m3[a2] ^ a3);
            col[2] = static_cast<std::uint8_t>(a0 ^ a1 ^ b.m2[a2] ^ b.m3[a3]);
            col[3] = static_cast<std::uint8_t>(b.m3[a0] ^ a1 ^ a2 ^ b.m2[a3]);
        } else {
            col[0] = static_cast<std::uint8_t>(b.m14[a0] ^ b.m11[a1] ^ b.m13[a2] ^ b.m9[a3]);
            col[1] = static_cast<std::uint8_t>(b.m9[a0] ^ b.m14[a1] ^ b.m11[a2] ^ b.m13[a3]);
            col[2] = static_cast<std::uint8_t>(b.m13[a0] ^ b.m9[a1] ^ b.m14[a2] ^ b.m11[a3]);
            col[3] = static_cast<std::uint8_t>(b.m11[a0] ^ b.m13[a1] ^ b.m9[a2] ^ b.m14[a3]);
        }
    }
}

}  // namespace

Aes128::Aes128(const std::uint8_t key[16]) {
    const Boxes& b = boxes();
    std::memcpy(round_keys_[0], key, 16);
    std::uint8_t rcon = 1;
    for (int r = 1; r <= 10; ++r) {
        const std::uint8_t* prev = round_keys_[r - 1];
        std::uint8_t* k = round_keys_[r];
        std::uint8_t t[4] = {b.s[prev[13]], b.s[prev[14]], b.s[prev[15]], b.s[prev[12]]};
        t[0] ^= rcon;
        rcon = xtime(rcon);
        for (int i = 0; i < 4; ++i) k[i] = prev[i] ^ t[i];
        for (int i = 4; i < 16; ++i) k[i] = prev[i] ^ k[i - 4];
    }
}

void Aes128::encrypt_block(const std::uint8_t in[16], std::uint8_t out[16]) const {
    const Boxes& b = boxes();
    std::uint8_t st[16];
    std::memcpy(st, in, 16);
    add_key(st, round_keys_[0]);
    for (int r = 1; r <= 10; ++r) {
        for (std::uint8_t& v : st) v = b.s[v];
        shift_rows(st, false);
        if (r != 10) mix_columns(st, false);
        add_key(st, round_keys_[r]);
    }
    std::memcpy(out, st, 16);
}

void Aes128::decrypt_block(const std::uint8_t in[16], std::uint8_t out[16]) const {
    const Boxes& b = boxes();
    std::uint8_t st[16];
    std::memcpy(st, in, 16);
    add_key(st, round_keys_[10]);
    for (int r = 9; r >= 0; --r) {
        shift_rows(st, true);
        for (std::uint8_t& v : st) v = b.inv[v];
        add_key(st, round_keys_[r]);
        if (r != 0) mix_columns(st, true);
    }
    std::memcpy(out, st, 16);
}

void Aes128::cbc_decrypt(const std::uint8_t iv[16], const std::uint8_t* in, std::uint8_t* out, std::size_t length) const {
    std::uint8_t chain[16], next[16], block[16];
    std::memcpy(chain, iv, 16);
    for (std::size_t i = 0; i + 16 <= length; i += 16) {
        std::memcpy(next, in + i, 16);
        decrypt_block(next, block);
        for (int k = 0; k < 16; ++k) out[i + k] = block[k] ^ chain[k];
        std::memcpy(chain, next, 16);
    }
}

void Aes128::cbc_encrypt(const std::uint8_t iv[16], const std::uint8_t* in, std::uint8_t* out, std::size_t length) const {
    std::uint8_t chain[16], block[16];
    std::memcpy(chain, iv, 16);
    for (std::size_t i = 0; i + 16 <= length; i += 16) {
        for (int k = 0; k < 16; ++k) block[k] = in[i + k] ^ chain[k];
        encrypt_block(block, chain);
        std::memcpy(out + i, chain, 16);
    }
}

bool decrypt_title_key(const std::uint8_t* ticket, std::size_t size, const std::uint8_t common_key[16],
                       std::uint8_t title_key[16], std::string& error) {
    if (!ticket || size < kTicketBytes) {
        error = "ticket too short";
        return false;
    }
    if (ticket[0x1F1] != 0) {
        error = "the game's ticket uses another common key (index " + std::to_string(ticket[0x1F1]) + ")";
        return false;
    }
    std::uint8_t iv[16] = {};
    std::memcpy(iv, ticket + 0x1DC, 8);
    Aes128(common_key).cbc_decrypt(iv, ticket + 0x1BF, title_key, 16);
    return true;
}

WiiPartitionSource::WiiPartitionSource(const ByteSource& disc, std::uint64_t data_offset, std::uint64_t data_size,
                                       const std::uint8_t title_key[16])
    : disc_(disc),
      data_offset_(data_offset),
      size_(data_size / kClusterBytes * kClusterDataBytes),
      aes_(title_key),
      cluster_(kClusterBytes) {}

bool WiiPartitionSource::read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const {
    if (offset > size_ || length > size_ - offset) return false;
    while (length > 0) {
        const std::uint64_t c = offset / kClusterDataBytes;
        const std::size_t within = static_cast<std::size_t>(offset % kClusterDataBytes);
        if (c != cached_) {
            cached_ = UINT64_MAX;
            if (!disc_.read(data_offset_ + c * kClusterBytes, cluster_.data(), kClusterBytes)) return false;
            std::uint8_t iv[16];
            std::memcpy(iv, cluster_.data() + 0x3D0, 16);
            aes_.cbc_decrypt(iv, cluster_.data() + 0x400, cluster_.data() + 0x400, kClusterDataBytes);
            cached_ = c;
        }
        const std::size_t n = std::min(length, kClusterDataBytes - within);
        std::memcpy(destination, cluster_.data() + 0x400 + within, n);
        destination += n;
        offset += n;
        length -= n;
    }
    return true;
}

bool open_game_partition(const ByteSource& disc, const std::uint8_t common_key[16],
                         std::unique_ptr<WiiPartitionSource>& out, std::string& error) {
    std::vector<PartitionEntry> table;
    PartitionEntry game;
    PartitionHeader header;
    if (!read_partition_table(disc, table, error)) return false;
    if (!find_game_partition(table, game)) {
        error = "the disc has no game partition";
        return false;
    }
    if (!read_partition_header(disc, game.offset, header, error)) return false;
    std::vector<std::uint8_t> ticket(kTicketBytes);
    if (!disc.read(game.offset, ticket.data(), ticket.size())) {
        error = "cannot read the game's ticket";
        return false;
    }
    std::uint8_t key[16];
    if (!decrypt_title_key(ticket.data(), ticket.size(), common_key, key, error)) return false;
    out.reset(new WiiPartitionSource(disc, header.data_offset, header.data_size, key));
    return true;
}

}  // namespace riftwii
