// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/bnrsound.hpp"

#include <algorithm>
#include <cstring>

#include "riftwii/bnr.hpp"

namespace riftwii {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
std::uint16_t be16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }
std::uint32_t le32(const std::uint8_t* p) {
    return (std::uint32_t(p[3]) << 24) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[1]) << 8) | p[0];
}
std::uint16_t le16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[1] << 8) | p[0]); }

// Mono becomes both sides; more than two channels keep the first two.
void interleave(const std::vector<std::vector<std::int16_t>>& ch, std::size_t frames, BannerSound& out) {
    out.pcm.assign(frames * 2, 0);
    for (std::size_t i = 0; i < frames; ++i) {
        out.pcm[2 * i] = ch[0][i];
        out.pcm[2 * i + 1] = ch.size() > 1 ? ch[1][i] : ch[0][i];
    }
}

// BNS: a header ('BNS ', 0xFEFF0100, size, header size, chunk count, then
// each chunk's offset and size), an INFO chunk and a DATA chunk. Offsets
// in INFO count from INFO's body (after its 8-byte header), the channels'
// data offsets from DATA's. Each channel's ADPCM lies in one piece.
bool decode_bns(const std::uint8_t* d, std::size_t size, BannerSound& out, std::string& error, std::size_t max_frames) {
    if (size < 0x20 || be32(d + 4) != 0xFEFF0100u) {
        error = "not a BNS stream";
        return false;
    }
    const unsigned chunks = be16(d + 0xE);
    const std::uint8_t* info = nullptr;
    const std::uint8_t* data = nullptr;
    std::size_t info_size = 0, data_size = 0;
    for (unsigned i = 0; i < chunks && 0x10 + 8 * (i + 1) <= size; ++i) {
        const std::size_t off = be32(d + 0x10 + 8 * i), len = be32(d + 0x14 + 8 * i);
        if (off > size || len < 8 || len > size - off) continue;
        if (std::memcmp(d + off, "INFO", 4) == 0) {
            info = d + off + 8;
            info_size = len - 8;
        } else if (std::memcmp(d + off, "DATA", 4) == 0) {
            data = d + off + 8;
            data_size = len - 8;
        }
    }
    if (!info || !data || info_size < 0x18) {
        error = "BNS without INFO or DATA";
        return false;
    }
    if (info[0] != 0) {
        error = "BNS in a codec other than ADPCM";
        return false;
    }
    const unsigned channels = info[2];
    if (channels < 1 || channels > 2) {
        error = "BNS with " + std::to_string(channels) + " channels";
        return false;
    }
    out.rate = be16(info + 4);
    out.loop = info[1] != 0;
    out.loop_start = be32(info + 8);
    const std::size_t frames = std::min<std::size_t>(be32(info + 12), max_frames);
    const std::size_t list = be32(info + 16);
    std::vector<std::vector<std::int16_t>> ch(channels, std::vector<std::int16_t>(frames, 0));
    for (unsigned c = 0; c < channels; ++c) {
        if (list + 4 * (c + 1) > info_size) {
            error = "BNS channel list out of range";
            return false;
        }
        const std::size_t entry = be32(info + list + 4 * c);
        if (entry + 12 > info_size) {
            error = "BNS channel out of range";
            return false;
        }
        const std::size_t at = be32(info + entry), dsp = be32(info + entry + 4);
        if (dsp + 0x30 > info_size || at > data_size) {
            error = "BNS channel data out of range";
            return false;
        }
        // The DSP's ADPCM info: 16 coefficients, the gain, the first
        // frame's header, then the two samples before it.
        std::int16_t coef[16];
        for (int k = 0; k < 16; ++k) coef[k] = static_cast<std::int16_t>(be16(info + dsp + 2 * k));
        const auto h1 = static_cast<std::int16_t>(be16(info + dsp + 0x24));
        const auto h2 = static_cast<std::int16_t>(be16(info + dsp + 0x26));
        decode_dsp_adpcm(data + at, data_size - at, frames, coef, h1, h2, ch[c].data(), 1);
    }
    interleave(ch, frames, out);
    if (out.loop_start >= frames) out.loop = false, out.loop_start = 0;
    return true;
}

// RIFF WAVE, 16-bit PCM; a "smpl" chunk's first loop, when it has one.
bool decode_wav(const std::uint8_t* d, std::size_t size, BannerSound& out, std::string& error, std::size_t max_frames) {
    if (size < 12 || std::memcmp(d + 8, "WAVE", 4) != 0) {
        error = "not a WAVE file";
        return false;
    }
    unsigned channels = 0, bits = 0;
    const std::uint8_t* samples = nullptr;
    std::size_t sample_bytes = 0;
    bool has_loop = false;
    std::size_t loop_start = 0;
    for (std::size_t at = 12; at + 8 <= size;) {
        const std::size_t len = le32(d + at + 4), body = at + 8;
        if (len > size - body) break;
        if (std::memcmp(d + at, "fmt ", 4) == 0 && len >= 16) {
            if (le16(d + body) != 1) {
                error = "WAVE that is not PCM";
                return false;
            }
            channels = le16(d + body + 2);
            out.rate = le32(d + body + 4);
            bits = le16(d + body + 14);
        } else if (std::memcmp(d + at, "data", 4) == 0) {
            samples = d + body;
            sample_bytes = len;
        } else if (std::memcmp(d + at, "smpl", 4) == 0 && len >= 0x24 + 24 && le32(d + body + 0x1C) > 0) {
            has_loop = true;
            loop_start = le32(d + body + 0x24 + 8);
        }
        at = body + len + (len & 1);
    }
    if (!samples || bits != 16 || channels < 1 || channels > 2) {
        error = "WAVE without 16-bit mono or stereo samples";
        return false;
    }
    const std::size_t frames = std::min(sample_bytes / (2 * channels), max_frames);
    std::vector<std::vector<std::int16_t>> ch(channels, std::vector<std::int16_t>(frames));
    for (std::size_t i = 0; i < frames; ++i)
        for (unsigned c = 0; c < channels; ++c) ch[c][i] = static_cast<std::int16_t>(le16(samples + 2 * (i * channels + c)));
    interleave(ch, frames, out);
    out.loop = has_loop && loop_start < frames;
    out.loop_start = out.loop ? loop_start : 0;
    return true;
}

