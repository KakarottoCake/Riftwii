// SPDX-License-Identifier: GPL-3.0-or-later
// The cover art's PNG reader: every colour type and depth, the row
// filters, Adam7, transparency and gamma, against images this file
// writes itself (zlib "stored" blocks, so the host needs no zlib).
#include "riftwii/pngdecode.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << +(a) << " != " << +(b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using Bytes = std::vector<std::uint8_t>;

namespace {

void Put32(Bytes& out, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(std::uint8_t(v >> s));
}

void Chunk(Bytes& png, const char* type, const Bytes& data, bool break_crc = false) {
    Put32(png, std::uint32_t(data.size()));
    const std::size_t start = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    std::uint32_t crc = riftwii::png_crc32(png.data() + start, data.size() + 4);
    if (break_crc) crc ^= 1;
    Put32(png, crc);
}

// A zlib stream of stored blocks.
Bytes Zlib(const Bytes& raw) {
    Bytes z = {0x78, 0x01};
    std::size_t at = 0;
    do {
        const std::size_t n = std::min<std::size_t>(raw.size() - at, 65535);
        z.push_back(at + n == raw.size() ? 1 : 0);
        z.push_back(std::uint8_t(n));
        z.push_back(std::uint8_t(n >> 8));
        z.push_back(std::uint8_t(~n));
        z.push_back(std::uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + n);
        at += n;
    } while (at < raw.size());
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    Put32(z, (b << 16) | a);
    return z;
}

// The host's inflater: stored blocks only.
bool StoredInflate(const std::uint8_t* data, std::size_t size, std::uint8_t* out, std::size_t out_size) {
    if (size < 2) return false;
    std::size_t at = 2, got = 0;
    for (;;) {
        if (size - at < 5) return false;
        const bool last = data[at] & 1;
        if ((data[at] >> 1) & 3) return false;
        const std::size_t n = data[at + 1] | (data[at + 2] << 8);
        at += 5;
        if (size - at < n) return false;
        const std::size_t take = std::min(n, out_size - got);
        std::memcpy(out + got, data + at, take);
        got += take;
        at += n;
        if (last) break;
    }
    return got == out_size;
}

unsigned PaethOf(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

// Filters rows of `row_bytes` (row y with filter y % 5, or `only`).
Bytes Filter(const Bytes& rows, std::size_t row_bytes, std::size_t bpp, int only = -1) {
    Bytes out;
    const std::size_t count = row_bytes ? rows.size() / row_bytes : 0;
    for (std::size_t y = 0; y < count; ++y) {
        const std::uint8_t* x = &rows[y * row_bytes];
        const std::uint8_t* up = y ? &rows[(y - 1) * row_bytes] : nullptr;
        const int f = only >= 0 ? only : int(y % 5);
        out.push_back(std::uint8_t(f));
        for (std::size_t i = 0; i < row_bytes; ++i) {
            const int left = i >= bpp ? x[i - bpp] : 0, above = up ? up[i] : 0,
                      corner = up && i >= bpp ? up[i - bpp] : 0;
            const int predict = f == 0 ? 0 : f == 1 ? left : f == 2 ? above : f == 3 ? (left + above) / 2
                                                                                  : int(PaethOf(left, above, corner));
            out.push_back(std::uint8_t(x[i] - predict));
        }
    }
    return out;
}

struct Image {
    std::uint32_t w = 0, h = 0;
    unsigned depth = 8, type = 2;
    Bytes samples;  // packed rows, as the file holds them (no filter bytes)
};

unsigned Channels(unsigned type) { return type == 2 ? 3 : type == 4 ? 2 : type == 6 ? 4 : 1; }
std::size_t RowBytes(const Image& im, std::uint32_t w) { return (w * Channels(im.type) * im.depth + 7) / 8; }
std::size_t Bpp(const Image& im) { return im.depth < 8 ? 1 : Channels(im.type) * im.depth / 8; }

// Bits `depth` wide at pixel sample `index` of a packed row.
unsigned GetBits(const std::uint8_t* row, std::size_t index, unsigned depth) {
    if (depth == 16) return (row[index * 2] << 8) | row[index * 2 + 1];
    if (depth == 8) return row[index];
    const std::size_t bit = index * depth;
    return (row[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
}
void SetBits(std::uint8_t* row, std::size_t index, unsigned depth, unsigned v) {
    if (depth == 16) {
        row[index * 2] = std::uint8_t(v >> 8);
        row[index * 2 + 1] = std::uint8_t(v);
        return;
    }
    if (depth == 8) {
        row[index] = std::uint8_t(v);
        return;
    }
    const std::size_t bit = index * depth;
    row[bit / 8] |= std::uint8_t(v << (8 - depth - bit % 8));
}

// The IDAT data: whole rows, or Adam7's seven passes.
Bytes Raw(const Image& im, bool interlace, int only_filter = -1) {
    static const unsigned kPass[7][4] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
                                         {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
    const std::size_t full = RowBytes(im, im.w);
    const unsigned ch = Channels(im.type);
    if (!interlace) return Filter(im.samples, full, Bpp(im), only_filter);
    Bytes out;
    for (const auto& p : kPass) {
        if (im.w <= p[0] || im.h <= p[1]) continue;
        const std::uint32_t pw = (im.w - p[0] + p[2] - 1) / p[2], ph = (im.h - p[1] + p[3] - 1) / p[3];
        const std::size_t rb = RowBytes(im, pw);
        Bytes rows(rb * ph, 0);
        for (std::uint32_t py = 0; py < ph; ++py)
            for (std::uint32_t px = 0; px < pw; ++px)
                for (unsigned c = 0; c < ch; ++c) {
                    const unsigned v = GetBits(&im.samples[(p[1] + py * p[3]) * full], (p[0] + px * p[2]) * ch + c,
                                               im.depth);
                    SetBits(&rows[py * rb], px * ch + c, im.depth, v);
                }
        const Bytes f = Filter(rows, rb, Bpp(im), only_filter);
        out.insert(out.end(), f.begin(), f.end());
    }
    return out;
}

struct Extra {
    const char* type;
    Bytes data;
    bool after_idat = false;
    bool break_crc = false;
};

Bytes Png(const Image& im, bool interlace, const std::vector<Extra>& extras = {}, int split_idat = 1,
          bool break_idat_crc = false) {
    Bytes png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    Bytes ihdr;
    Put32(ihdr, im.w);
    Put32(ihdr, im.h);
    ihdr.push_back(std::uint8_t(im.depth));
    ihdr.push_back(std::uint8_t(im.type));
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(interlace ? 1 : 0);
    Chunk(png, "IHDR", ihdr);
    for (const Extra& e : extras)
        if (!e.after_idat) Chunk(png, e.type, e.data, e.break_crc);
    const Bytes z = Zlib(Raw(im, interlace));
    const std::size_t part = (z.size() + split_idat - 1) / split_idat;
    for (std::size_t at = 0; at < z.size(); at += part)
        Chunk(png, "IDAT", Bytes(z.begin() + at, z.begin() + std::min(z.size(), at + part)), break_idat_crc);
    for (const Extra& e : extras)
        if (e.after_idat) Chunk(png, e.type, e.data, e.break_crc);
    Chunk(png, "IEND", {});
    return png;
}

bool Decode(const Bytes& png, Bytes& rgba, std::uint32_t& w, std::uint32_t& h, std::string& error) {
    return riftwii::decode_png(png.data(), png.size(), &StoredInflate, 1024, rgba, w, h, error);
}

// A w x h image of the given format whose samples are a fixed pattern.
Image Pattern(std::uint32_t w, std::uint32_t h, unsigned type, unsigned depth) {
    Image im;
    im.w = w;
    im.h = h;
    im.type = type;
    im.depth = depth;
    const std::size_t rb = RowBytes(im, w);
    im.samples.assign(rb * h, 0);
    const unsigned ch = Channels(type), max = (1u << depth) - 1;
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x)
            for (unsigned c = 0; c < ch; ++c) {
                const unsigned v = (x * 37 + y * 101 + c * 53 + (x * y) * 7) & max;
                SetBits(&im.samples[y * rb], x * ch + c, depth, depth == 16 ? (v * 257) ^ (x * 3) : v);
            }
    return im;
}

}  // namespace

static void test_rgb8_filters() {
    // RGB, every filter (row y uses filter y % 5).
    const Image im = Pattern(7, 10, 2, 8);
    Bytes rgba;
    std::uint32_t w = 0, h = 0;
    std::string error;
    EXPECT_TRUE(Decode(Png(im, false), rgba, w, h, error));
    EXPECT_EQ(w, 7u);
    EXPECT_EQ(h, 10u);
    bool same = rgba.size() == 7u * 10 * 4;
    for (std::uint32_t i = 0; same && i < 70; ++i) {
        for (int c = 0; c < 3; ++c) same = same && rgba[i * 4 + c] == im.samples[i * 3 + c];
        same = same && rgba[i * 4 + 3] == 255;
    }
    EXPECT_TRUE(same);
    // Split IDATs join; a damaged ancillary chunk is skipped.
    Bytes again;
    EXPECT_TRUE(Decode(Png(im, false, {{"tEXt", {'a', 0, 'b'}, false, true}}, 3), again, w, h, error));
    EXPECT_TRUE(again == rgba);
    // Paeth's tie: left 0, up 30, corner 10 has |p-up| == |p-corner|, and
    // up wins.
    Bytes paeth = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    Bytes ihdr;
    Put32(ihdr, 2);
    Put32(ihdr, 2);
    ihdr.insert(ihdr.end(), {8, 0, 0, 0, 0});
    Chunk(paeth, "IHDR", ihdr);
    Chunk(paeth, "IDAT", Zlib(Filter({10, 30, 0, 77}, 2, 1, 4)));
    Chunk(paeth, "IEND", {});
    EXPECT_TRUE(Decode(paeth, rgba, w, h, error));
    EXPECT_EQ(rgba[12], 77);
}

static void test_adam7() {
    // Every format, interlaced and not, sizes that leave passes empty.
    const unsigned formats[][2] = {{0, 1}, {0, 2}, {0, 4}, {0, 8}, {0, 16}, {2, 8}, {2, 16}, {3, 1}, {3, 2},
                                   {3, 4}, {3, 8}, {4, 8}, {4, 16}, {6, 8}, {6, 16}};
    const std::uint32_t sizes[][2] = {{1, 1}, {3, 2}, {9, 10}, {17, 5}};
    for (const auto& f : formats) {
        for (const auto& s : sizes) {
            const Image im = Pattern(s[0], s[1], f[0], f[1]);
            std::vector<Extra> extras;
            if (f[0] == 3) {
                Bytes plte;
                for (unsigned i = 0; i < (1u << f[1]); ++i) {
                    plte.push_back(std::uint8_t(i * 3));
                    plte.push_back(std::uint8_t(255 - i));
                    plte.push_back(std::uint8_t(i * 7));
                }
                extras.push_back({"PLTE", plte});
            }
            // 16-bit files are read as sRGB here (see test_gamma for linear).
            if (f[1] == 16) extras.push_back({"sRGB", {0}});
            Bytes flat, laced;
            std::uint32_t w = 0, h = 0;
            std::string error;
            EXPECT_TRUE(Decode(Png(im, false, extras), flat, w, h, error));
            EXPECT_TRUE(Decode(Png(im, true, extras), laced, w, h, error));
            EXPECT_TRUE(flat == laced);
            EXPECT_EQ(flat.size(), std::size_t(s[0]) * s[1] * 4);
            // Pixel (x, y)'s samples, as libpng gives them.
            bool ok = true;
            const unsigned ch = Channels(f[0]);
            const std::size_t rb = RowBytes(im, im.w);
            for (std::uint32_t y = 0; y < im.h && ok; ++y)
                for (std::uint32_t x = 0; x < im.w && ok; ++x) {
                    const std::uint8_t* row = &im.samples[y * rb];
                    const std::uint8_t* px = &flat[(y * im.w + x) * 4];
                    auto to8 = [&](unsigned v) -> unsigned {
                        if (f[1] == 16) return (v * 255 + 32895) >> 16;
                        return f[1] < 8 && f[0] != 3 ? v * 255 / ((1u << f[1]) - 1) : v;
                    };
                    unsigned want[4] = {0, 0, 0, 255};
                    if (f[0] == 3) {
                        const unsigned i = GetBits(row, x, f[1]);
                        want[0] = (i * 3) & 255;
                        want[1] = 255 - i;
                        want[2] = (i * 7) & 255;
                    } else if (ch <= 2) {
                        want[0] = want[1] = want[2] = to8(GetBits(row, x * ch, f[1]));
                        if (ch == 2) want[3] = to8(GetBits(row, x * ch + 1, f[1]));
                    } else {
                        for (unsigned c = 0; c < ch; ++c) want[c] = to8(GetBits(row, x * ch + c, f[1]));
                    }
                    for (int c = 0; c < 4; ++c) ok = ok && px[c] == want[c];
                    if (!ok)
                        std::cerr << "format " << f[0] << "/" << f[1] << " size " << s[0] << "x" << s[1] << " pixel " << x
                                  << "," << y << std::endl;
                }
            EXPECT_TRUE(ok);
        }
    }
}

static void test_transparency() {
    std::uint32_t w = 0, h = 0;
    std::string error;
    Bytes rgba;
    // A palette with fewer alphas than colours: the rest are opaque.
    Image pal = Pattern(4, 1, 3, 2);
    pal.samples = {0x1B};  // indices 0, 1, 2, 3
    EXPECT_TRUE(Decode(Png(pal, false, {{"PLTE", {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}}, {"tRNS", {0, 128}}}),
                       rgba, w, h, error));
    EXPECT_EQ(rgba[3], 0);
    EXPECT_EQ(rgba[7], 128);
    EXPECT_EQ(rgba[11], 255);
    EXPECT_EQ(rgba[12], 10);
    // An index past the palette reads as black.
    EXPECT_TRUE(Decode(Png(pal, false, {{"PLTE", {1, 2, 3, 4, 5, 6}}}), rgba, w, h, error));
    EXPECT_EQ(rgba[8], 0);
    EXPECT_EQ(rgba[11], 255);
    // RGB with a transparent colour.
    Image rgb = Pattern(2, 1, 2, 8);
    rgb.samples = {10, 20, 30, 10, 20, 31};
    EXPECT_TRUE(Decode(Png(rgb, false, {{"tRNS", {0, 10, 0, 20, 0, 30}}}), rgba, w, h, error));
    EXPECT_EQ(rgba[3], 0);
    EXPECT_EQ(rgba[7], 255);
    // Gray at 2 bits: the key is the 2-bit value.
    Image gray = Pattern(4, 1, 0, 2);
    gray.samples = {0x1B};
    EXPECT_TRUE(Decode(Png(gray, false, {{"tRNS", {0, 2}}}), rgba, w, h, error));
    EXPECT_EQ(rgba[0], 0);
    EXPECT_EQ(rgba[4], 85);
    EXPECT_EQ(rgba[8], 170);
    EXPECT_EQ(rgba[11], 0);
    EXPECT_EQ(rgba[15], 255);
}

static void test_gamma() {
    std::uint32_t w = 0, h = 0;
    std::string error;
    Bytes rgba;
    Image gray = Pattern(3, 1, 0, 8);
    gray.samples = {0, 128, 255};
    // sRGB's own gamma (and near it): the samples as they are.
    EXPECT_TRUE(Decode(Png(gray, false, {{"gAMA", {0, 0, 0xB1, 0x8F}}}), rgba, w, h, error));
    EXPECT_EQ(rgba[4], 128);
    EXPECT_TRUE(Decode(Png(gray, false, {{"gAMA", {0, 0, 0xB0, 0x00}}}), rgba, w, h, error));  // 45056: within 5%
    EXPECT_EQ(rgba[4], 128);
    // A linear file: 255 * (128/255)^0.45455, rounded.
    EXPECT_TRUE(Decode(Png(gray, false, {{"gAMA", {0, 1, 0x86, 0xA0}}}), rgba, w, h, error));
    EXPECT_EQ(rgba[0], 0);
    EXPECT_EQ(rgba[4], 186);
    EXPECT_EQ(rgba[8], 255);
    // sRGB wins over a gAMA that follows it.
    EXPECT_TRUE(Decode(Png(gray, false, {{"sRGB", {0}}, {"gAMA", {0, 1, 0x86, 0xA0}}}), rgba, w, h, error));
    EXPECT_EQ(rgba[4], 128);
    // 16-bit without gAMA is linear, as libpng's simplified reader takes it.
    Image deep = Pattern(1, 1, 0, 16);
    deep.samples = {0x80, 0x00};
    EXPECT_TRUE(Decode(Png(deep, false), rgba, w, h, error));
    EXPECT_EQ(rgba[0], 186);
    EXPECT_TRUE(Decode(Png(deep, false, {{"sRGB", {0}}}), rgba, w, h, error));
    EXPECT_EQ(rgba[0], 128);  // (0x8000 * 255 + 32895) >> 16
    // Palettes take the gamma, their alphas do not.
    Image pal = Pattern(1, 1, 3, 8);
    pal.samples = {0};
    EXPECT_TRUE(Decode(Png(pal, false, {{"gAMA", {0, 1, 0x86, 0xA0}}, {"PLTE", {128, 0, 255}}, {"tRNS", {128}}}),
                       rgba, w, h, error));
    EXPECT_EQ(rgba[0], 186);
    EXPECT_EQ(rgba[3], 128);
}

static void test_refusals() {
    std::uint32_t w = 0, h = 0;
    std::string error;
    Bytes rgba;
    const Image im = Pattern(4, 4, 6, 8);
    Bytes png = Png(im, false);
    EXPECT_TRUE(Decode(png, rgba, w, h, error));
    // Truncated anywhere.
    for (std::size_t cut = 0; cut < png.size(); cut += 7) {
        EXPECT_TRUE(!Decode(Bytes(png.begin(), png.begin() + cut), rgba, w, h, error));
    }
    // A damaged IDAT.
    EXPECT_TRUE(!Decode(Png(im, false, {}, 1, true), rgba, w, h, error));
    // A bad filter byte.
    Bytes bad_filter = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    Bytes ihdr;
    Put32(ihdr, 1);
    Put32(ihdr, 1);
    ihdr.insert(ihdr.end(), {8, 0, 0, 0, 0});
    Chunk(bad_filter, "IHDR", ihdr);
    Chunk(bad_filter, "IDAT", Zlib({5, 0}));
    Chunk(bad_filter, "IEND", {});
    EXPECT_TRUE(!Decode(bad_filter, rgba, w, h, error));
    // Too big, a bad depth, palette images without a palette.
    Bytes big = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    Bytes big_ihdr;
    Put32(big_ihdr, 1025);
    Put32(big_ihdr, 1);
    big_ihdr.insert(big_ihdr.end(), {8, 0, 0, 0, 0});
    Chunk(big, "IHDR", big_ihdr);
    Chunk(big, "IEND", {});
    EXPECT_TRUE(!Decode(big, rgba, w, h, error));
    EXPECT_TRUE(!Decode(Png(Pattern(2, 2, 3, 8), false), rgba, w, h, error));
    Image odd = Pattern(2, 2, 2, 8);
    odd.depth = 4;
    EXPECT_TRUE(!Decode(Png(odd, false), rgba, w, h, error));
    // IDATs with a chunk between them.
    Bytes gap = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    Chunk(gap, "IHDR", ihdr);
    const Bytes z = Zlib({0, 7});
    Chunk(gap, "IDAT", Bytes(z.begin(), z.begin() + 4));
    Chunk(gap, "tEXt", {'a', 0});
    Chunk(gap, "IDAT", Bytes(z.begin() + 4, z.end()));
    Chunk(gap, "IEND", {});
    EXPECT_TRUE(!Decode(gap, rgba, w, h, error));
    EXPECT_EQ(riftwii::png_crc32(reinterpret_cast<const std::uint8_t*>("IEND"), 4), 0xAE426082u);
}

int main() {
    test_rgb8_filters();
    test_adam7();
    test_transparency();
    test_gamma();
    test_refusals();
    if (g_failures == 0) {
        std::cout << "ALL PNG TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
