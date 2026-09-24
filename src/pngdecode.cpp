// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/pngdecode.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace riftwii {
namespace {

std::uint32_t Be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

bool Fail(std::string& error, const char* why) {
    error = why;
    return false;
}

struct Header {
    std::uint32_t width = 0, height = 0;
    unsigned depth = 0, type = 0, interlace = 0;
    unsigned channels = 0;  // samples a pixel: 1 (gray, palette), 2, 3 or 4
};

bool ValidDepth(unsigned type, unsigned depth) {
    switch (type) {
    case 0: return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
    case 3: return depth == 1 || depth == 2 || depth == 4 || depth == 8;
    case 2:
    case 4:
    case 6: return depth == 8 || depth == 16;
    default: return false;
    }
}

// The bytes of one filtered row (without its filter byte) `w` pixels wide.
std::size_t RowBytes(const Header& h, std::uint32_t w) {
    return (static_cast<std::size_t>(w) * h.channels * h.depth + 7) / 8;
}

unsigned Paeth(unsigned a, unsigned b, unsigned c) {
    const int p = int(a) + int(b) - int(c);
    const int pa = std::abs(p - int(a)), pb = std::abs(p - int(b)), pc = std::abs(p - int(c));
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

// Undoes the filters of `rows` rows of `row_bytes` in place (each row
// keeps its leading filter byte).
bool Unfilter(std::uint8_t* data, std::uint32_t rows, std::size_t row_bytes, std::size_t bpp) {
    const std::uint8_t* prior = nullptr;
    for (std::uint32_t y = 0; y < rows; ++y) {
        std::uint8_t* row = data + y * (row_bytes + 1);
        const unsigned filter = row[0];
        std::uint8_t* x = row + 1;
        switch (filter) {
        case 0: break;
        case 1:
            for (std::size_t i = bpp; i < row_bytes; ++i) x[i] = std::uint8_t(x[i] + x[i - bpp]);
            break;
        case 2:
            if (prior)
                for (std::size_t i = 0; i < row_bytes; ++i) x[i] = std::uint8_t(x[i] + prior[i]);
            break;
        case 3:
            for (std::size_t i = 0; i < row_bytes; ++i) {
                const unsigned left = i >= bpp ? x[i - bpp] : 0;
                const unsigned up = prior ? prior[i] : 0;
                x[i] = std::uint8_t(x[i] + ((left + up) >> 1));
            }
            break;
        case 4:
            for (std::size_t i = 0; i < row_bytes; ++i) {
                const unsigned left = i >= bpp ? x[i - bpp] : 0;
                const unsigned up = prior ? prior[i] : 0;
                const unsigned corner = prior && i >= bpp ? prior[i - bpp] : 0;
                x[i] = std::uint8_t(x[i] + Paeth(left, up, corner));
            }
            break;
        default: return false;
        }
        prior = x;
    }
    return true;
}

// Sample `index` of a row at `depth` bits (16-bit samples whole).
unsigned Sample(const std::uint8_t* row, std::size_t index, unsigned depth) {
    switch (depth) {
    case 16: return (unsigned(row[index * 2]) << 8) | row[index * 2 + 1];
    case 8: return row[index];
    default: {
        const std::size_t bit = index * depth;
        const unsigned shift = 8 - depth - unsigned(bit % 8);
        return (row[bit / 8] >> shift) & ((1u << depth) - 1);
    }
    }
}

// Colour samples to 8 bits: libpng's gamma table when the file's gamma is
// not sRGB's, else the sample itself (16-bit ones rounded as libpng's
// scale_16 does).
struct ToByte {
    unsigned depth = 8;
    bool gamma = false;  // convert with `exponent`
    double exponent = 1;
    std::uint8_t table[256];

    void setup(unsigned d, std::uint32_t file_gamma) {
        depth = d;
        // libpng: the file gamma times sRGB's 2.2, significant outside 1 +- 0.05.
        const double combined = double(file_gamma) * 220000.0 / 1e10;
        gamma = combined < 0.95 || combined > 1.05;
        if (!gamma) return;
        // png_reciprocal2: the output exponent in 1/100000ths, rounded.
        exponent = std::floor(1e15 / (double(file_gamma) * 220000.0) + 0.5) / 1e5;
        for (unsigned v = 0; v < 256; ++v) {
            table[v] = v == 0 || v == 255 ? std::uint8_t(v)
                                           : std::uint8_t(std::floor(255 * std::pow(v / 255.0, exponent) + 0.5));
        }
    }
    std::uint8_t operator()(unsigned v) const {
        if (depth == 16) {
            if (!gamma) return std::uint8_t((v * 255 + 32895) >> 16);
            return std::uint8_t(std::floor(255 * std::pow(v / 65535.0, exponent) + 0.5));
        }
        if (depth < 8) v = v * 255 / ((1u << depth) - 1);  // 1, 2 and 4-bit gray
        return gamma ? table[v] : std::uint8_t(v);
    }
};

}  // namespace

std::uint32_t png_crc32(const std::uint8_t* data, std::size_t size) {
    static std::uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        ready = true;
    }
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool decode_png(const std::uint8_t* png, std::size_t size, ZlibInflate inflate, std::uint32_t max_side,
                std::vector<std::uint8_t>& rgba, std::uint32_t& width, std::uint32_t& height, std::string& error) {
    static const std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (size < 8 || std::memcmp(png, kSignature, 8) != 0) return Fail(error, "not a PNG");
    Header h;
    bool have_header = false, idat_done = false;
    std::vector<std::uint8_t> idat;
    std::uint8_t palette[256][3] = {};
    unsigned palette_size = 0;
    std::uint8_t palette_alpha[256];
    std::memset(palette_alpha, 255, sizeof(palette_alpha));
    bool have_key = false;  // tRNS for gray and RGB: the transparent colour
    unsigned key[3] = {};
    std::uint32_t file_gamma = 0;
    bool have_gamma = false, seen_srgb = false;
    std::size_t at = 8;
    for (;;) {
        if (size - at < 12) return Fail(error, "the PNG ends early");
        const std::uint32_t length = Be32(png + at);
        if (length > size - at - 12) return Fail(error, "the PNG ends early");
        const std::uint8_t* type = png + at + 4;
        const std::uint8_t* data = png + at + 8;
        const bool critical = (type[0] & 0x20) == 0;
        const bool crc_ok = png_crc32(type, length + 4) == Be32(data + length);
        at += 12 + std::size_t(length);
        auto named = [&](const char* name) { return std::memcmp(type, name, 4) == 0; };
        if (!crc_ok) {
            if (critical) return Fail(error, "a PNG chunk is damaged");
            continue;  // an ancillary chunk: skipped
        }
        if (!have_header && !named("IHDR")) return Fail(error, "the PNG does not start with IHDR");
        if (named("IHDR")) {
            if (have_header || length != 13) return Fail(error, "bad IHDR");
            h.width = Be32(data);
            h.height = Be32(data + 4);
            h.depth = data[8];
            h.type = data[9];
            h.interlace = data[12];
            if (h.width == 0 || h.height == 0 || !ValidDepth(h.type, h.depth) || data[10] != 0 || data[11] != 0 ||
                h.interlace > 1) {
                return Fail(error, "unsupported PNG format");
            }
            if (h.width > max_side || h.height > max_side) return Fail(error, "the PNG is too big");
            h.channels = h.type == 2 ? 3 : h.type == 4 ? 2 : h.type == 6 ? 4 : 1;
            have_header = true;
        } else if (named("PLTE")) {
            if (length % 3 != 0 || length == 0 || length / 3 > 256) {
                if (h.type == 3) return Fail(error, "bad PLTE");
                continue;
            }
            if (h.type == 0 || h.type == 4) continue;  // libpng ignores it for gray
            palette_size = length / 3;
            for (unsigned i = 0; i < palette_size; ++i) std::memcpy(palette[i], data + i * 3, 3);
        } else if (named("tRNS")) {
            if (h.type == 3) {
                for (unsigned i = 0; i < length && i < 256; ++i) palette_alpha[i] = data[i];
            } else if (h.type == 0 && length == 2) {
                key[0] = (unsigned(data[0]) << 8) | data[1];
                have_key = true;
            } else if (h.type == 2 && length == 6) {
                for (int c = 0; c < 3; ++c) key[c] = (unsigned(data[c * 2]) << 8) | data[c * 2 + 1];
                have_key = true;
            }
        } else if (named("gAMA")) {
            if (length == 4 && !seen_srgb && Be32(data) != 0) {
                file_gamma = Be32(data);
                have_gamma = true;
            }
        } else if (named("sRGB")) {
            file_gamma = 45455;
            have_gamma = seen_srgb = true;
        } else if (named("IDAT")) {
            if (idat_done) return Fail(error, "the PNG's image data is split");
            idat.insert(idat.end(), data, data + length);
        } else if (named("IEND")) {
            break;
        } else if (critical) {
            return Fail(error, "the PNG has an unknown critical chunk");
        }
        if (!idat.empty() && !named("IDAT")) idat_done = true;
    }
    if (idat.empty()) return Fail(error, "the PNG has no image data");
    if (h.type == 3 && palette_size == 0) return Fail(error, "the PNG has no palette");

    // Adam7's passes (or the one whole image): origin and step.
    static const unsigned kPass[7][4] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
                                         {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
    static const unsigned kWhole[1][4] = {{0, 0, 1, 1}};
    const unsigned (*passes)[4] = h.interlace ? kPass : kWhole;
    const int pass_count = h.interlace ? 7 : 1;
    std::size_t raw_size = 0;
    for (int p = 0; p < pass_count; ++p) {
        const std::uint32_t pw = (h.width - passes[p][0] + passes[p][2] - 1) / passes[p][2];
        const std::uint32_t ph = (h.height - passes[p][1] + passes[p][3] - 1) / passes[p][3];
        if (h.width > passes[p][0] && h.height > passes[p][1] && pw && ph) raw_size += ph * (RowBytes(h, pw) + 1);
    }
    std::vector<std::uint8_t> raw(raw_size);
    if (!inflate(idat.data(), idat.size(), raw.data(), raw.size())) return Fail(error, "the PNG's image data is damaged");
    std::vector<std::uint8_t>().swap(idat);

    ToByte to_byte;
    to_byte.setup(h.depth, have_gamma ? file_gamma : h.depth == 16 ? 100000u : 45455u);
    if (h.type == 3) {
        // The palette is what gamma applies to.
        ToByte palette_byte;
        palette_byte.setup(8, have_gamma ? file_gamma : 45455u);
        for (unsigned i = 0; i < palette_size; ++i)
            for (int c = 0; c < 3; ++c) palette[i][c] = palette_byte(palette[i][c]);
    }

    rgba.assign(static_cast<std::size_t>(h.width) * h.height * 4, 0);
    const std::size_t bpp = h.depth < 8 ? 1 : h.channels * h.depth / 8;
    std::size_t offset = 0;
    for (int p = 0; p < pass_count; ++p) {
        if (h.width <= passes[p][0] || h.height <= passes[p][1]) continue;
        const std::uint32_t pw = (h.width - passes[p][0] + passes[p][2] - 1) / passes[p][2];
        const std::uint32_t ph = (h.height - passes[p][1] + passes[p][3] - 1) / passes[p][3];
        const std::size_t row_bytes = RowBytes(h, pw);
        std::uint8_t* pass = raw.data() + offset;
        offset += ph * (row_bytes + 1);
        if (!Unfilter(pass, ph, row_bytes, bpp)) return Fail(error, "the PNG has a bad row filter");
        for (std::uint32_t py = 0; py < ph; ++py) {
            const std::uint8_t* row = pass + py * (row_bytes + 1) + 1;
            const std::uint32_t y = passes[p][1] + py * passes[p][3];
            for (std::uint32_t px = 0; px < pw; ++px) {
                const std::uint32_t x = passes[p][0] + px * passes[p][2];
                std::uint8_t* out = &rgba[(static_cast<std::size_t>(y) * h.width + x) * 4];
                const std::size_t s = static_cast<std::size_t>(px) * h.channels;
                switch (h.type) {
                case 0: {
                    const unsigned g = Sample(row, s, h.depth);
                    out[0] = out[1] = out[2] = to_byte(g);
                    out[3] = have_key && g == key[0] ? 0 : 255;
                    break;
                }
                case 2: {
                    const unsigned r = Sample(row, s, h.depth), g = Sample(row, s + 1, h.depth),
                                   b = Sample(row, s + 2, h.depth);
                    out[0] = to_byte(r);
                    out[1] = to_byte(g);
                    out[2] = to_byte(b);
                    out[3] = have_key && r == key[0] && g == key[1] && b == key[2] ? 0 : 255;
                    break;
                }
                case 3: {
                    const unsigned i = Sample(row, s, h.depth);
                    if (i >= palette_size) {
                        out[0] = out[1] = out[2] = 0;  // libpng: black
                    } else {
                        std::memcpy(out, palette[i], 3);
                    }
                    out[3] = palette_alpha[i];
                    break;
                }
                case 4:
                    out[0] = out[1] = out[2] = to_byte(Sample(row, s, h.depth));
                    out[3] = h.depth == 16 ? std::uint8_t((Sample(row, s + 1, 16) * 255 + 32895) >> 16)
                                           : std::uint8_t(Sample(row, s + 1, 8));
                    break;
                default:
                    for (int c = 0; c < 3; ++c) out[c] = to_byte(Sample(row, s + c, h.depth));
                    out[3] = h.depth == 16 ? std::uint8_t((Sample(row, s + 3, 16) * 255 + 32895) >> 16)
                                           : std::uint8_t(Sample(row, s + 3, 8));
                    break;
                }
            }
        }
    }
    width = h.width;
    height = h.height;
    return true;
}

}  // namespace riftwii
