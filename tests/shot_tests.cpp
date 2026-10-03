// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Screenshots: the PNG writer (read back with the cover art's reader and
// an inflater for the block types it writes), the YUYV conversion and
// the in-game raw file's header.
#include "riftwii/pngdecode.hpp"
#include "riftwii/pngencode.hpp"
#include "riftwii/shotfile.hpp"
#include "rtshot.h"

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

// Stored and fixed-Huffman blocks (RFC 1951), what the writer produces.
struct BitReader {
    const std::uint8_t* data;
    std::size_t size, at = 0;
    unsigned bit = 0;
    bool bad = false;
    unsigned Get(unsigned n) {
        unsigned v = 0;
        for (unsigned i = 0; i < n; ++i) {
            if (at >= size) {
                bad = true;
                return 0;
            }
            v |= ((data[at] >> bit) & 1u) << i;
            if (++bit == 8) bit = 0, ++at;
        }
        return v;
    }
    // A Huffman code, most significant bit first.
    unsigned Code(unsigned n) {
        unsigned v = 0;
        for (unsigned i = 0; i < n; ++i) v = (v << 1) | Get(1);
        return v;
    }
};

unsigned FixedSymbol(BitReader& r) {
    unsigned c = r.Code(7);
    if (c <= 0x17) return 256 + c;
    c = (c << 1) | r.Get(1);
    if (c >= 0x30 && c <= 0xBF) return c - 0x30;
    if (c >= 0xC0 && c <= 0xC7) return 280 + (c - 0xC0);
    c = (c << 1) | r.Get(1);
    return 144 + (c - 0x190);
}

