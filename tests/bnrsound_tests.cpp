// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Banner sounds: the DSP's ADPCM, and BNS, WAV and AIFF streams (bare,
// under IMD5, LZ77-packed), on small files built here.
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/bnrsound.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;
using Bytes = std::vector<std::uint8_t>;

namespace {

void be32(Bytes& b, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<std::uint8_t>(v >> s));
}
void be16(Bytes& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v >> 8));
    b.push_back(static_cast<std::uint8_t>(v));
}
void le32(Bytes& b, std::uint32_t v) {
    for (int s = 0; s <= 24; s += 8) b.push_back(static_cast<std::uint8_t>(v >> s));
}
void le16(Bytes& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}
void tag(Bytes& b, const char* t) { b.insert(b.end(), t, t + 4); }

// One ADPCM frame: coefficient pair `pair`, shift `shift`, 14 steps.
Bytes frame(int pair, int shift, const int (&steps)[14]) {
    Bytes f{static_cast<std::uint8_t>((pair << 4) | shift)};
    for (int k = 0; k < 14; k += 2) f.push_back(static_cast<std::uint8_t>(((steps[k] & 0xF) << 4) | (steps[k + 1] & 0xF)));
    return f;
}

void test_adpcm() {
    // Pair 0 (0, 0): each sample is its step, shifted.
    std::int16_t coef[16] = {};
    // Pair 1 (2048, 0): each sample adds its step to the one before.
    coef[2] = 2048;
    const int steps[14] = {1, -1, 7, -8, 0, 2, 3, 4, 5, 6, -2, -3, -4, -5};
    std::int16_t out[14];
    const Bytes a = frame(0, 2, steps);
    decode_dsp_adpcm(a.data(), a.size(), 14, coef, 0, 0, out, 1);
    for (int k = 0; k < 14; ++k) EXPECT_EQ(out[k], steps[k] * 4);
    const int ones[14] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    const Bytes b = frame(1, 0, ones);
    decode_dsp_adpcm(b.data(), b.size(), 14, coef, 100, 0, out, 1);
    for (int k = 0; k < 14; ++k) EXPECT_EQ(out[k], 101 + k);
    // Clamped, and a short buffer stops early.
    decode_dsp_adpcm(b.data(), b.size(), 14, coef, 32760, 0, out, 1);
    EXPECT_EQ(out[13], 32767);
    std::int16_t two[28] = {};
    decode_dsp_adpcm(b.data(), b.size(), 28, coef, 0, 0, two, 1);
    EXPECT_EQ(two[13], 14);
    EXPECT_EQ(two[14], 0);
}

// A BNS of two channels, 14 samples each, 32000 a second, looping from 7.
Bytes make_bns() {
    const int left[14] = {1, 2, 3, 4, 5, 6, 7, -1, -2, -3, -4, -5, -6, -7};
    const int right[14] = {0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1};
    Bytes data = frame(0, 0, left), r = frame(0, 0, right);
    data.insert(data.end(), r.begin(), r.end());
    Bytes info;
    info.push_back(0);   // ADPCM
    info.push_back(1);   // loops
    info.push_back(2);   // channels
    info.push_back(0);
    be16(info, 32000);
    be16(info, 0);
    be32(info, 7);       // loop start
    be32(info, 14);      // samples
    be32(info, 0x18);    // the channel list, from INFO's body
    be32(info, 0);
    be32(info, 0x20);    // channel 0's entry
    be32(info, 0x2C);    // channel 1's
    be32(info, 0); be32(info, 0x38); be32(info, 0);    // data at 0, DSP info at 0x38
    be32(info, 8); be32(info, 0x68); be32(info, 0);    // data at 8, DSP info at 0x68
    for (int c = 0; c < 2; ++c)
        for (int k = 0; k < 24; ++k) be16(info, 0);    // coefficients 0, gain, header, history...
    Bytes b;
    tag(b, "BNS ");
    be32(b, 0xFEFF0100u);
    be32(b, static_cast<std::uint32_t>(0x20 + 8 + info.size() + 8 + data.size()));
    be16(b, 0x20);
    be16(b, 2);
    be32(b, 0x20);
    be32(b, static_cast<std::uint32_t>(8 + info.size()));
    be32(b, static_cast<std::uint32_t>(0x20 + 8 + info.size()));
    be32(b, static_cast<std::uint32_t>(8 + data.size()));
    tag(b, "INFO");
    be32(b, static_cast<std::uint32_t>(8 + info.size()));
    b.insert(b.end(), info.begin(), info.end());
    tag(b, "DATA");
    be32(b, static_cast<std::uint32_t>(8 + data.size()));
    b.insert(b.end(), data.begin(), data.end());
    return b;
}

