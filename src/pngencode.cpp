// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/pngencode.hpp"

#include <cstdlib>
#include <cstring>
#include <new>

#include "riftwii/pngdecode.hpp"

namespace riftwii {
namespace {

constexpr std::uint32_t kWindow = 32768;         // deflate's farthest match
constexpr std::uint32_t kRing = 65536;           // window + lookahead, a power of two
constexpr std::uint32_t kMaxMatch = 258;
constexpr std::uint32_t kLookahead = kMaxMatch + 3;
constexpr std::uint32_t kHashBits = 15;
constexpr std::uint32_t kChain = 16;             // candidates tried per position
constexpr std::size_t kIdatBytes = 65536;        // image data per IDAT chunk

// RFC 1951 section 3.2.5: base value and extra bits of the length codes
// 257-285 and the distance codes 0-29.
constexpr std::uint16_t kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                           31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                           2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                         33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                         1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                         6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

std::uint32_t Reverse(std::uint32_t code, unsigned bits) {
    std::uint32_t r = 0;
    for (unsigned i = 0; i < bits; ++i) r |= ((code >> i) & 1u) << (bits - 1 - i);
    return r;
}

void Be32(std::uint8_t* p, std::uint32_t v) {
    p[0] = std::uint8_t(v >> 24);
    p[1] = std::uint8_t(v >> 16);
    p[2] = std::uint8_t(v >> 8);
    p[3] = std::uint8_t(v);
}

}  // namespace

struct PngWriter::Impl {
    Sink sink;
    void* user;
    std::uint32_t width, height, rows = 0;
    bool ok = true;
    std::size_t row_bytes;
    std::uint8_t* prev;       // the previous row, unfiltered (zeros before the first)
    std::uint8_t* cand[4];    // the row under None, Sub, Up and Paeth

    // Deflate state: bytes are numbered from 0 as they come in.
    std::uint8_t ring[kRing];
    std::uint32_t head[1u << kHashBits];  // hash -> last position + 1 (0: none)
    std::uint32_t chain[kWindow];         // position % window -> the one before with its hash, + 1
    std::uint32_t end = 0;                // bytes taken
    std::uint32_t pos = 0;                // bytes encoded
    std::uint32_t adler_a = 1, adler_b = 0;
    std::uint32_t bits = 0;
    unsigned bit_count = 0;
    std::uint16_t lit_code[288];
    std::uint8_t lit_len[288];

    // The IDAT being filled: its type, then up to kIdatBytes of data.
    std::uint8_t idat[4 + kIdatBytes];
    std::size_t idat_used = 0;

    bool Put(const std::uint8_t* data, std::size_t size) {
        if (ok && size != 0 && !sink(user, data, size)) ok = false;
        return ok;
    }

    bool Chunk(const char* type, const std::uint8_t* data, std::size_t size) {
        std::uint8_t head_bytes[8];
        Be32(head_bytes, static_cast<std::uint32_t>(size));
        std::memcpy(head_bytes + 4, type, 4);
        std::uint8_t* joined = static_cast<std::uint8_t*>(std::malloc(size + 4));
        if (!joined) return ok = false;
        std::memcpy(joined, type, 4);
        if (size) std::memcpy(joined + 4, data, size);
        std::uint8_t crc[4];
        Be32(crc, png_crc32(joined, size + 4));
        std::free(joined);
        return Put(head_bytes, 8) && Put(data, size) && Put(crc, 4);
    }

    void FlushIdat() {
        if (idat_used == 0 || !ok) return;
        std::uint8_t length[4];
        Be32(length, static_cast<std::uint32_t>(idat_used));
        std::uint8_t crc[4];
        Be32(crc, png_crc32(idat, idat_used + 4));
        Put(length, 4) && Put(idat, idat_used + 4) && Put(crc, 4);
        idat_used = 0;
    }

    void Byte(std::uint8_t b) {
        idat[4 + idat_used++] = b;
        if (idat_used == kIdatBytes) FlushIdat();
    }

    void Bits(std::uint32_t value, unsigned count) {
        bits |= value << bit_count;
        bit_count += count;
        while (bit_count >= 8) {
            Byte(std::uint8_t(bits));
            bits >>= 8;
            bit_count -= 8;
        }
    }

