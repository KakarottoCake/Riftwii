// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "menusounds.hpp"

#include <gccore.h>

#include "log.hpp"
#include "skin.hpp"
#include "stash.hpp"
#include "zstd.h"

namespace {

// What a sound that could not be unpacked plays: one DSP block of silence.
alignas(32) const u8 kSilence[64] = {};

}  // namespace

const u8* button_over_pcm = kSilence;
u32 button_over_pcm_size = sizeof(kSilence);
const u8* button_click_pcm = kSilence;
u32 button_click_pcm_size = sizeof(kSilence);
const u8* home_open_pcm = kSilence;
u32 home_open_pcm_size = sizeof(kSilence);
const u8* home_close_pcm = kSilence;
u32 home_close_pcm_size = sizeof(kSilence);

namespace riftwii::wii {
namespace {

// One stash entry into MEM2. The DSP reads the sound from memory, so the
// buffer is 32-byte aligned (Mem2Alloc's), padded with silence to whole
// 32-byte blocks and flushed from the cache.
bool Unpack(const char* name, const u8*& sound, u32& bytes) {
    const u8* packed = nullptr;
    u32 packed_size = 0;
    if (!stash::Find(name, packed, packed_size)) {
        logf("Menu sounds: %s is not in the start-up stash; it plays silence\n", name);
        return false;
    }
    const unsigned long long full = ZSTD_getFrameContentSize(packed, packed_size);
    if (full == ZSTD_CONTENTSIZE_UNKNOWN || full == ZSTD_CONTENTSIZE_ERROR || full == 0 || full > (1u << 20)) {
        logf("Menu sounds: %s is damaged in the start-up stash; it plays silence\n", name);
        return false;
    }
    const std::size_t length = static_cast<std::size_t>(full);
    const std::size_t padded = (length + 31) & ~std::size_t(31);
    u8* buffer = skin::Mem2Alloc(padded);
    if (buffer == nullptr) {
        logf("Menu sounds: no MEM2 for %s (%u bytes); it plays silence\n", name, static_cast<unsigned>(padded));
        return false;
    }
    if (ZSTD_decompress(buffer, length, packed, packed_size) != length) {
        logf("Menu sounds: %s could not be unpacked; it plays silence\n", name);
        return false;
    }
    for (std::size_t i = length; i < padded; ++i) buffer[i] = 0;
    DCFlushRange(buffer, padded);
    sound = buffer;
    bytes = static_cast<u32>(length);
    return true;
}

}  // namespace

void UnpackMenuSounds() {
    unsigned unpacked = 0;
    unpacked += Unpack("button_over.pcm.zst", button_over_pcm, button_over_pcm_size);
    unpacked += Unpack("button_click.pcm.zst", button_click_pcm, button_click_pcm_size);
    unpacked += Unpack("home_open.pcm.zst", home_open_pcm, home_open_pcm_size);
    unpacked += Unpack("home_close.pcm.zst", home_close_pcm, home_close_pcm_size);
    const unsigned bytes = button_over_pcm_size + button_click_pcm_size + home_open_pcm_size + home_close_pcm_size;
    logf("Menu sounds: %u of 4 unpacked from the start-up stash into MEM2 (%u bytes)\n", unpacked, bytes);
}

}  // namespace riftwii::wii