void test_bns() {
    const Bytes bns = make_bns();
    BannerSound s;
    std::string error;
    EXPECT_TRUE(decode_banner_sound(bns.data(), bns.size(), s, error));
    EXPECT_EQ(s.rate, 32000u);
    EXPECT_EQ(s.frames(), 14u);
    EXPECT_TRUE(s.loop);
    EXPECT_EQ(s.loop_start, 7u);
    EXPECT_EQ(s.pcm[0], 1);    // left
    EXPECT_EQ(s.pcm[1], 0);    // right
    EXPECT_EQ(s.pcm[2 * 13], -7);
    EXPECT_EQ(s.pcm[2 * 13 + 1], 1);
    // Under an IMD5 header, as sound.bin has it.
    Bytes imd5;
    tag(imd5, "IMD5");
    be32(imd5, static_cast<std::uint32_t>(bns.size()));
    imd5.resize(0x20, 0);
    imd5.insert(imd5.end(), bns.begin(), bns.end());
    EXPECT_TRUE(decode_banner_sound(imd5.data(), imd5.size(), s, error));
    EXPECT_EQ(s.pcm[2 * 6], 7);
    // Cut at max_frames.
    EXPECT_TRUE(decode_banner_sound(bns.data(), bns.size(), s, error, 5));
    EXPECT_EQ(s.frames(), 5u);
    EXPECT_TRUE(!s.loop);
    // Broken: refused, never read past the end.
    for (std::size_t cut = 0; cut < bns.size(); cut += 7) {
        BannerSound t;
        decode_banner_sound(bns.data(), cut, t, error);
    }
    Bytes bad = bns;
    bad[0x20 + 8 + 2] = 5;  // five channels
    EXPECT_TRUE(!decode_banner_sound(bad.data(), bad.size(), s, error));
    EXPECT_TRUE(s.pcm.empty());
}