    void Symbol(unsigned s) { Bits(lit_code[s], lit_len[s]); }

    std::uint32_t Hash(std::uint32_t p) const {
        const std::uint32_t a = ring[p & (kRing - 1)], b = ring[(p + 1) & (kRing - 1)], c = ring[(p + 2) & (kRing - 1)];
        return ((a << 10) ^ (b << 5) ^ c) & ((1u << kHashBits) - 1);
    }

    void Insert(std::uint32_t p) {
        const std::uint32_t h = Hash(p);
        chain[p & (kWindow - 1)] = head[h];
        head[h] = p + 1;
    }

    void Match(std::uint32_t length, std::uint32_t distance) {
        unsigned l = 0;
        while (l < 28 && kLengthBase[l + 1] <= length) ++l;
        Symbol(257 + l);
        if (kLengthExtra[l]) Bits(length - kLengthBase[l], kLengthExtra[l]);
        unsigned d = 0;
        while (d < 29 && kDistBase[d + 1] <= distance) ++d;
        Bits(Reverse(d, 5), 5);
        if (kDistExtra[d]) Bits(distance - kDistBase[d], kDistExtra[d]);
    }

    // Encodes while more than a lookahead is waiting (everything when `all`).
    void Encode(bool all) {
        while (end - pos > (all ? 0u : kLookahead)) {
            const std::uint32_t avail = end - pos;
            std::uint32_t best = 0, best_distance = 0;
            if (avail >= 3) {
                const std::uint32_t limit = avail < kMaxMatch ? avail : kMaxMatch;
                std::uint32_t cand = head[Hash(pos)];
                for (std::uint32_t tries = 0; cand != 0 && tries < kChain; ++tries) {
                    const std::uint32_t c = cand - 1;
                    const std::uint32_t distance = pos - c;
                    if (distance > kWindow || distance == 0) break;
                    std::uint32_t len = 0;
                    while (len < limit && ring[(c + len) & (kRing - 1)] == ring[(pos + len) & (kRing - 1)]) ++len;
                    if (len > best) {
                        best = len;
                        best_distance = distance;
                        if (len == limit) break;
                    }
                    cand = chain[c & (kWindow - 1)];
                }
                Insert(pos);
            }
            if (best >= 3) {
                Match(best, best_distance);
                for (std::uint32_t i = 1; i < best; ++i)
                    if (end - (pos + i) >= 3) Insert(pos + i);
                pos += best;
            } else {
                Symbol(ring[pos & (kRing - 1)]);
                ++pos;
            }
        }
    }

    void Take(const std::uint8_t* data, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) {
            ring[end & (kRing - 1)] = data[i];
            ++end;
            adler_a += data[i];
            if (adler_a >= 65521) adler_a -= 65521;
            adler_b += adler_a;
            if (adler_b >= 65521) adler_b -= 65521;
            if (end - pos > kLookahead) Encode(false);
        }
    }
};

std::size_t PngWriter::work_bytes() { return sizeof(Impl); }

