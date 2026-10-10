// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2012 giantpune
// SPDX-FileCopyrightText: 2012 Dimok
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/brfnt.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace riftwii {

const std::uint8_t kWiiBitmapFontHash[20] = {0x4f, 0xad, 0x97, 0xfd, 0x4a, 0x28, 0x8c, 0x47, 0xe0, 0x58,
                                             0x7f, 0x3b, 0xbd, 0x29, 0x23, 0x79, 0xf8, 0x70, 0x9e, 0xb9};

namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
std::uint16_t be16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }
std::uint32_t le32(const std::uint8_t* p) {
    return (std::uint32_t(p[3]) << 24) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[1]) << 8) | p[0];
}

constexpr std::uint32_t kRfnt = 0x52464E54, kRfna = 0x52464E41, kVersion = 0xFEFF0104;
// The sections read. "FINF", the font's own header, is walked past:
// nothing in it is needed.
constexpr std::uint32_t kTglp = 0x54474C50, kCwdh = 0x43574448, kCmap = 0x434D4150;
// The font's 32 pixels make a line as wide as the TrueType font's at this size.
constexpr float kAsFreeType = 32.5f;

}  // namespace

bool u8_find_file(const std::uint8_t* data, std::size_t size, const char* want, std::size_t& offset,
                  std::size_t& length) {
    if (size < 32 || be32(data) != 0x55AA382D) return false;
    const std::size_t root = be32(data + 4);
    if (root > size || size - root < 12) return false;
    const std::size_t count = be32(data + root + 8);
    if (count == 0 || count > (size - root) / 12) return false;
    const std::size_t names = root + count * 12;
    for (std::size_t i = 1; i < count; ++i) {
        const std::uint8_t* node = data + root + i * 12;
        if (node[0] != 0) continue;  // a folder
        const std::size_t name_at = names + (be32(node) & 0xFFFFFF);
        if (name_at >= size) return false;
        const char* name = reinterpret_cast<const char*>(data + name_at);
        const std::size_t room = size - name_at, n = std::strlen(want);
        if (room <= n || std::memcmp(name, want, n) != 0 || name[n] != 0) continue;
        const std::size_t at = be32(node + 4), len = be32(node + 8);
        if (at > size || len > size - at) return false;
        offset = at;
        length = len;
        return true;
    }
    return false;
}

bool huff8_decode(const std::uint8_t* src, std::size_t src_len, std::uint8_t* out, std::size_t out_len) {
    // Header: type 0x28 and the unpacked size (24 bits, little-endian);
    // then the tree's size in halfwords less one, the tree (root at 5) and
    // the bits, little-endian words read from their top bit. A node's low
    // six bits lead to its children, bit 7 (left) and bit 6 (right) mark a
    // child that is a symbol.
    if (src_len < 8 || src[0] != 0x28) return false;
    const std::size_t tree_end = 4 + (std::size_t(src[4]) + 1) * 2;
    if (tree_end >= src_len) return false;
    std::size_t node = 5, in = tree_end, done = 0;
    while (done < out_len) {
        if (in + 4 > src_len) return false;
        const std::uint32_t bits = le32(src + in);
        in += 4;
        for (int k = 31; k >= 0 && done < out_len; --k) {
            const unsigned bit = (bits >> k) & 1u;
            const std::uint8_t b = src[node];
            const std::size_t child = (node & ~std::size_t(1)) + (b & 0x3Fu) * 2 + 2 + bit;
            if (child >= tree_end) return false;
            if ((b >> (7 - bit)) & 1u) {
                out[done++] = src[child];
                node = 5;
            } else {
                node = child;
            }
        }
    }
    return true;
}

bool BitmapFont::sheet_size(const std::uint8_t* data, std::size_t size, std::size_t& bytes) {
    if (size < 16) return false;
    const std::uint32_t magic = be32(data);
    if ((magic != kRfnt && magic != kRfna) || be32(data + 4) != kVersion) return false;
    std::size_t at = be16(data + 12);
    for (unsigned i = 0, n = be16(data + 14); i < n && at + 8 <= size; ++i) {
        const std::size_t len = be32(data + at + 4);
        if (be32(data + at) == kTglp && at + 32 <= size) {
            bytes = be32(data + at + 12);
            return bytes != 0;
        }
        if (len < 8) return false;
        at += len;
    }
    return false;
}

