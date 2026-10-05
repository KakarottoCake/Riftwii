// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/tpl.hpp"

namespace riftwii {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}
std::uint16_t be16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }

// Block size in pixels and bits per pixel.
bool block_of(std::uint32_t format, int& bw, int& bh, int& bpp) {
    switch (format) {
        case kTplI4: case kTplCI4: case kTplCMPR: bw = 8; bh = 8; bpp = 4; return true;
        case kTplI8: case kTplIA4: case kTplCI8: bw = 8; bh = 4; bpp = 8; return true;
        case kTplIA8: case kTplRGB565: case kTplRGB5A3: case kTplCI14x2: bw = 4; bh = 4; bpp = 16; return true;
        case kTplRGBA8: bw = 4; bh = 4; bpp = 32; return true;
        default: return false;
    }
}

struct Px {
    std::uint8_t r, g, b, a;
};

Px from565(std::uint16_t v) {
    const int r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    return {static_cast<std::uint8_t>((r << 3) | (r >> 2)), static_cast<std::uint8_t>((g << 2) | (g >> 4)),
            static_cast<std::uint8_t>((b << 3) | (b >> 2)), 255};
}

Px from5a3(std::uint16_t v) {
    if (v & 0x8000) {
        const int r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
        return {static_cast<std::uint8_t>((r << 3) | (r >> 2)), static_cast<std::uint8_t>((g << 3) | (g >> 2)),
                static_cast<std::uint8_t>((b << 3) | (b >> 2)), 255};
    }
    const int a = (v >> 12) & 7, r = (v >> 8) & 15, g = (v >> 4) & 15, b = v & 15;
    return {static_cast<std::uint8_t>(r * 17), static_cast<std::uint8_t>(g * 17), static_cast<std::uint8_t>(b * 17),
            static_cast<std::uint8_t>((a << 5) | (a << 2) | (a >> 1))};
}

Px from_palette(std::uint32_t format, std::uint16_t v) {
    if (format == 1) return from565(v);
    if (format == 2) return from5a3(v);
    // IA8: alpha in the high byte, intensity in the low one.
    const std::uint8_t i = static_cast<std::uint8_t>(v & 0xFF);
    return {i, i, i, static_cast<std::uint8_t>(v >> 8)};
}

}  // namespace

std::size_t gx_texture_size(std::uint32_t format, int width, int height) {
    int bw, bh, bpp;
    if (!block_of(format, bw, bh, bpp) || width <= 0 || height <= 0) return 0;
    const std::size_t bx = static_cast<std::size_t>((width + bw - 1) / bw), by = static_cast<std::size_t>((height + bh - 1) / bh);
    return bx * by * static_cast<std::size_t>(bw * bh * bpp / 8);
}

bool parse_tpl(const std::uint8_t* data, std::size_t size, std::vector<TplImage>& out, std::string& error) {
    if (!data || size < 12 || be32(data) != 0x0020AF30u) {
        error = "not a TPL file";
        return false;
    }
    const std::uint32_t count = be32(data + 4), table = be32(data + 8);
    if (count == 0 || count > 1024 || table > size || static_cast<std::size_t>(count) * 8 > size - table) {
        error = "TPL image table is wrong";
        return false;
    }
    std::vector<TplImage> images;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t ih = be32(data + table + i * 8), ph = be32(data + table + i * 8 + 4);
        if (ih > size || size - ih < 0x24) {
            error = "TPL image header lies outside the file";
            return false;
        }
        const std::uint8_t* h = data + ih;
        TplImage im;
        im.height = be16(h);
        im.width = be16(h + 2);
        im.format = be32(h + 4);
        im.data_offset = be32(h + 8);
        im.wrap_s = be32(h + 0xC);
        im.wrap_t = be32(h + 0x10);
        im.min_filter = be32(h + 0x14);
        im.mag_filter = be32(h + 0x18);
        im.data_size = gx_texture_size(im.format, im.width, im.height);
        if (im.data_size == 0) {
            error = "TPL image format " + std::to_string(im.format) + " is not supported";
            return false;
        }
        if (im.data_offset > size || im.data_size > size - im.data_offset) {
            error = "TPL image data lies outside the file";
            return false;
        }
        if (ph != 0) {
            if (ph > size || size - ph < 12) {
                error = "TPL palette header lies outside the file";
                return false;
            }
            im.has_palette = true;
            im.palette_count = be16(data + ph);
            im.palette_format = be32(data + ph + 4);
            im.palette_offset = be32(data + ph + 8);
            if (im.palette_offset > size || static_cast<std::size_t>(im.palette_count) * 2 > size - im.palette_offset) {
                error = "TPL palette lies outside the file";
                return false;
            }
        } else if (im.format == kTplCI4 || im.format == kTplCI8 || im.format == kTplCI14x2) {
            error = "TPL palette image without a palette";
            return false;
        }
        images.push_back(im);
    }
    out.swap(images);
    return true;
}