// AIFF: COMM (channels, frames, bits, the rate as an 80-bit float) and
// SSND (an offset, a block size, then big-endian samples). Played once.
bool decode_aiff(const std::uint8_t* d, std::size_t size, BannerSound& out, std::string& error, std::size_t max_frames) {
    if (size < 12 || std::memcmp(d + 8, "AIFF", 4) != 0) {
        error = "not an AIFF file";
        return false;
    }
    unsigned channels = 0, bits = 0;
    std::size_t count = 0;
    const std::uint8_t* samples = nullptr;
    std::size_t sample_bytes = 0;
    for (std::size_t at = 12; at + 8 <= size;) {
        const std::size_t len = be32(d + at + 4), body = at + 8;
        if (len > size - body) break;
        if (std::memcmp(d + at, "COMM", 4) == 0 && len >= 18) {
            channels = be16(d + body);
            count = be32(d + body + 2);
            bits = be16(d + body + 6);
            const int exponent = (be16(d + body + 8) & 0x7FFF) - 16383;
            const std::uint32_t hi = be32(d + body + 10);
            out.rate = exponent >= 0 && exponent < 32 ? static_cast<unsigned>(hi >> (31 - exponent)) : 0;
        } else if (std::memcmp(d + at, "SSND", 4) == 0 && len >= 8) {
            const std::size_t skip = be32(d + body);
            if (skip <= len - 8) {
                samples = d + body + 8 + skip;
                sample_bytes = len - 8 - skip;
            }
        }
        at = body + len + (len & 1);
    }
    if (!samples || bits != 16 || channels < 1 || channels > 2 || out.rate == 0) {
        error = "AIFF without 16-bit mono or stereo samples";
        return false;
    }
    const std::size_t frames = std::min({count, sample_bytes / (2 * channels), max_frames});
    std::vector<std::vector<std::int16_t>> ch(channels, std::vector<std::int16_t>(frames));
    for (std::size_t i = 0; i < frames; ++i)
        for (unsigned c = 0; c < channels; ++c) ch[c][i] = static_cast<std::int16_t>(be16(samples + 2 * (i * channels + c)));
    interleave(ch, frames, out);
    out.loop = false;
    out.loop_start = 0;
    return true;
}

}  // namespace

void decode_dsp_adpcm(const std::uint8_t* data, std::size_t bytes, std::size_t count, const std::int16_t coef[16],
                      std::int16_t hist1, std::int16_t hist2, std::int16_t* out, std::size_t stride) {
    std::int32_t h1 = hist1, h2 = hist2;
    for (std::size_t i = 0; i < count;) {
        const std::size_t frame = i / 14;
        if (frame * 8 + 8 > bytes) break;
        const std::uint8_t* f = data + frame * 8;
        const std::int32_t scale = 1 << (f[0] & 0xF);
        const unsigned pair = (f[0] >> 4) & 7;
        const std::int64_t c1 = coef[2 * pair], c2 = coef[2 * pair + 1];
        for (int k = 0; k < 14 && i < count; ++k, ++i) {
            int step = (k & 1) ? (f[1 + k / 2] & 0xF) : (f[1 + k / 2] >> 4);
            if (step >= 8) step -= 16;
            std::int64_t s = (static_cast<std::int64_t>(step) * scale << 11) + 1024 + c1 * h1 + c2 * h2;
            s >>= 11;
            s = std::max<std::int64_t>(-32768, std::min<std::int64_t>(32767, s));
            out[i * stride] = static_cast<std::int16_t>(s);
            h2 = h1;
            h1 = static_cast<std::int32_t>(s);
        }
    }
}

bool decode_banner_sound(const std::uint8_t* data, std::size_t size, BannerSound& out, std::string& error,
                         std::size_t max_frames) {
    out = BannerSound();
    if (data && size >= 0x20 && std::memcmp(data, "IMD5", 4) == 0) {
        data += 0x20;
        size -= 0x20;
    }
    std::vector<std::uint8_t> unpacked;
    if (data && size >= 4 && std::memcmp(data, "LZ77", 4) == 0) {
        if (!lz77_decompress(data, size, unpacked, error)) return false;
        data = unpacked.data();
        size = unpacked.size();
    }
    if (!data || size < 12) {
        error = "no sound";
        return false;
    }
    bool ok = false;
    if (std::memcmp(data, "BNS ", 4) == 0) ok = decode_bns(data, size, out, error, max_frames);
    else if (std::memcmp(data, "RIFF", 4) == 0) ok = decode_wav(data, size, out, error, max_frames);
    else if (std::memcmp(data, "FORM", 4) == 0) ok = decode_aiff(data, size, out, error, max_frames);
    else error = "a sound in an unknown format";
    if (ok && (out.rate == 0 || out.pcm.empty())) {
        error = "an empty sound";
        ok = false;
    }
    if (!ok) out = BannerSound();
    return ok;
}

}  // namespace riftwii
