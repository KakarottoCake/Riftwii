// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "menusounds.hpp"

#include <gccore.h>
#include <tremor/ivorbiscodec.h>
#include <tremor/ivorbisfile.h>

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "filelist.h"
#include "log.hpp"
#include "skin.hpp"

// What GuiSound plays, filled in by DecodeMenuSounds (see filelist.h).
const u8* button_click_pcm = nullptr;
s32 button_click_pcm_size = 0;
const u8* home_open_pcm = nullptr;
s32 home_open_pcm_size = 0;
const u8* home_close_pcm = nullptr;
s32 home_close_pcm_size = 0;

namespace riftwii::wii {
namespace {

// The sounds are all 48 kHz stereo, and half a second or so. A longer
// result means the file is not the one RiftWii shipped; it is refused here
// rather than trusted.
constexpr unsigned kExpectedRate = 48000;
constexpr std::size_t kMaxBytes = 48000 * 4 * 2;  // two seconds

// Tremor reads the Ogg through callbacks; this is the Ogg held in memory.
struct MemoryFile {
    const u8* data;
    std::size_t size;
    std::size_t position;
};

size_t MemRead(void* out, size_t size, size_t count, void* source) {
    MemoryFile* file = static_cast<MemoryFile*>(source);
    const std::size_t wanted = size * count;
    const std::size_t left = file->size - file->position;
    const std::size_t take = wanted < left ? wanted : left;
    std::memcpy(out, file->data + file->position, take);
    file->position += take;
    return size ? take / size : 0;
}

int MemSeek(void* source, ogg_int64_t offset, int whence) {
    MemoryFile* file = static_cast<MemoryFile*>(source);
    ogg_int64_t target = offset;
    if (whence == SEEK_CUR) target += static_cast<ogg_int64_t>(file->position);
    else if (whence == SEEK_END) target += static_cast<ogg_int64_t>(file->size);
    if (target < 0 || target > static_cast<ogg_int64_t>(file->size)) return -1;
    file->position = static_cast<std::size_t>(target);
    return 0;
}

int MemClose(void*) { return 0; }

long MemTell(void* source) { return static_cast<long>(static_cast<MemoryFile*>(source)->position); }

// Decodes `ogg` into a MEM2 block the DSP can read: 32-byte aligned, a whole
// number of 32-byte blocks, flushed from the cache. Returns null and says
// why in the log when it can't.
const u8* Decode(const char* name, const u8* ogg, std::size_t ogg_size, s32& out_bytes) {
    MemoryFile file{ogg, ogg_size, 0};
    ov_callbacks callbacks = {MemRead, MemSeek, MemClose, MemTell};
    OggVorbis_File vorbis;
    if (ov_open_callbacks(&file, &vorbis, nullptr, 0, callbacks) < 0) {
        logf("Menu sounds: %s is not a readable Ogg; it will be silent\n", name);
        return nullptr;
    }
    const vorbis_info* info = ov_info(&vorbis, -1);
    const ogg_int64_t frames = ov_pcm_total(&vorbis, -1);
    if (!info || info->channels != 2 || static_cast<unsigned>(info->rate) != kExpectedRate || frames <= 0 ||
        static_cast<std::size_t>(frames) * 4 > kMaxBytes) {
        logf("Menu sounds: %s is not 48 kHz stereo of at most two seconds; it will be silent\n", name);
        ov_clear(&vorbis);
        return nullptr;
    }
    const std::size_t pcm_bytes = static_cast<std::size_t>(frames) * 4;
    const std::size_t block_bytes = (pcm_bytes + 31) & ~std::size_t(31);
    u8* pcm = skin::Mem2Alloc(block_bytes);
    if (!pcm) {
        logf("Menu sounds: no MEM2 for %s (%u bytes); it will be silent\n", name,
             static_cast<unsigned>(block_bytes));
        ov_clear(&vorbis);
        return nullptr;
    }
    std::memset(pcm, 0, block_bytes);
    // ov_read hands back big-endian 16-bit samples on the Wii's CPU, which
    // is what ASND wants; it returns in pieces, so loop until the end.
    std::size_t filled = 0;
    while (filled < pcm_bytes) {
        int section = 0;
        const long got = ov_read(&vorbis, reinterpret_cast<char*>(pcm) + filled,
                                 static_cast<int>(pcm_bytes - filled), &section);
        if (got <= 0) break;
        filled += static_cast<std::size_t>(got);
    }
    ov_clear(&vorbis);
    DCFlushRange(pcm, block_bytes);
    out_bytes = static_cast<s32>(block_bytes);
    logf("Menu sounds: %s decoded, %u ms, %u bytes of MEM2 (the Ogg is %u bytes)\n", name,
         static_cast<unsigned>(frames * 1000 / kExpectedRate), static_cast<unsigned>(block_bytes),
         static_cast<unsigned>(ogg_size));
    return pcm;
}

}  // namespace

void DecodeMenuSounds() {
    static bool done = false;
    if (done) return;
    done = true;
    button_click_pcm = Decode("button_click", button_click_ogg, button_click_ogg_size, button_click_pcm_size);
    home_open_pcm = Decode("home_open", home_open_ogg, home_open_ogg_size, home_open_pcm_size);
    home_close_pcm = Decode("home_close", home_close_ogg, home_close_ogg_size, home_close_pcm_size);
}

}  // namespace riftwii::wii