bool TestInflate(const std::uint8_t* data, std::size_t size, std::uint8_t* out, std::size_t out_size) {
    static const unsigned kLb[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
                                     67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const unsigned kLe[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const unsigned kDb[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                     1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const unsigned kDe[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
                                     9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    if (size < 6 || ((data[0] << 8) | data[1]) % 31 != 0 || (data[0] & 15) != 8) return false;
    BitReader r{data + 2, size - 6};
    Bytes got;
    for (;;) {
        const unsigned last = r.Get(1), type = r.Get(2);
        if (type == 0) {
            if (r.bit) r.bit = 0, ++r.at;
            if (r.at + 4 > r.size) return false;
            const unsigned n = r.data[r.at] | (r.data[r.at + 1] << 8);
            r.at += 4;
            if (r.at + n > r.size) return false;
            got.insert(got.end(), r.data + r.at, r.data + r.at + n);
            r.at += n;
        } else if (type == 1) {
            for (;;) {
                const unsigned s = FixedSymbol(r);
                if (r.bad || s > 285) return false;
                if (s < 256) {
                    got.push_back(std::uint8_t(s));
                    continue;
                }
                if (s == 256) break;
                const unsigned length = kLb[s - 257] + r.Get(kLe[s - 257]);
                const unsigned d = r.Code(5);
                if (d > 29) return false;
                const unsigned distance = kDb[d] + r.Get(kDe[d]);
                if (distance > got.size()) return false;
                for (unsigned i = 0; i < length; ++i) got.push_back(got[got.size() - distance]);
            }
        } else {
            return false;
        }
        if (r.bad) return false;
        if (last) break;
    }
    // The Adler-32 after the (byte-aligned) end.
    if (r.bit) ++r.at;
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t c : got) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    const std::uint8_t* t = r.data + r.at;
    if (r.at != r.size) return false;
    const std::uint32_t stored = (std::uint32_t(t[0]) << 24) | (t[1] << 16) | (t[2] << 8) | t[3];
    if (stored != ((b << 16) | a)) return false;
    if (got.size() < out_size) return false;
    std::memcpy(out, got.data(), out_size);
    return true;
}

bool RoundTrip(const Bytes& rgb, std::uint32_t w, std::uint32_t h, std::size_t* png_size = nullptr) {
    Bytes png;
    if (!riftwii::encode_png_rgb(rgb.data(), w, h, png)) return false;
    if (png_size) *png_size = png.size();
    Bytes rgba;
    std::uint32_t dw = 0, dh = 0;
    std::string error;
    if (!riftwii::decode_png(png.data(), png.size(), TestInflate, 4096, rgba, dw, dh, error)) {
        std::cerr << "decode: " << error << std::endl;
        return false;
    }
    if (dw != w || dh != h) return false;
    for (std::size_t i = 0; i < std::size_t(w) * h; ++i) {
        if (rgba[i * 4] != rgb[i * 3] || rgba[i * 4 + 1] != rgb[i * 3 + 1] || rgba[i * 4 + 2] != rgb[i * 3 + 2] ||
            rgba[i * 4 + 3] != 255)
            return false;
    }
    return true;
}

std::uint32_t g_seed = 12345;
std::uint8_t Random() {
    g_seed = g_seed * 1103515245u + 12345u;
    return std::uint8_t(g_seed >> 16);
}

void test_png_round_trip() {
    // A tiny image, one pixel wide, and odd sizes.
    EXPECT_TRUE(RoundTrip({1, 2, 3}, 1, 1));
    EXPECT_TRUE(RoundTrip({9, 8, 7, 6, 5, 4, 3, 2, 1}, 1, 3));
    // Noise: no matches worth having.
    Bytes noise(37 * 29 * 3);
    for (auto& b : noise) b = Random();
    EXPECT_TRUE(RoundTrip(noise, 37, 29));
    // A menu-like screen: flat areas, gradients and some text-like noise,
    // bigger than the 32 KiB window and one IDAT.
    const std::uint32_t w = 640, h = 480;
    Bytes screen(std::size_t(w) * h * 3);
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            std::uint8_t* p = &screen[(std::size_t(y) * w + x) * 3];
            if (y < 60) p[0] = 40, p[1] = 90, p[2] = 200;
            else if (x < 200) p[0] = std::uint8_t(x), p[1] = std::uint8_t(y), p[2] = std::uint8_t(x + y);
            else if ((x / 8 + y / 12) % 3 == 0) p[0] = p[1] = p[2] = Random() & 0xF0;
            else p[0] = p[1] = p[2] = 235;
        }
    std::size_t size = 0;
    EXPECT_TRUE(RoundTrip(screen, w, h, &size));
    EXPECT_TRUE(size < screen.size() / 4);  // it does compress
    // Long runs: matches of 258 and far distances.
    Bytes flat(std::size_t(300) * 200 * 3, 77);
    EXPECT_TRUE(RoundTrip(flat, 300, 200, &size));
    EXPECT_TRUE(size < 2000);
}

void test_png_refusals() {
    Bytes out;
    std::uint8_t px[3] = {};
    EXPECT_TRUE(!riftwii::encode_png_rgb(px, 0, 1, out));
    // Too few rows: finish refuses.
    riftwii::PngWriter w(1, 2, [](void*, const std::uint8_t*, std::size_t) { return true; }, nullptr);
    EXPECT_TRUE(w.add_row(px));
    EXPECT_TRUE(!w.finish());
    // The tables in the caller's memory, as the menu keeps them.
    std::vector<std::uint64_t> work(riftwii::PngWriter::work_bytes() / 8 + 1);
    Bytes one;
    {
        riftwii::PngWriter m(1, 1, [](void* u, const std::uint8_t* d, std::size_t n) {
            auto* o = static_cast<Bytes*>(u);
            o->insert(o->end(), d, d + n);
            return true;
        }, &one, work.data());
        EXPECT_TRUE(m.add_row(px));
        EXPECT_TRUE(m.finish());
    }
    Bytes rgba;
    std::uint32_t dw = 0, dh = 0;
    std::string error;
    EXPECT_TRUE(riftwii::decode_png(one.data(), one.size(), TestInflate, 4, rgba, dw, dh, error));
    // A sink that fails stops everything.
    riftwii::PngWriter f(1, 1, [](void*, const std::uint8_t*, std::size_t) { return false; }, nullptr);
    EXPECT_TRUE(!f.add_row(px));
}

void test_yuyv() {
    // Black, white, and a red pair (BT.601: Y 81, U 90, V 240).
    const std::uint8_t black[4] = {16, 128, 16, 128}, white[4] = {235, 128, 235, 128}, red[4] = {81, 90, 81, 240};
    std::uint8_t rgb[6];
    riftwii::yuyv_row_to_rgb(black, 2, rgb);
    for (int i = 0; i < 6; ++i) EXPECT_EQ(rgb[i], 0);
    riftwii::yuyv_row_to_rgb(white, 2, rgb);
    for (int i = 0; i < 6; ++i) EXPECT_EQ(rgb[i], 255);
    riftwii::yuyv_row_to_rgb(red, 2, rgb);
    EXPECT_TRUE(rgb[0] >= 250 && rgb[1] <= 4 && rgb[2] <= 4);
    EXPECT_TRUE(rgb[3] >= 250 && rgb[4] <= 4 && rgb[5] <= 4);
    // Two pixels of one pair keep their own brightness.
    const std::uint8_t pair[4] = {16, 128, 235, 128};
    riftwii::yuyv_row_to_rgb(pair, 2, rgb);
    EXPECT_EQ(rgb[0], 0);
    EXPECT_EQ(rgb[3], 255);

    // A 4x2 picture, doubled to 4x4, through the PNG writer.
    Bytes yuyv = {235, 128, 235, 128, 16, 128, 16, 128, 16, 128, 16, 128, 235, 128, 235, 128};
    Bytes png;
    EXPECT_TRUE(riftwii::write_yuyv_png(yuyv.data(), 4, 2, 8, true,
                                        [](void* u, const std::uint8_t* d, std::size_t n) {
                                            auto* o = static_cast<Bytes*>(u);
                                            o->insert(o->end(), d, d + n);
                                            return true;
                                        },
                                        &png));
    Bytes rgba;
    std::uint32_t w = 0, h = 0;
    std::string error;
    EXPECT_TRUE(riftwii::decode_png(png.data(), png.size(), TestInflate, 64, rgba, w, h, error));
    EXPECT_EQ(w, 4u);
    EXPECT_EQ(h, 4u);
    if (rgba.size() == 64) {
        EXPECT_EQ(rgba[0], 255);        // row 0: white, then black
        EXPECT_EQ(rgba[2 * 4], 0);
        EXPECT_EQ(rgba[16 + 0], 255);   // row 1 repeats it
        EXPECT_EQ(rgba[32 + 0], 0);     // row 2: black, then white
        EXPECT_EQ(rgba[32 + 2 * 4], 255);
    }
}

void test_header() {
    Bytes h(riftwii::kShotHeaderBytes, 0);
    auto put = [&](std::size_t at, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) h[at + i] = std::uint8_t(v >> (24 - 8 * i));
    };
    put(0, riftwii::kShotMagic);
    put(4, riftwii::kShotVersion);
    put(8, 640);
    put(12, 480);
    put(16, riftwii::kShotFlagDoubleLines);
    std::memcpy(&h[20], "RMCE01", 6);
    put(28, 3);
    riftwii::ShotInfo info;
    std::string error;
    const std::size_t full = riftwii::kShotHeaderBytes + 640 * 2 * 480;
    EXPECT_TRUE(riftwii::parse_shot_header(h.data(), full, info, error));
    EXPECT_EQ(info.width, 640u);
    EXPECT_EQ(info.lines, 480u);
    EXPECT_EQ(info.flags, riftwii::kShotFlagDoubleLines);
    EXPECT_TRUE(info.game_id == "RMCE01");
    EXPECT_EQ(info.index, 3u);
    EXPECT_TRUE(!riftwii::parse_shot_header(h.data(), full - 1, info, error));  // cut short
    put(8, 641);
    EXPECT_TRUE(!riftwii::parse_shot_header(h.data(), full + 4096, info, error));  // odd width
    put(8, 640);
    put(4, 2);
    EXPECT_TRUE(!riftwii::parse_shot_header(h.data(), full, info, error));  // version
    put(4, 1);
    h[0] = 'X';
    EXPECT_TRUE(!riftwii::parse_shot_header(h.data(), full, info, error));
    // A game ID with junk ends at the junk.
    h[0] = 'R';
    std::memcpy(&h[20], "RM\x01\x02\x03\x04", 6);
    EXPECT_TRUE(riftwii::parse_shot_header(h.data(), full, info, error));
    EXPECT_TRUE(info.game_id == "RM");
    EXPECT_TRUE(riftwii::shot_file_name("RMCE01", 7) == "RMCE01-0007.png");
    EXPECT_TRUE(riftwii::shot_file_name("riftwii", 12345) == "riftwii-12345.png");
}


// An HCI ACL packet carrying one Wii Remote input report.
Bytes Report(std::uint16_t handle, std::uint8_t id, std::uint8_t b0, std::uint8_t b1, std::uint16_t cid = 0x41) {
    Bytes hid = {0xA1, id, b0, b1, 0, 0, 0};
    Bytes acl = {std::uint8_t(handle), std::uint8_t(0x20 | (handle >> 8)), std::uint8_t(hid.size() + 4), 0,
                 std::uint8_t(hid.size()), 0, std::uint8_t(cid), std::uint8_t(cid >> 8)};
    acl.insert(acl.end(), hid.begin(), hid.end());
    return acl;
}

int Feed(rtshot_input& in, Bytes& acl) { return rtshot_acl(&in, acl.data(), std::uint32_t(acl.size())); }

void test_remote_combo() {
    rtshot_input in{};
    Bytes r = Report(1, 0x30, 0, 0x02);  // 1 held
    EXPECT_EQ(Feed(in, r), 0);
    r = Report(1, 0x30, 0, 0x82);        // and HOME: fired, HOME hidden
    EXPECT_EQ(Feed(in, r), 1);
    EXPECT_EQ(r[11], 0x02);
    r = Report(1, 0x37, 0, 0x82);        // still held: hidden, not fired again
    EXPECT_EQ(Feed(in, r), 0);
    EXPECT_EQ(r[11], 0x02);
    r = Report(1, 0x30, 0, 0x80);        // 1 let go, HOME still down: still hidden
    EXPECT_EQ(Feed(in, r), 0);
    EXPECT_EQ(r[11], 0x00);
    r = Report(1, 0x30, 0, 0x00);        // HOME let go
    EXPECT_EQ(Feed(in, r), 0);
    r = Report(1, 0x30, 0, 0x80);        // HOME alone reaches the game
    EXPECT_EQ(Feed(in, r), 0);
    EXPECT_EQ(r[11], 0x80);
    r = Report(1, 0x30, 0, 0x82);        // HOME first, then 1: not the combo
    EXPECT_EQ(Feed(in, r), 0);
    EXPECT_EQ(r[11], 0x82);
    // Both pressed in one report: 1 was not held before.
    rtshot_input fresh{};
    r = Report(2, 0x30, 0, 0x82);
    EXPECT_EQ(Feed(fresh, r), 0);
    // A second remote is its own: remote 2 holds 1 while remote 1 presses HOME.
    rtshot_input two{};
    r = Report(2, 0x30, 0, 0x02);
    Feed(two, r);
    r = Report(1, 0x30, 0, 0x80);
    EXPECT_EQ(Feed(two, r), 0);
    r = Report(2, 0x33, 0x10, 0x82);     // remote 2 (with Plus held too): fired
    EXPECT_EQ(Feed(two, r), 1);
    EXPECT_EQ(r[10], 0x10);
    // What is not a button report is left alone.
    rtshot_input other{};
    r = Report(1, 0x30, 0, 0x02, 0x0001);  // the signalling channel
    Feed(other, r);
    r = Report(1, 0x30, 0, 0x82, 0x0001);
    EXPECT_EQ(Feed(other, r), 0);
    EXPECT_EQ(r[11], 0x82);
    r = Report(1, 0x3D, 0, 0x02);  // no buttons in 0x3D
    Feed(other, r);
    r = Report(1, 0x3D, 0, 0x82);
    EXPECT_EQ(Feed(other, r), 0);
    EXPECT_EQ(r[11], 0x82);
    EXPECT_EQ(other.reports, 0u);
    Bytes short_packet = {1, 0x20, 4, 0};
    EXPECT_EQ(rtshot_acl(&other, short_packet.data(), 4), 0);
    r = Report(1, 0x30, 0, 0x02);
    r[2] = 99;  // lengths that disagree
    EXPECT_EQ(Feed(other, r), 0);
    EXPECT_EQ(other.reports, 0u);
}

void test_pad_combo() {
    rtshot_input in{};
    std::uint8_t status[48] = {};
    auto set = [&](int port, unsigned buttons, std::uint8_t l = 0, std::uint8_t r = 0, int err = 0) {
        std::uint8_t* s = status + port * 12;
        s[0] = std::uint8_t(buttons >> 8);
        s[1] = std::uint8_t(buttons);
        s[6] = l;
        s[7] = r;
        s[10] = std::uint8_t(err);
    };
    for (int p = 1; p < 4; ++p) set(p, 0, 0, 0, -1);  // nothing plugged in
    set(0, RTSHOT_PAD_L | RTSHOT_PAD_R);
    EXPECT_EQ(rtshot_pads(&in, status), 0);
    set(0, RTSHOT_PAD_L | RTSHOT_PAD_R | RTSHOT_PAD_DOWN);
    EXPECT_EQ(rtshot_pads(&in, status), 1);
    EXPECT_EQ(status[1] & RTSHOT_PAD_DOWN, 0u);  // hidden
    EXPECT_EQ(status[1] & RTSHOT_PAD_L, RTSHOT_PAD_L);
    set(0, RTSHOT_PAD_DOWN);  // L and R let go, Down held: still hidden
    EXPECT_EQ(rtshot_pads(&in, status), 0);
    EXPECT_EQ(status[1] & RTSHOT_PAD_DOWN, 0u);
    set(0, 0);
    EXPECT_EQ(rtshot_pads(&in, status), 0);
    set(0, RTSHOT_PAD_DOWN);  // Down alone
    EXPECT_EQ(rtshot_pads(&in, status), 0);
    EXPECT_EQ(status[1] & RTSHOT_PAD_DOWN, RTSHOT_PAD_DOWN);
    set(0, 0);
    rtshot_pads(&in, status);
    // Analog triggers half down count; a little does not.
    set(0, 0, 0x90, 0x90);
    rtshot_pads(&in, status);
    set(0, RTSHOT_PAD_DOWN, 0x90, 0x90);
    EXPECT_EQ(rtshot_pads(&in, status), 1);
    set(0, 0);
    rtshot_pads(&in, status);
    set(0, 0, 0x20, 0x90);
    rtshot_pads(&in, status);
    set(0, RTSHOT_PAD_DOWN, 0x20, 0x90);
    EXPECT_EQ(rtshot_pads(&in, status), 0);
    // A port with no new data (err -3) keeps its state.
    set(2, RTSHOT_PAD_L | RTSHOT_PAD_R | RTSHOT_PAD_DOWN, 0, 0, -3);
    EXPECT_EQ(rtshot_pads(&in, status), 0);
}

void test_vi_frame() {
    struct rtshot_frame f{};
    const std::uint32_t mem1 = (1u << 28) | (0x00F00000u >> 5);
    // 480i, both fields in one buffer: a stride of two lines.
    EXPECT_EQ(rtshot_frame(0x0F06, (40 << 8) | 80, mem1, &f), 1);
    EXPECT_EQ(f.address, 0x00F00000u);
    EXPECT_EQ(f.width, 640u);
    EXPECT_EQ(f.lines, 480u);
    EXPECT_EQ(f.stride, 1280u);
    EXPECT_EQ(f.flags, 0u);
    // 480p: one buffer line per line.
    EXPECT_EQ(rtshot_frame((480 << 4) | 6, (40 << 8) | 40, mem1, &f), 1);
    EXPECT_EQ(f.lines, 480u);
    // PAL 576i.
    EXPECT_EQ(rtshot_frame((287 << 4) | 5, (40 << 8) | 80, mem1, &f), 1);
    EXPECT_EQ(f.lines, 574u);
    // A field-rendered 240-line picture: shown twice.
    EXPECT_EQ(rtshot_frame(0x0F06, (40 << 8) | 40, mem1, &f), 1);
    EXPECT_EQ(f.lines, 240u);
    EXPECT_EQ(f.flags, std::uint32_t(RTSHOT_DOUBLE_LINES));
    // MEM2, and an address without the page-offset bit.
    EXPECT_EQ(rtshot_frame(0x0F06, (40 << 8) | 80, (1u << 28) | (0x10200000u >> 5), &f), 1);
    EXPECT_EQ(f.address, 0x10200000u);
    EXPECT_EQ(rtshot_frame(0x0F06, (40 << 8) | 80, 0x00100000u, &f), 1);
    EXPECT_EQ(f.address, 0x00100000u);
    // Nothing to take.
    EXPECT_EQ(rtshot_frame(0, (40 << 8) | 80, mem1, &f), 0);                          // VI off
    EXPECT_EQ(rtshot_frame(0x0F06, (40 << 8) | 120, mem1, &f), 0);                    // a stride of three lines
    EXPECT_EQ(rtshot_frame(0x0F06, (46 << 8) | 92, mem1, &f), 0);                     // wider than 720
    EXPECT_EQ(rtshot_frame(0x0F06, (40 << 8) | 80, (1u << 28) | (0x017F0000u >> 5), &f), 0);  // past MEM1
    EXPECT_EQ(rtshot_frame(0x0F06, (40 << 8) | 80, 0x00100010u, &f), 0);              // unaligned
}

void test_shot_file() {
    char path[32];
    rtshot_path(path, 7);
    EXPECT_TRUE(std::string(path) == "/shared2/riftwii/shot0007.raw");
    rtshot_path(path, 12345);
    EXPECT_TRUE(std::string(path) == "/shared2/riftwii/shot2345.raw");
    rtshot_dir(path);
    EXPECT_TRUE(std::string(path) == "/shared2/riftwii");
    std::uint8_t attr[RTSHOT_FS_ATTR_BYTES];
    rtshot_fs_attr(attr, "/shared2/riftwii");
    EXPECT_TRUE(std::string(reinterpret_cast<const char*>(attr + 6)) == "/shared2/riftwii");
    EXPECT_EQ(attr[70], 3);
    EXPECT_EQ(attr[71], 3);
    EXPECT_EQ(attr[72], 3);
    EXPECT_EQ(attr[73], 0);
    // The blob's header is what the menu reads.
    struct rtshot_frame f{};
    rtshot_frame(0x0F06, (40 << 8) | 40, (1u << 28) | (0x00F00000u >> 5), &f);
    std::uint8_t h[RTSHOT_HEADER_BYTES];
    rtshot_header(h, &f, reinterpret_cast<const std::uint8_t*>("SB4E01"), 2, 12345);
    riftwii::ShotInfo info;
    std::string error;
    EXPECT_TRUE(riftwii::parse_shot_header(h, RTSHOT_HEADER_BYTES + 640 * 2 * 240, info, error));
    EXPECT_EQ(info.width, 640u);
    EXPECT_EQ(info.lines, 240u);
    EXPECT_EQ(info.flags, riftwii::kShotFlagDoubleLines);
    EXPECT_TRUE(info.game_id == "SB4E01");
    EXPECT_EQ(info.index, 2u);
    EXPECT_EQ(std::size_t(RTSHOT_FRAME_BYTES), riftwii::kShotHeaderBytes + 720 * 2 * 576);
}

}  // namespace

int main() {
    test_png_round_trip();
    test_png_refusals();
    test_yuyv();
    test_header();
    test_remote_combo();
    test_pad_combo();
    test_vi_frame();
    test_shot_file();
    if (g_failures == 0) {
        std::cout << "ALL SHOT TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