Bytes make_wav(bool loop) {
    Bytes body;
    tag(body, "WAVE");
    tag(body, "fmt ");
    le32(body, 16);
    le16(body, 1);
    le16(body, 1);       // mono
    le32(body, 22050);
    le32(body, 44100);
    le16(body, 2);
    le16(body, 16);
    if (loop) {
        tag(body, "smpl");
        le32(body, 0x24 + 24);
        for (int k = 0; k < 7; ++k) le32(body, 0);
        le32(body, 1);   // one loop
        le32(body, 0);
        le32(body, 0); le32(body, 0); le32(body, 2); le32(body, 3); le32(body, 0); le32(body, 0);
    }
    tag(body, "data");
    le32(body, 8);
    for (std::int16_t v : {100, -200, 300, -400}) le16(body, static_cast<std::uint16_t>(v));
    Bytes b;
    tag(b, "RIFF");
    le32(b, static_cast<std::uint32_t>(body.size()));
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

void test_wav() {
    BannerSound s;
    std::string error;
    const Bytes wav = make_wav(false);
    EXPECT_TRUE(decode_banner_sound(wav.data(), wav.size(), s, error));
    EXPECT_EQ(s.rate, 22050u);
    EXPECT_EQ(s.frames(), 4u);
    EXPECT_EQ(s.pcm[0], 100);
    EXPECT_EQ(s.pcm[1], 100);   // mono on both sides
    EXPECT_EQ(s.pcm[7], -400);
    EXPECT_TRUE(!s.loop);
    const Bytes looped = make_wav(true);
    EXPECT_TRUE(decode_banner_sound(looped.data(), looped.size(), s, error));
    EXPECT_TRUE(s.loop);
    EXPECT_EQ(s.loop_start, 2u);
    // LZ77-packed (every byte a literal: flag bytes of 0), under IMD5.
    Bytes lz;
    tag(lz, "LZ77");
    le32(lz, static_cast<std::uint32_t>(0x10 | (wav.size() << 8)));
    for (std::size_t i = 0; i < wav.size(); ++i) {
        if (i % 8 == 0) lz.push_back(0);
        lz.push_back(wav[i]);
    }
    Bytes imd5;
    tag(imd5, "IMD5");
    imd5.resize(0x20, 0);
    imd5.insert(imd5.end(), lz.begin(), lz.end());
    EXPECT_TRUE(decode_banner_sound(imd5.data(), imd5.size(), s, error));
    EXPECT_EQ(s.pcm[4], 300);
}

void test_aiff() {
    Bytes body;
    tag(body, "AIFF");
    tag(body, "COMM");
    be32(body, 18);
    be16(body, 2);        // stereo
    be32(body, 2);        // frames
    be16(body, 16);
    be16(body, 16383 + 14);              // 22050 = 1.3458... x 2^14
    be32(body, 22050u << 17);
    be32(body, 0);
    tag(body, "SSND");
    be32(body, 8 + 8);
    be32(body, 0);
    be32(body, 0);
    for (std::int16_t v : {1, -1, 2, -2}) be16(body, static_cast<std::uint16_t>(v));
    Bytes b;
    tag(b, "FORM");
    be32(b, static_cast<std::uint32_t>(body.size()));
    b.insert(b.end(), body.begin(), body.end());
    BannerSound s;
    std::string error;
    EXPECT_TRUE(decode_banner_sound(b.data(), b.size(), s, error));
    EXPECT_EQ(s.rate, 22050u);
    EXPECT_EQ(s.frames(), 2u);
    EXPECT_EQ(s.pcm[1], -1);
    EXPECT_EQ(s.pcm[3], -2);
    const Bytes junk = {'O', 'G', 'G', 'S', 0, 0, 0, 0, 0, 0, 0, 0, 0};
    EXPECT_TRUE(!decode_banner_sound(junk.data(), junk.size(), s, error));
}

// What a crafted sound.bin can ask for. Before the fix, a 264-byte BNS
// decoded to 3.84 million samples (about 23 MB at peak) and a few bytes
// of LZ77 reserved 16 MB, both on the menu's thread.
void test_hostile() {
    BannerSound s;
    std::string error;
    const auto set32 = [](Bytes& b, std::size_t at, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) b[at + i] = static_cast<std::uint8_t>(v >> (24 - 8 * i));
    };
    const std::size_t info = 0x28;  // INFO's body in make_bns
    {
        // A frame count far past the data: cut to what the data holds.
        Bytes bns = make_bns();
        set32(bns, info + 12, 1920000);
        EXPECT_TRUE(decode_banner_sound(bns.data(), bns.size(), s, error));
        EXPECT_EQ(s.frames(), 14u);
    }
    {
        // Offsets near 4 GB, which wrapped round on 32 bits.
        for (std::size_t field : {std::size_t(16), std::size_t(0x18), std::size_t(0x20), std::size_t(0x24)}) {
            Bytes bns = make_bns();
            set32(bns, info + field, 0xFFFFFFF8u);
            EXPECT_TRUE(!decode_banner_sound(bns.data(), bns.size(), s, error) || s.frames() <= 14);
        }
    }
    {
        // A rate the mixer cannot play.
        Bytes wav = make_wav(false);
        for (std::uint32_t rate : {0u, 3999u, 48001u, 0xFFFFFFFFu}) {
            for (int i = 0; i < 4; ++i) wav[24 + i] = static_cast<std::uint8_t>(rate >> (8 * i));
            EXPECT_TRUE(!decode_banner_sound(wav.data(), wav.size(), s, error));
        }
        for (int i = 0; i < 4; ++i) wav[24 + i] = static_cast<std::uint8_t>(48000u >> (8 * i));
        EXPECT_TRUE(decode_banner_sound(wav.data(), wav.size(), s, error));
    }
    {
        // LZ77 claiming 16 MB from 16 bytes.
        Bytes lz;
        tag(lz, "LZ77");
        le32(lz, 0x10 | (0xFFFFFFu << 8));
        lz.resize(16, 0);
        EXPECT_TRUE(!decode_banner_sound(lz.data(), lz.size(), s, error));
        EXPECT_TRUE(error.find("too large") != std::string::npos);
    }
}

}  // namespace

int main() {
    test_adpcm();
    test_bns();
    test_wav();
    test_aiff();
    test_hostile();
    if (g_failures) {
        std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
        return 1;
    }
    std::cout << "bnrsound tests passed" << std::endl;
    return 0;
}
