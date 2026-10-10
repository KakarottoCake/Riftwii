// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// A banner's sound (opening.bnr's meta/sound.bin), as the Wii Menu plays
// it while a channel is shown: an IMD5 header over a BNS stream (the
// GameCube DSP's ADPCM, with an intro and a loop), a WAV (16-bit PCM) or
// an AIFF; a WAV or an AIFF may be LZ77-packed. The layouts follow
// WiiBrew's Opening.bnr page and the DSP ADPCM codec's public description.
namespace riftwii {

struct BannerSound {
    std::vector<std::int16_t> pcm;  // stereo, left then right, in the host's byte order
    unsigned rate = 0;              // samples a second
    bool loop = false;              // from loop_start to the end, again and again after the first time through
    std::size_t loop_start = 0;     // in frames (left-right pairs)
    std::size_t frames() const { return pcm.size() / 2; }
};

// False (and `error` says why) for anything else, or a rate outside
// 4000-48000 Hz. A sound longer than `max_frames` is cut there, not
// refused (banners' run some seconds), and never longer than its data.
bool decode_banner_sound(const std::uint8_t* data, std::size_t size, BannerSound& out, std::string& error,
                         std::size_t max_frames = 48000 * 40);

// The DSP's ADPCM: 8-byte frames of 14 samples each, the first byte a
// coefficient pair's index (high four bits) and a shift (low four), then
// 14 signed four-bit steps. `coef` holds the eight pairs; `hist1` and
// `hist2` the two samples before the first. Writes `count` samples to
// `out`, `stride` apart; fewer if `bytes` runs out.
void decode_dsp_adpcm(const std::uint8_t* data, std::size_t bytes, std::size_t count, const std::int16_t coef[16],
                      std::int16_t hist1, std::int16_t hist2, std::int16_t* out, std::size_t stride);

}  // namespace riftwii