bool BitmapFont::load(const std::uint8_t* data, std::size_t size, std::uint8_t* slot_memory, std::size_t slot_bytes,
                      unsigned slots, std::string& error) {
    if (size < 16 || (be32(data) != kRfnt && be32(data) != kRfna) || be32(data + 4) != kVersion) {
        error = "not a BRFNT font";
        return false;
    }
    data_ = data;
    size_ = size;
    packed_ = be32(data) == kRfna;
    cmap_.clear();
    widths_.clear();
    const std::uint8_t* tglp = nullptr;
    std::size_t at = be16(data + 12);
    for (unsigned i = 0, n = be16(data + 14); i < n; ++i) {
        if (at + 8 > size) break;
        const std::uint32_t magic = be32(data + at);
        const std::size_t len = be32(data + at + 4);
        if (len < 8 || len > size - at) {
            error = "a section runs past the file";
            return false;
        }
        const std::uint8_t* s = data + at;
        if (magic == kTglp && len >= 32) {
            tglp = s;
        } else if (magic == kCwdh && len >= 16) {
            const unsigned first = be16(s + 8), last = be16(s + 10);
            if (last < first || 16 + (last - first + 1) * 3 > len) {
                error = "a width table runs past its section";
                return false;
            }
            if (widths_.empty()) widths_first_ = first;
            if (first < widths_first_) {
                error = "width tables out of order";
                return false;
            }
            if (widths_.size() < last - widths_first_ + 1) widths_.resize(last - widths_first_ + 1);
            for (unsigned g = first; g <= last; ++g) {
                const std::uint8_t* w = s + 16 + (g - first) * 3;
                widths_[g - widths_first_] = Width{static_cast<std::int8_t>(w[0]), w[1], static_cast<std::int8_t>(w[2])};
            }
        } else if (magic == kCmap && len >= 20) {
            const unsigned first = be16(s + 8), last = be16(s + 10), type = be16(s + 12);
            const std::uint8_t* body = s + 20;
            const std::size_t room = len - 20;
            if (last < first) {
                error = "a character map is backwards";
                return false;
            }
            if (type == 0) {  // a run of characters, a run of glyphs
                if (room < 2) return false;
                const unsigned glyph = be16(body);
                for (unsigned c = first; c <= last; ++c) cmap_.push_back((c << 16) | ((glyph + c - first) & 0xFFFF));
            } else if (type == 1) {  // a glyph for each character of the run (0xFFFF: none)
                if (room < (last - first + 1) * 2) {
                    error = "a character map runs past its section";
                    return false;
                }
                for (unsigned c = first; c <= last; ++c) {
                    const unsigned glyph = be16(body + (c - first) * 2);
                    if (glyph != 0xFFFF) cmap_.push_back((c << 16) | glyph);
                }
            } else if (type == 2) {  // pairs
                const unsigned pairs = room >= 2 ? be16(body) : 0;
                if (room < 2 + std::size_t(pairs) * 4) {
                    error = "a character map runs past its section";
                    return false;
                }
                for (unsigned k = 0; k < pairs; ++k)
                    cmap_.push_back((std::uint32_t(be16(body + 2 + k * 4)) << 16) | be16(body + 4 + k * 4));
            } else {
                error = "a character map of an unknown type";
                return false;
            }
        }
        at += len;
    }
    if (!tglp || widths_.empty() || cmap_.empty()) {
        error = "the font has no glyph sheets, widths or character map";
        return false;
    }
    std::sort(cmap_.begin(), cmap_.end());
    cell_w_ = tglp[8] + 1;
    cell_h_ = tglp[9] + 1;
    baseline_ = tglp[10];
    sheet_bytes_ = be32(tglp + 12);
    const unsigned sheets = be16(tglp + 16), format = be16(tglp + 18) & 0x7FFF;
    columns_ = be16(tglp + 20);
    rows_ = be16(tglp + 22);
    sheet_w_ = be16(tglp + 24);
    sheet_h_ = be16(tglp + 26);
    const std::size_t data_at = be32(tglp + 28);
    if (format != 0) {
        error = "glyph sheets not in I4 (format " + std::to_string(format) + ")";
        return false;
    }
    if (sheet_w_ % 8 || sheet_h_ % 8 || sheet_bytes_ != std::size_t(sheet_w_) * sheet_h_ / 2 ||
        columns_ * cell_w_ > sheet_w_ || rows_ * cell_h_ > sheet_h_ || columns_ == 0 || rows_ == 0) {
        error = "the glyph sheets' layout does not add up";
        return false;
    }
    sheet_at_.clear();
    std::size_t p = data_at;
    for (unsigned i = 0; i < sheets; ++i) {
        if (packed_) {
            if (p + 4 > size) break;
            const std::size_t packed = be32(data + p);
            if (packed > size - p - 4) break;
            sheet_at_.push_back(p + 4);
            p += packed + 4;
        } else {
            if (p + sheet_bytes_ > size) break;
            sheet_at_.push_back(p);
            p += sheet_bytes_;
        }
    }
    if (sheet_at_.size() != sheets) {
        error = "the glyph sheets run past the file";
        return false;
    }
    if (slots == 0 || slot_bytes < sheet_bytes_ || slot_memory == nullptr) {
        error = "no room for an unpacked sheet";
        return false;
    }
    slot_memory_ = slot_memory;
    slot_bytes_ = slot_bytes;
    slots_.assign(slots, Slot{});
    return true;
}