PngWriter::PngWriter(std::uint32_t width, std::uint32_t height, Sink sink, void* user, void* work)
    : impl_(work ? new (work) Impl : new (std::nothrow) Impl), own_(work == nullptr) {
    if (!impl_) return;
    Impl& m = *impl_;
    m.sink = sink;
    m.user = user;
    m.width = width;
    m.height = height;
    m.row_bytes = static_cast<std::size_t>(width) * 3;
    m.prev = static_cast<std::uint8_t*>(std::calloc(m.row_bytes ? m.row_bytes : 1, 1));
    for (auto& c : m.cand) c = static_cast<std::uint8_t*>(std::malloc(m.row_bytes ? m.row_bytes : 1));
    std::memset(m.head, 0, sizeof(m.head));
    std::memcpy(m.idat, "IDAT", 4);
    // The fixed Huffman code (RFC 1951 section 3.2.6), bit-reversed for
    // the LSB-first stream.
    for (unsigned v = 0; v < 288; ++v) {
        unsigned code, len;
        if (v < 144) code = 0x30 + v, len = 8;
        else if (v < 256) code = 0x190 + (v - 144), len = 9;
        else if (v < 280) code = v - 256, len = 7;
        else code = 0xC0 + (v - 280), len = 8;
        m.lit_code[v] = static_cast<std::uint16_t>(Reverse(code, len));
        m.lit_len[v] = static_cast<std::uint8_t>(len);
    }
    if (!m.prev || !m.cand[0] || !m.cand[1] || !m.cand[2] || !m.cand[3] || width == 0 || height == 0) {
        m.ok = false;
        return;
    }
    static const std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::uint8_t ihdr[13] = {};
    Be32(ihdr, width);
    Be32(ihdr + 4, height);
    ihdr[8] = 8;  // bits a sample
    ihdr[9] = 2;  // RGB
    if (!m.Put(kSignature, 8) || !m.Chunk("IHDR", ihdr, sizeof(ihdr))) return;
    m.Byte(0x78);  // zlib: deflate, 32 KiB window
    m.Byte(0x01);
    m.Bits(1, 1);  // the last block,
    m.Bits(1, 2);  // fixed Huffman
}

PngWriter::~PngWriter() {
    if (!impl_) return;
    std::free(impl_->prev);
    for (auto& c : impl_->cand) std::free(c);
    if (own_) delete impl_;
    else impl_->~Impl();
}

bool PngWriter::add_row(const std::uint8_t* rgb) {
    if (!impl_) return false;
    Impl& m = *impl_;
    if (!m.ok || m.rows >= m.height) return m.ok = false;
    const std::size_t n = m.row_bytes;
    std::uint32_t sum[4] = {};
    for (std::size_t i = 0; i < n; ++i) {
        const int x = rgb[i], a = i >= 3 ? rgb[i - 3] : 0, b = m.prev[i], c = i >= 3 ? m.prev[i - 3] : 0;
        const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
        const int paeth = (pa <= pb && pa <= pc) ? a : pb <= pc ? b : c;
        const std::uint8_t v[4] = {std::uint8_t(x), std::uint8_t(x - a), std::uint8_t(x - b), std::uint8_t(x - paeth)};
        for (int f = 0; f < 4; ++f) {
            m.cand[f][i] = v[f];
            sum[f] += static_cast<std::uint32_t>(std::abs(static_cast<int>(static_cast<std::int8_t>(v[f]))));
        }
    }
    int best = 0;
    for (int f = 1; f < 4; ++f)
        if (sum[f] < sum[best]) best = f;
    static const std::uint8_t kFilter[4] = {0, 1, 2, 4};  // None, Sub, Up, Paeth
    m.Take(&kFilter[best], 1);
    m.Take(m.cand[best], n);
    std::memcpy(m.prev, rgb, n);
    ++m.rows;
    return m.ok;
}

bool PngWriter::finish() {
    if (!impl_) return false;
    Impl& m = *impl_;
    if (!m.ok || m.rows != m.height) return m.ok = false;
    m.Encode(true);
    m.Symbol(256);
    if (m.bit_count) m.Bits(0, 8 - m.bit_count);
    const std::uint32_t adler = (m.adler_b << 16) | m.adler_a;
    for (int s = 24; s >= 0; s -= 8) m.Byte(std::uint8_t(adler >> s));
    m.FlushIdat();
    m.Chunk("IEND", nullptr, 0);
    m.rows = m.height + 1;  // nothing more
    return m.ok;
}

namespace {
bool AppendSink(void* user, const std::uint8_t* data, std::size_t size) {
    auto* out = static_cast<std::vector<std::uint8_t>*>(user);
    out->insert(out->end(), data, data + size);
    return true;
}
}  // namespace

bool encode_png_rgb(const std::uint8_t* rgb, std::uint32_t width, std::uint32_t height,
                    std::vector<std::uint8_t>& out) {
    out.clear();
    PngWriter w(width, height, AppendSink, &out);
    for (std::uint32_t y = 0; y < height; ++y)
        if (!w.add_row(rgb + static_cast<std::size_t>(y) * width * 3)) return false;
    return w.finish();
}

}  // namespace riftwii
