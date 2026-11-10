// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpefa.hpp"

#include <cstring>
#include <deque>
#include <memory>

namespace riftwii {
namespace {

struct Tables {
    std::uint8_t sbox[256];
    std::uint8_t inv[256];
    std::uint8_t m9[256], m11[256], m13[256], m14[256];
    std::uint8_t rcon[11];
    Tables() {
        // S-box from the multiplicative inverse and the affine map.
        std::uint8_t p = 1, q = 1;
        do {
            p = static_cast<std::uint8_t>(p ^ (p << 1) ^ ((p & 0x80) ? 0x1B : 0));
            q ^= q << 1;
            q ^= q << 2;
            q ^= q << 4;
            if (q & 0x80) q ^= 0x09;
            const std::uint8_t x = static_cast<std::uint8_t>(
                q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^ ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4)));
            sbox[p] = static_cast<std::uint8_t>(x ^ 0x63);
        } while (p != 1);
        sbox[0] = 0x63;
        for (int i = 0; i < 256; ++i) inv[sbox[i]] = static_cast<std::uint8_t>(i);
        for (int i = 0; i < 256; ++i) {
            m9[i] = mul(static_cast<std::uint8_t>(i), 9);
            m11[i] = mul(static_cast<std::uint8_t>(i), 11);
            m13[i] = mul(static_cast<std::uint8_t>(i), 13);
            m14[i] = mul(static_cast<std::uint8_t>(i), 14);
        }
        std::uint8_t r = 1;
        for (int i = 1; i < 11; ++i) {
            rcon[i] = r;
            r = static_cast<std::uint8_t>((r << 1) ^ ((r & 0x80) ? 0x1B : 0));
        }
    }
    static std::uint8_t mul(std::uint8_t a, std::uint8_t b) {
        std::uint8_t out = 0;
        while (b) {
            if (b & 1) out ^= a;
            a = static_cast<std::uint8_t>((a << 1) ^ ((a & 0x80) ? 0x1B : 0));
            b >>= 1;
        }
        return out;
    }
};

const Tables& tables() {
    static const Tables t;
    return t;
}

}  // namespace

Aes128Decrypt::Aes128Decrypt(const std::array<std::uint8_t, 16>& key) {
    const Tables& t = tables();
    std::memcpy(round_keys_[0], key.data(), 16);
    for (int r = 1; r < 11; ++r) {
        const std::uint8_t* prev = round_keys_[r - 1];
        std::uint8_t* cur = round_keys_[r];
        std::uint8_t w[4] = {t.sbox[prev[13]], t.sbox[prev[14]], t.sbox[prev[15]], t.sbox[prev[12]]};
        w[0] ^= t.rcon[r];
        for (int i = 0; i < 4; ++i) cur[i] = prev[i] ^ w[i];
        for (int i = 4; i < 16; ++i) cur[i] = prev[i] ^ cur[i - 4];
    }
}

void Aes128Decrypt::decrypt_block(const std::uint8_t in[16], std::uint8_t out[16]) const {
    const Tables& t = tables();
    std::uint8_t s[16];
    for (int i = 0; i < 16; ++i) s[i] = in[i] ^ round_keys_[10][i];
    for (int round = 9; round >= 0; --round) {
        // InvShiftRows + InvSubBytes
        std::uint8_t u[16];
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) u[4 * ((c + r) & 3) + r] = t.inv[s[4 * c + r]];
        for (int i = 0; i < 16; ++i) u[i] ^= round_keys_[round][i];
        if (round == 0) {
            std::memcpy(s, u, 16);
            break;
        }
        for (int c = 0; c < 4; ++c) {
            const std::uint8_t a = u[4 * c], b = u[4 * c + 1], cc = u[4 * c + 2], d = u[4 * c + 3];
            s[4 * c] = t.m14[a] ^ t.m11[b] ^ t.m13[cc] ^ t.m9[d];
            s[4 * c + 1] = t.m9[a] ^ t.m14[b] ^ t.m11[cc] ^ t.m13[d];
            s[4 * c + 2] = t.m13[a] ^ t.m9[b] ^ t.m14[cc] ^ t.m11[d];
            s[4 * c + 3] = t.m11[a] ^ t.m13[b] ^ t.m9[cc] ^ t.m14[d];
        }
    }
    std::memcpy(out, s, 16);
}

void Aes128Decrypt::decrypt_cbc(const std::uint8_t iv[16], const std::uint8_t* in, std::uint8_t* out,
                                std::size_t length) const {
    std::uint8_t prev[16], cipher[16], plain[16];
    std::memcpy(prev, iv, 16);
    for (std::size_t at = 0; at + 16 <= length; at += 16) {
        std::memcpy(cipher, in + at, 16);  // `in` may be `out`
        decrypt_block(cipher, plain);
        for (int i = 0; i < 16; ++i) out[at + i] = plain[i] ^ prev[i];
        std::memcpy(prev, cipher, 16);
    }
}