const std::uint8_t* BitmapFont::sheet(unsigned index) {
    if (index >= sheet_at_.size()) return nullptr;
    ++clock_;
    unsigned oldest = 0;
    for (unsigned i = 0; i < slots_.size(); ++i) {
        if (slots_[i].sheet == static_cast<int>(index)) {
            slots_[i].used = clock_;
            return slot_memory_ + i * slot_bytes_;
        }
        if (slots_[i].used < slots_[oldest].used) oldest = i;
    }
    std::uint8_t* out = slot_memory_ + oldest * slot_bytes_;
    const std::size_t at = sheet_at_[index];
    bool ok;
    if (packed_) {
        const std::size_t packed = be32(data_ + at - 4);
        ok = packed >= 4 && (le32(data_ + at) >> 8) == sheet_bytes_ && huff8_decode(data_ + at, packed, out, sheet_bytes_);
    } else {
        std::memcpy(out, data_ + at, sheet_bytes_);
        ok = true;
    }
    slots_[oldest].sheet = ok ? static_cast<int>(index) : -1;
    slots_[oldest].used = clock_;
    return ok ? out : nullptr;
}

bool BitmapFont::glyph_index(std::uint32_t ch, std::uint16_t& index) const {
    if (ch > 0xFFFF) return false;
    const std::uint32_t key = ch << 16;
    const auto it = std::lower_bound(cmap_.begin(), cmap_.end(), key);
    if (it == cmap_.end() || (*it >> 16) != ch) return false;
    index = static_cast<std::uint16_t>(*it & 0xFFFF);
    return true;
}

bool BitmapFont::render(std::uint32_t ch, int pixel_size, Glyph& out) {
    std::uint16_t g;
    if (!data_ || pixel_size <= 0 || !glyph_index(ch, g)) return false;
    if (g < widths_first_ || g - widths_first_ >= widths_.size()) return false;
    const Width w = widths_[g - widths_first_];
    const unsigned per_sheet = columns_ * rows_;
    const std::uint8_t* tex = sheet(g / per_sheet);
    if (!tex) return false;
    const unsigned k = g % per_sheet;
    const int x0 = static_cast<int>(k % columns_) * cell_w_, y0 = static_cast<int>(k / columns_) * cell_h_;
    const int src_w = std::min<int>(w.glyph, cell_w_), src_h = cell_h_;

    const float scale = pixel_size / kAsFreeType;
    // The glyph lands where it falls, a fraction of a pixel in: only the
    // bitmap's corner is whole (rounding each glyph's start left gaps such
    // as "d rive" at small sizes).
    const float lx = w.left * scale, ty = baseline_ * scale;
    out.left = static_cast<int>(std::floor(lx));
    const float fx = lx - out.left;
    out.advance = static_cast<int>(std::lround(w.advance * scale));
    out.top = static_cast<int>(std::ceil(ty));
    const float fy = out.top - ty;
    out.width = src_w > 0 ? std::max(1, static_cast<int>(std::ceil(src_w * scale + fx))) : 0;
    out.rows = src_w > 0 ? std::max(1, static_cast<int>(std::ceil(src_h * scale + fy))) : 0;
    out.pixels.assign(std::size_t(out.width) * out.rows, 0);
    if (out.width == 0) return true;

    const auto at = [&](int x, int y) -> unsigned {  // one I4 texel: 8x8 tiles, two to a byte
        if (x < 0 || y < 0 || x >= src_w || y >= src_h) return 0;
        const int sx = x0 + x, sy = y0 + y;
        const std::size_t tile = std::size_t(sy / 8) * (sheet_w_ / 8) + sx / 8;
        const std::uint8_t b = tex[tile * 32 + (sy % 8) * 4 + (sx % 8) / 2];
        return ((sx & 1) ? (b & 0x0F) : (b >> 4)) * 17u;
    };
    // Each pixel out averages the part of the glyph it covers (a box filter):
    // smaller sizes keep the hand-drawn shapes' weight.
    const float step = 1.0f / scale;
    for (int oy = 0; oy < out.rows; ++oy) {
        const float sy0 = (oy - fy) * step, sy1 = sy0 + step;
        for (int ox = 0; ox < out.width; ++ox) {
            const float sx0 = (ox - fx) * step, sx1 = sx0 + step;
            float sum = 0, area = 0;
            for (int y = static_cast<int>(std::floor(sy0)); y < static_cast<int>(std::ceil(sy1)); ++y) {
                const float hy = std::min<float>(sy1, y + 1) - std::max<float>(sy0, y);
                if (hy <= 0) continue;
                for (int x = static_cast<int>(std::floor(sx0)); x < static_cast<int>(std::ceil(sx1)); ++x) {
                    const float wx = std::min<float>(sx1, x + 1) - std::max<float>(sx0, x);
                    if (wx <= 0) continue;
                    sum += at(x, y) * wx * hy;
                    area += wx * hy;
                }
            }
            out.pixels[std::size_t(oy) * out.width + ox] =
                static_cast<std::uint8_t>(area > 0 ? std::min(255.0f, sum / area + 0.5f) : 0);
        }
    }
    return true;
}

}  // namespace riftwii
