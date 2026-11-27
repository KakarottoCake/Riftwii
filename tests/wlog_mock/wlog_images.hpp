// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
// Synthetic headers let the real planner/install code run on the host.
// Real PowerPC images are checked separately by Makefile.runtime.
alignas(32) inline unsigned char riftwii_wlog_bin[512]{};
alignas(32) inline unsigned char riftwii_wlog_trace_bin[480]{};
alignas(32) inline unsigned char riftwii_wlog_translate_bin[448]{};
inline constexpr std::uint32_t riftwii_wlog_bin_size = sizeof(riftwii_wlog_bin);
inline constexpr std::uint32_t riftwii_wlog_trace_bin_size = sizeof(riftwii_wlog_trace_bin);
inline constexpr std::uint32_t riftwii_wlog_translate_bin_size = sizeof(riftwii_wlog_translate_bin);

// The three as the DOL embeds them (tools/pack_blobs.py): their sizes, then
// one zstd frame, here of one raw (stored) block, which the real decoder
// reads as it reads a compressed one. pack_wlog_images() fills it once the
// test has written the images.
inline constexpr std::size_t kWlogImagesBytes =
    sizeof(riftwii_wlog_bin) + sizeof(riftwii_wlog_trace_bin) + sizeof(riftwii_wlog_translate_bin);
alignas(32) inline unsigned char riftwii_wlogs_bin[12 + 9 + 3 + kWlogImagesBytes]{};
inline std::size_t riftwii_wlogs_bin_size = 0;

inline void pack_wlog_images() {
    const unsigned char* images[] = {riftwii_wlog_bin, riftwii_wlog_trace_bin, riftwii_wlog_translate_bin};
    const std::uint32_t sizes[] = {riftwii_wlog_bin_size, riftwii_wlog_trace_bin_size, riftwii_wlog_translate_bin_size};
    unsigned char* p = riftwii_wlogs_bin;
    for (std::uint32_t size : sizes) {
        *p++ = static_cast<unsigned char>(size >> 24);
        *p++ = static_cast<unsigned char>(size >> 16);
        *p++ = static_cast<unsigned char>(size >> 8);
        *p++ = static_cast<unsigned char>(size);
    }
    const std::uint32_t total = static_cast<std::uint32_t>(kWlogImagesBytes);
    // Magic; a single-segment frame with a four-byte content size.
    const unsigned char frame[] = {0x28, 0xB5, 0x2F, 0xFD, 0xA0};
    std::memcpy(p, frame, sizeof(frame));
    p += sizeof(frame);
    for (int i = 0; i < 4; ++i) *p++ = static_cast<unsigned char>(total >> (8 * i));
    // The last block, raw: its size, then its bytes as they are.
    const std::uint32_t block = (total << 3) | 1;
    for (int i = 0; i < 3; ++i) *p++ = static_cast<unsigned char>(block >> (8 * i));
    for (int i = 0; i < 3; ++i) {
        std::memcpy(p, images[i], sizes[i]);
        p += sizes[i];
    }
    riftwii_wlogs_bin_size = static_cast<std::size_t>(p - riftwii_wlogs_bin);
}