std::array<std::uint8_t, 16> ctgp_efa_chunk_iv(const CtgpChunkIv& iv, std::uint32_t sector) {
    std::array<std::uint8_t, 16> out{};
    const std::uint32_t words[4] = {iv.w0, iv.w1, sector, iv.w3};
    for (int w = 0; w < 4; ++w)
        for (int b = 0; b < 4; ++b) out[4 * w + b] = static_cast<std::uint8_t>(words[w] >> (24 - 8 * b));
    return out;
}

namespace {

struct EfaState {
    ByteReader raw;
    std::uint64_t size = 0;
    Aes128Decrypt aes;
    CtgpChunkIv iv;
    struct Chunk {
        std::uint32_t index;
        std::vector<std::uint8_t> bytes;
    };
    std::deque<Chunk> chunks;  // most recent first
    EfaState(ByteReader r, std::uint64_t s, const std::array<std::uint8_t, 16>& k, const CtgpChunkIv& v)
        : raw(std::move(r)), size(s), aes(k), iv(v) {}

    const Chunk* chunk(std::uint32_t index) {
        for (const Chunk& c : chunks)
            if (c.index == index) return &c;
        const std::uint64_t start = std::uint64_t(index) * kCtgpEfaChunkBytes;
        if (start >= size) return nullptr;
        const std::uint32_t length = static_cast<std::uint32_t>(
            size - start < kCtgpEfaChunkBytes ? size - start : kCtgpEfaChunkBytes);
        Chunk c;
        c.index = index;
        c.bytes.resize(length);
        if (!raw(start, length, c.bytes.data())) return nullptr;
        const auto chunk_iv = ctgp_efa_chunk_iv(iv, index * kCtgpEfaChunkSectors);
        aes.decrypt_cbc(chunk_iv.data(), c.bytes.data(), c.bytes.data(), length & ~15u);
        chunks.push_front(std::move(c));
        if (chunks.size() > 4) chunks.pop_back();
        return &chunks.front();
    }
};

}  // namespace

BlockReader ctgp_efa_block_reader(ByteReader raw, std::uint64_t size, const std::array<std::uint8_t, 16>& key,
                                  const CtgpChunkIv& iv) {
    auto state = std::make_shared<EfaState>(std::move(raw), size, key, iv);
    return [state](std::uint64_t lba, std::uint32_t count, std::uint8_t* out) {
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint64_t sector = lba + i;
            const std::uint64_t chunk_index = sector / kCtgpEfaChunkSectors;
            if (chunk_index > 0xFFFFFFFFull) return false;
            const EfaState::Chunk* c = state->chunk(static_cast<std::uint32_t>(chunk_index));
            if (!c) return false;
            const std::size_t at = static_cast<std::size_t>(sector % kCtgpEfaChunkSectors) * 512;
            if (at + 512 > c->bytes.size()) return false;
            std::memcpy(out + std::size_t(i) * 512, c->bytes.data() + at, 512);
        }
        return true;
    };
}


std::uint32_t ctgp_fat_seconds(std::uint16_t time, std::uint16_t date) {
    const int sec = (time & 0x1F) * 2, min = (time >> 5) & 0x3F, hour = time >> 11;
    const int mday = date & 0x1F, mon = ((date >> 5) & 0xF) - 1, year = (date >> 9) + 1980;
    // days from 1970-01-01 to the date (civil-from-days, proleptic Gregorian)
    int y = year;
    const int m = mon + 1;
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + mday - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long days = static_cast<long>(era) * 146097 + doe - 719468;
    return static_cast<std::uint32_t>(days * 86400 + hour * 3600 + min * 60 + sec);
}

std::array<std::uint8_t, 16> ctgp_efb_key(const CtgpEfbKeyWords& words, std::uint16_t write_time, std::uint16_t write_date,
                                          std::uint16_t create_time, std::uint16_t create_date) {
    const std::uint32_t mtime = ctgp_fat_seconds(write_time, write_date);
    const std::uint32_t ctime = ctgp_fat_seconds(create_time, create_date);
    const std::uint32_t key_words[4] = {mtime, words.w1, ctime, words.w3};
    std::array<std::uint8_t, 16> key{};
    for (int w = 0; w < 4; ++w)
        for (int b = 0; b < 4; ++b) key[4 * w + b] = static_cast<std::uint8_t>(key_words[w] >> (24 - 8 * b));
    return key;
}

}  // namespace riftwii