bool tpl_to_rgba(const std::uint8_t* file, std::size_t size, const TplImage& im, std::vector<std::uint8_t>& rgba,
                 std::string& error) {
    int bw, bh, bpp;
    if (!block_of(im.format, bw, bh, bpp) || im.data_offset > size || im.data_size > size - im.data_offset) {
        error = "TPL image cannot be decoded";
        return false;
    }
    const int w = im.width, h = im.height;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4, 0);
    const std::uint8_t* src = file + im.data_offset;
    const auto put = [&](int x, int y, Px p) {
        if (x >= w || y >= h) return;
        std::uint8_t* d = &out[(static_cast<std::size_t>(y) * w + x) * 4];
        d[0] = p.r;
        d[1] = p.g;
        d[2] = p.b;
        d[3] = p.a;
    };
    const auto palette = [&](std::uint32_t index) -> Px {
        if (!im.has_palette || index >= im.palette_count) return {0, 0, 0, 0};
        return from_palette(im.palette_format, be16(file + im.palette_offset + index * 2));
    };
    std::size_t pos = 0;
    for (int by = 0; by < h; by += bh) {
        for (int bx = 0; bx < w; bx += bw) {
            switch (im.format) {
                case kTplI4:
                case kTplCI4:
                    for (int y = 0; y < 8; ++y)
                        for (int x = 0; x < 8; x += 2) {
                            const std::uint8_t v = src[pos++];
                            for (int k = 0; k < 2; ++k) {
                                const int n = k == 0 ? v >> 4 : v & 15;
                                const std::uint8_t i = static_cast<std::uint8_t>(n * 17);
                                put(bx + x + k, by + y, im.format == kTplI4 ? Px{i, i, i, i} : palette(n));
                            }
                        }
                    break;
                case kTplI8:
                case kTplCI8:
                    for (int y = 0; y < 4; ++y)
                        for (int x = 0; x < 8; ++x) {
                            const std::uint8_t v = src[pos++];
                            put(bx + x, by + y, im.format == kTplI8 ? Px{v, v, v, v} : palette(v));
                        }
                    break;
                case kTplIA4:
                    for (int y = 0; y < 4; ++y)
                        for (int x = 0; x < 8; ++x) {
                            const std::uint8_t v = src[pos++];
                            const std::uint8_t i = static_cast<std::uint8_t>((v & 15) * 17);
                            put(bx + x, by + y, {i, i, i, static_cast<std::uint8_t>((v >> 4) * 17)});
                        }
                    break;
                case kTplIA8:
                    for (int y = 0; y < 4; ++y)
                        for (int x = 0; x < 4; ++x) {
                            const std::uint8_t a = src[pos], i = src[pos + 1];
                            pos += 2;
                            put(bx + x, by + y, {i, i, i, a});
                        }
                    break;
                case kTplRGB565:
                case kTplRGB5A3:
                case kTplCI14x2:
                    for (int y = 0; y < 4; ++y)
                        for (int x = 0; x < 4; ++x) {
                            const std::uint16_t v = be16(src + pos);
                            pos += 2;
                            put(bx + x, by + y,
                                im.format == kTplRGB565   ? from565(v)
                                : im.format == kTplRGB5A3 ? from5a3(v)
                                                          : palette(v & 0x3FFF));
                        }
                    break;
                case kTplRGBA8:
                    // Two passes of 32 bytes: AR pairs, then GB pairs.
                    for (int k = 0; k < 16; ++k) {
                        const std::uint8_t a = src[pos + 2 * k], r = src[pos + 2 * k + 1];
                        const std::uint8_t g = src[pos + 32 + 2 * k], b = src[pos + 32 + 2 * k + 1];
                        put(bx + (k & 3), by + (k >> 2), {r, g, b, a});
                    }
                    pos += 64;
                    break;
                case kTplCMPR:
                    // Four 4x4 DXT1 sub-blocks, left to right, top to bottom.
                    for (int sub = 0; sub < 4; ++sub) {
                        const std::uint16_t c0 = be16(src + pos), c1 = be16(src + pos + 2);
                        Px c[4] = {from565(c0), from565(c1), {}, {}};
                        if (c0 > c1) {
                            c[2] = {static_cast<std::uint8_t>((2 * c[0].r + c[1].r) / 3), static_cast<std::uint8_t>((2 * c[0].g + c[1].g) / 3),
                                    static_cast<std::uint8_t>((2 * c[0].b + c[1].b) / 3), 255};
                            c[3] = {static_cast<std::uint8_t>((c[0].r + 2 * c[1].r) / 3), static_cast<std::uint8_t>((c[0].g + 2 * c[1].g) / 3),
                                    static_cast<std::uint8_t>((c[0].b + 2 * c[1].b) / 3), 255};
                        } else {
                            c[2] = {static_cast<std::uint8_t>((c[0].r + c[1].r) / 2), static_cast<std::uint8_t>((c[0].g + c[1].g) / 2),
                                    static_cast<std::uint8_t>((c[0].b + c[1].b) / 2), 255};
                            c[3] = {0, 0, 0, 0};
                        }
                        const int sx = bx + (sub & 1) * 4, sy = by + (sub >> 1) * 4;
                        for (int y = 0; y < 4; ++y) {
                            const std::uint8_t bits = src[pos + 4 + y];
                            for (int x = 0; x < 4; ++x) put(sx + x, sy + y, c[(bits >> (6 - 2 * x)) & 3]);
                        }
                        pos += 8;
                    }
                    break;
            }
        }
    }
    rgba.swap(out);
    return true;
}

}  // namespace riftwii
