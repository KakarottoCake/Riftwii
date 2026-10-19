// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/coverart.hpp"

#include <algorithm>
#include <cmath>

namespace riftwii {
namespace {

constexpr float kCornerRadius = 8.0f;  // the menu's tile cards round at 14 around a 7 margin

std::string region_for(char code, const std::string& lang) {
    switch (code) {
        case 'E':
        case 'N': return "US";
        case 'J': return "JA";
        case 'K':
        case 'Q':
        case 'T': return "KO";
        case 'W': return "ZHTW";
        case 'C': return "ZHCN";
        case 'R': return "RU";
        case 'D': return "DE";
        case 'F': return "FR";
        case 'S': return "ES";
        case 'I': return "IT";
        case 'H': return "NL";
        case 'U': return "AU";
        default:
            // Europe (P, X, Y, Z, ...): the menu's language when GameTDB
            // keeps covers for it.
            if (lang == "es") return "ES";
            if (lang == "it") return "IT";
            if (lang == "pt") return "PT";
            if (lang == "fr") return "FR";
            return "EN";
    }
}

std::uint16_t rgb5a3(const std::uint8_t* p) {
    const unsigned r = p[0], g = p[1], b = p[2], a = p[3];
    if (a >= 0xE0) return static_cast<std::uint16_t>(0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
    return static_cast<std::uint16_t>(((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
}

// How much of pixel (x, y) lies inside the rounded rectangle.
float corner_coverage(int x, int y, int w, int h) {
    const float px = x + 0.5f, py = y + 0.5f;
    const float cx = px < kCornerRadius ? kCornerRadius : px > w - kCornerRadius ? w - kCornerRadius : px;
    const float cy = py < kCornerRadius ? kCornerRadius : py > h - kCornerRadius ? h - kCornerRadius : py;
    const float d = std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
    return std::clamp(kCornerRadius - d + 0.5f, 0.0f, 1.0f);
}

}  // namespace

std::vector<std::string> cover_regions(const std::string& game_id, const std::string& menu_language) {
    std::vector<std::string> out;
    const auto add = [&](const std::string& r) {
        if (std::find(out.begin(), out.end(), r) == out.end()) out.push_back(r);
    };
    if (game_id.size() >= 4) add(region_for(game_id[3], menu_language));
    add("EN");
    add("US");
    add("JA");
    return out;
}

std::string cover_url(const std::string& region, const std::string& game_id) {
    return "http://art.gametdb.com/wii/cover/" + region + "/" + game_id + ".png";
}

int http_answered_status(const std::string& error) {
    static const std::string kAnswered = " answered ";
    const std::size_t at = error.find(kAnswered);
    if (at == std::string::npos) return 0;
    const std::size_t digits = at + kAnswered.size();
    if (error.size() < digits + 3) return 0;
    int status = 0;
    for (std::size_t i = digits; i < digits + 3; ++i) {
        const char c = error[i];
        if (c < '0' || c > '9') return 0;
        status = status * 10 + (c - '0');
    }
    // A fourth digit would make it something else than a status.
    const bool ends_there = error.size() == digits + 3 || error[digits + 3] < '0' || error[digits + 3] > '9';
    return ends_there ? status : 0;
}

bool cover_answer_means_missing(int status) {
    const bool redirect = status >= 300 && status < 400;
    const bool refused = status >= 400 && status < 500 && status != 408 && status != 429;
    return redirect || refused;
}

bool CoverDownloads::count(CoverOutcome outcome) {
    counted_since_summary_ = true;
    switch (outcome) {
        case CoverOutcome::Stored:
            ++stored_;
            no_answer_in_a_row_ = 0;
            return false;
        case CoverOutcome::NotFound:
            // GameTDB answered, so the network is working.
            ++not_found_;
            no_answer_in_a_row_ = 0;
            return false;
        case CoverOutcome::NoAnswer:
            ++no_answer_;
            ++no_answer_in_a_row_;
            return no_answer_in_a_row_ >= kNoAnswerInARow;
        case CoverOutcome::CannotStore:
            // Every cover after it would fail the same way.
            return true;
    }
    return true;
}

std::string CoverDownloads::take_summary() {
    if (!counted_since_summary_) return std::string();
    counted_since_summary_ = false;
    std::string text = log_name_ + ": " + std::to_string(stored_) + " downloaded this session; GameTDB has no " +
                       picture_ + " for " + std::to_string(not_found_) + " game(s) (asked again in a week)";
    if (no_answer_ != 0) {
        text += "; " + std::to_string(no_answer_) +
                " got no answer and were passed over (tried again after Look for games again or a restart)";
    }
    return text + ".";
}

namespace {
// Columns [left, left + sw) of a picture `stride` pixels wide, scaled to
// dw x dh: what scale_rgba does, without copying the columns out first.
std::vector<std::uint8_t> scale_columns(const std::uint8_t* src, int stride, int left, int sw, int sh, int dw, int dh) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(dw) * dh * 4, 0);
    if (!src || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return out;
    const double fx = static_cast<double>(sw) / dw, fy = static_cast<double>(sh) / dh;
    for (int dy = 0; dy < dh; ++dy) {
        const double y0 = dy * fy, y1 = (dy + 1) * fy;
        for (int dx = 0; dx < dw; ++dx) {
            const double x0 = dx * fx, x1 = (dx + 1) * fx;
            double sum[4] = {0, 0, 0, 0}, area = 0;
            for (int sy = static_cast<int>(y0); sy < sh && sy < y1; ++sy) {
                const double wy = std::min<double>(sy + 1, y1) - std::max<double>(sy, y0);
                for (int sx = static_cast<int>(x0); sx < sw && sx < x1; ++sx) {
                    const double wgt = wy * (std::min<double>(sx + 1, x1) - std::max<double>(sx, x0));
                    if (wgt <= 0) continue;
                    const std::uint8_t* p = src + (static_cast<std::size_t>(sy) * stride + left + sx) * 4;
                    for (int c = 0; c < 4; ++c) sum[c] += p[c] * wgt;
                    area += wgt;
                }
            }
            std::uint8_t* q = &out[(static_cast<std::size_t>(dy) * dw + dx) * 4];
            for (int c = 0; c < 4; ++c) q[c] = area > 0 ? static_cast<std::uint8_t>(sum[c] / area + 0.5) : 0;
        }
    }
    return out;
}
}  // namespace

std::vector<std::uint8_t> scale_rgba(const std::uint8_t* src, int sw, int sh, int dw, int dh) {
    return scale_columns(src, sw, 0, sw, sh, dw, dh);
}

std::vector<std::uint8_t> make_cover_file(const std::uint8_t* rgba, int w, int h) {
    if (!rgba || w < 8 || h < 8 || w > 2048 || h > 2048) return {};
    std::vector<std::uint8_t> px = scale_rgba(rgba, w, h, kCoverWidth, kCoverHeight);
    for (int y = 0; y < kCoverHeight; ++y) {
        for (int x = 0; x < kCoverWidth; ++x) {
            std::uint8_t& a = px[(static_cast<std::size_t>(y) * kCoverWidth + x) * 4 + 3];
            a = static_cast<std::uint8_t>(a * corner_coverage(x, y, kCoverWidth, kCoverHeight) + 0.5f);
        }
    }
    std::vector<std::uint8_t> out = {'R', 'W', 'C', '1', 0, static_cast<std::uint8_t>(kCoverWidth), 0,
                                     static_cast<std::uint8_t>(kCoverHeight)};
    out.reserve(kCoverFileSize);
    for (int ty = 0; ty < kCoverHeight; ty += 4) {
        for (int tx = 0; tx < kCoverWidth; tx += 4) {
            for (int y = ty; y < ty + 4; ++y) {
                for (int x = tx; x < tx + 4; ++x) {
                    const std::uint16_t v = rgb5a3(&px[(static_cast<std::size_t>(y) * kCoverWidth + x) * 4]);
                    out.push_back(static_cast<std::uint8_t>(v >> 8));
                    out.push_back(static_cast<std::uint8_t>(v));
                }
            }
        }
    }
    return out;
}

std::string coverfull_url(const std::string& region, const std::string& game_id) {
    return "http://art.gametdb.com/wii/coverfull/" + region + "/" + game_id + ".png";
}

bool spine_strip(int w, int h, int& x0, int& x1) {
    // Wider than tall, and not by more than three faces' worth.
    if (w <= h || h < 64 || w > 3 * h) return false;
    const int face = h * 135 / 190;
    const int spine = w - 2 * face;
    const int expected = h * 14 / 190;
    if (spine >= expected / 2 && spine <= expected * 2) {
        x0 = face;
        x1 = w - face;
    } else {
        x0 = (w - expected) / 2;
        x1 = x0 + expected;
    }
    return x1 > x0;
}

namespace {
// `px` (w x h RGBA rows) as RGB5A3 tiles, after the 8-byte header.
std::vector<std::uint8_t> rgb5a3_file(const char* magic, const std::vector<std::uint8_t>& px, int w, int h) {
    std::vector<std::uint8_t> out = {static_cast<std::uint8_t>(magic[0]), static_cast<std::uint8_t>(magic[1]),
                                     static_cast<std::uint8_t>(magic[2]), static_cast<std::uint8_t>(magic[3]),
                                     static_cast<std::uint8_t>(w >> 8), static_cast<std::uint8_t>(w),
                                     static_cast<std::uint8_t>(h >> 8), static_cast<std::uint8_t>(h)};
    out.reserve(8 + static_cast<std::size_t>(w) * h * 2);
    for (int ty = 0; ty < h; ty += 4)
        for (int tx = 0; tx < w; tx += 4)
            for (int y = ty; y < ty + 4; ++y)
                for (int x = tx; x < tx + 4; ++x) {
                    const std::uint16_t v = rgb5a3(&px[(static_cast<std::size_t>(y) * w + x) * 4]);
                    out.push_back(static_cast<std::uint8_t>(v >> 8));
                    out.push_back(static_cast<std::uint8_t>(v));
                }
    return out;
}

}  // namespace

std::vector<std::uint8_t> make_box_file(const std::uint8_t* rgba, int w, int h) {
    int x0 = 0, x1 = 0;
    if (!rgba || w > 4096 || h > 4096 || !spine_strip(w, h, x0, x1)) return {};
    // Each part scaled straight from the scan: the menu has little heap to spare.
    const std::vector<std::uint8_t> spine = scale_columns(rgba, w, x0, x1 - x0, h, kBoxSpineWidth, kBoxHeight);
    const std::vector<std::uint8_t> front = scale_columns(rgba, w, x1, w - x1, h, kBoxFrontWidth, kBoxHeight);
    const std::vector<std::uint8_t> back = scale_columns(rgba, w, 0, x0, h, kBoxBackWidth, kBoxHeight);
    std::vector<std::uint8_t> px(static_cast<std::size_t>(kBoxWidth) * kBoxHeight * 4);
    for (int y = 0; y < kBoxHeight; ++y) {
        std::copy(spine.begin() + static_cast<std::ptrdiff_t>(y) * kBoxSpineWidth * 4,
                  spine.begin() + static_cast<std::ptrdiff_t>(y + 1) * kBoxSpineWidth * 4,
                  px.begin() + static_cast<std::ptrdiff_t>(y) * kBoxWidth * 4);
        std::copy(front.begin() + static_cast<std::ptrdiff_t>(y) * kBoxFrontWidth * 4,
                  front.begin() + static_cast<std::ptrdiff_t>(y + 1) * kBoxFrontWidth * 4,
                  px.begin() + (static_cast<std::ptrdiff_t>(y) * kBoxWidth + kBoxSpineWidth) * 4);
        std::copy(back.begin() + static_cast<std::ptrdiff_t>(y) * kBoxBackWidth * 4,
                  back.begin() + static_cast<std::ptrdiff_t>(y + 1) * kBoxBackWidth * 4,
                  px.begin() + (static_cast<std::ptrdiff_t>(y) * kBoxWidth + kBoxSpineWidth + kBoxFrontWidth) * 4);
    }
    // A box is solid: no see-through edges from the scan.
    for (std::size_t i = 3; i < px.size(); i += 4) px[i] = 255;
    return rgb5a3_file("RWB2", px, kBoxWidth, kBoxHeight);
}

namespace {
// One RGB5A3 texel as RGBA.
void from_rgb5a3(std::uint16_t v, std::uint8_t* p) {
    if (v & 0x8000) {
        p[0] = static_cast<std::uint8_t>(((v >> 10) & 31) * 255 / 31);
        p[1] = static_cast<std::uint8_t>(((v >> 5) & 31) * 255 / 31);
        p[2] = static_cast<std::uint8_t>((v & 31) * 255 / 31);
        p[3] = 255;
    } else {
        p[0] = static_cast<std::uint8_t>(((v >> 8) & 15) * 17);
        p[1] = static_cast<std::uint8_t>(((v >> 4) & 15) * 17);
        p[2] = static_cast<std::uint8_t>((v & 15) * 17);
        p[3] = static_cast<std::uint8_t>(((v >> 12) & 7) * 255 / 7);
    }
}
}  // namespace

void make_backdrop(const std::uint8_t* tiles, int w, int h, int x0, int x1, std::uint8_t* out, bool rgba8) {
    if (!tiles || !out || w <= 0 || h <= 0 || w % 4 || h % 4 || x0 < 0 || x1 > w || x1 <= x0) return;
    const int sw = x1 - x0;
    std::vector<std::uint8_t> src(static_cast<std::size_t>(sw) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = x0; x < x1; ++x) {
            std::uint8_t* p = &src[(static_cast<std::size_t>(y) * sw + (x - x0)) * 4];
            const std::size_t tile = static_cast<std::size_t>(y / 4) * (w / 4) + x / 4;
            const std::size_t in = static_cast<std::size_t>((y % 4) * 4 + x % 4) * 2;
            if (rgba8) {
                // A tile is 64 bytes: alpha and red pairs, then green and blue.
                const std::uint8_t* t = tiles + tile * 64;
                p[0] = t[in + 1];
                p[1] = t[32 + in];
                p[2] = t[32 + in + 1];
                p[3] = t[in];
            } else {
                const std::size_t at = tile * 32 + in;
                from_rgb5a3(static_cast<std::uint16_t>((tiles[at] << 8) | tiles[at + 1]), p);
            }
        }
    std::vector<std::uint8_t> px = scale_rgba(src.data(), sw, h, kBackdropWidth, kBackdropHeight);
    // Twice a 3x3 average, the edges repeated.
    std::vector<std::uint8_t> tmp(px.size());
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < kBackdropHeight; ++y)
            for (int x = 0; x < kBackdropWidth; ++x)
                for (int c = 0; c < 3; ++c) {
                    int sum = 0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int sx = std::clamp(x + dx, 0, kBackdropWidth - 1);
                            const int sy = std::clamp(y + dy, 0, kBackdropHeight - 1);
                            sum += px[(static_cast<std::size_t>(sy) * kBackdropWidth + sx) * 4 + c];
                        }
                    tmp[(static_cast<std::size_t>(y) * kBackdropWidth + x) * 4 + c] = static_cast<std::uint8_t>(sum / 9);
                }
        for (std::size_t i = 0; i < px.size(); i += 4) {
            px[i] = tmp[i];
            px[i + 1] = tmp[i + 1];
            px[i + 2] = tmp[i + 2];
            px[i + 3] = 255;
        }
    }
    std::uint8_t* q = out;
    for (int ty = 0; ty < kBackdropHeight; ty += 4)
        for (int tx = 0; tx < kBackdropWidth; tx += 4)
            for (int y = ty; y < ty + 4; ++y)
                for (int x = tx; x < tx + 4; ++x) {
                    const std::uint16_t v = rgb5a3(&px[(static_cast<std::size_t>(y) * kBackdropWidth + x) * 4]);
                    *q++ = static_cast<std::uint8_t>(v >> 8);
                    *q++ = static_cast<std::uint8_t>(v);
                }
}

bool round_corners(const std::uint8_t* tiles, int w, int h, int x0, int x1, float radius, std::uint8_t* out) {
    if (!tiles || !out || w <= 0 || h <= 0 || w % 4 || h % 4 || x0 < 0 || x0 % 4 || x1 % 4 || x1 > w || x1 <= x0)
        return false;
    const int ow = x1 - x0;
    const float r = std::clamp(radius, 0.0f, std::min(ow, h) / 2.0f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < ow; ++x) {
            const std::size_t from = (static_cast<std::size_t>(y / 4) * (w / 4) + (x + x0) / 4) * 32 +
                                     static_cast<std::size_t>((y % 4) * 4 + (x + x0) % 4) * 2;
            const std::size_t to = (static_cast<std::size_t>(y / 4) * (ow / 4) + x / 4) * 32 +
                                   static_cast<std::size_t>((y % 4) * 4 + x % 4) * 2;
            out[to] = tiles[from];
            out[to + 1] = tiles[from + 1];
            // How far into a corner's square the texel's middle is; inside
            // one, how much of the texel the curve keeps.
            const float px = x + 0.5f, py = y + 0.5f;
            const float dx = px < r ? r - px : px > ow - r ? px - (ow - r) : 0.0f;
            const float dy = py < r ? r - py : py > h - r ? py - (h - r) : 0.0f;
            if (dx <= 0.0f || dy <= 0.0f) continue;
            const float keep = std::clamp(r - std::sqrt(dx * dx + dy * dy) + 0.5f, 0.0f, 1.0f);
            if (keep >= 1.0f) continue;
            std::uint8_t p[4];
            from_rgb5a3(static_cast<std::uint16_t>((tiles[from] << 8) | tiles[from + 1]), p);
            p[3] = static_cast<std::uint8_t>(p[3] * keep + 0.5f);
            const std::uint16_t v = rgb5a3(p);
            out[to] = static_cast<std::uint8_t>(v >> 8);
            out[to + 1] = static_cast<std::uint8_t>(v);
        }
    return true;
}

std::string cover3d_url(const std::string& region, const std::string& game_id) {
    return "http://art.gametdb.com/wii/cover3D/" + region + "/" + game_id + ".png";
}

std::vector<std::uint8_t> make_cover3d_file(const std::uint8_t* rgba, int w, int h) {
    if (!rgba || w < 8 || h < 8 || w > 2048 || h > 2048) return {};
    // As large as fits, keeping the picture's shape.
    const float k = std::min(static_cast<float>(kCover3dWidth) / w, static_cast<float>(kCover3dHeight) / h);
    const int dw = std::clamp(static_cast<int>(w * k + 0.5f), 1, kCover3dWidth);
    const int dh = std::clamp(static_cast<int>(h * k + 0.5f), 1, kCover3dHeight);
    const std::vector<std::uint8_t> scaled = scale_rgba(rgba, w, h, dw, dh);
    std::vector<std::uint8_t> px(static_cast<std::size_t>(kCover3dWidth) * kCover3dHeight * 4, 0);
    const int left = (kCover3dWidth - dw) / 2, top = kCover3dHeight - dh;
    for (int y = 0; y < dh; ++y)
        std::copy(scaled.begin() + static_cast<std::ptrdiff_t>(y) * dw * 4,
                  scaled.begin() + static_cast<std::ptrdiff_t>(y + 1) * dw * 4,
                  px.begin() + (static_cast<std::ptrdiff_t>(top + y) * kCover3dWidth + left) * 4);
    return rgb5a3_file("RW3D", px, kCover3dWidth, kCover3dHeight);
}

bool cover3d_header_valid(const std::uint8_t* h) {
    return h && h[0] == 'R' && h[1] == 'W' && h[2] == '3' && h[3] == 'D' && ((h[4] << 8) | h[5]) == kCover3dWidth &&
           ((h[6] << 8) | h[7]) == kCover3dHeight;
}

bool box_header_valid(const std::uint8_t* h) {
    return h && h[0] == 'R' && h[1] == 'W' && h[2] == 'B' && h[3] == '2' && ((h[4] << 8) | h[5]) == kBoxWidth &&
           ((h[6] << 8) | h[7]) == kBoxHeight;
}

bool cover_header_valid(const std::uint8_t* h) {
    return h && h[0] == 'R' && h[1] == 'W' && h[2] == 'C' && h[3] == '1' && ((h[4] << 8) | h[5]) == kCoverWidth &&
           ((h[6] << 8) | h[7]) == kCoverHeight;
}

}  // namespace riftwii
