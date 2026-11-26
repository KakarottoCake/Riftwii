// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "mp3stream.hpp"

#include <asndlib.h>
#include <gccore.h>
#include <mad.h>
#include <unistd.h>

#include <atomic>
#include <cstring>

#include "log.hpp"
#include "skin.hpp"

namespace riftwii::wii {
namespace {

// 4096 stereo frames a buffer: 85 ms at 48 kHz, two of them in turn.
constexpr std::size_t kFrames = 4096;
constexpr std::size_t kBufferBytes = kFrames * 2 * sizeof(std::int16_t);
constexpr std::size_t kStackBytes = 16 * 1024;
// The song's last bytes are decoded from here, followed by the zeros
// libmad needs after the final frame. A frame is at most 2881 bytes.
constexpr std::size_t kTailBytes = 4096;

struct State {
    mad_stream stream;
    mad_frame frame;
    mad_synth synth;
    alignas(32) std::int16_t out[2][kFrames * 2];
    alignas(32) std::uint8_t stack[kStackBytes];
    std::uint8_t tail[kTailBytes + MAD_BUFFER_GUARD];
};

State* g_state = nullptr;
bool g_decoder_open = false;
bool g_in_tail = false;
unsigned g_pcm_at = 0;  // of the synth's samples, the next one to play
unsigned g_rate = 0;
lwp_t g_thread = LWP_THREAD_NULL;
std::atomic<bool> g_running{false};
std::atomic<bool> g_ended{false};
std::atomic<int> g_volume{255};

std::int16_t ToSample(mad_fixed_t value) {
    value += 1L << (MAD_F_FRACBITS - 16);  // round
    if (value >= MAD_F_ONE) value = MAD_F_ONE - 1;
    else if (value < -MAD_F_ONE) value = -MAD_F_ONE;
    return static_cast<std::int16_t>(value >> (MAD_F_FRACBITS + 1 - 16));
}

// The next frame decoded into the synth's samples. False at the song's end
// or on damage libmad cannot step over.
bool NextFrame() {
    mad_stream& stream = g_state->stream;
    for (;;) {
        if (mad_frame_decode(&g_state->frame, &stream) == 0) {
            mad_synth_frame(&g_state->synth, &g_state->frame);
            g_pcm_at = 0;
            return true;
        }
        if (MAD_RECOVERABLE(stream.error)) continue;  // a tag or a broken frame: skipped
        if (stream.error != MAD_ERROR_BUFLEN || g_in_tail) return false;
        // Out of bytes: the last frame needs MAD_BUFFER_GUARD zeros after
        // it, which the song's own buffer does not have.
        const std::uint8_t* rest = stream.next_frame ? stream.next_frame : stream.this_frame;
        const std::size_t left = rest ? static_cast<std::size_t>(stream.bufend - rest) : 0;
        if (left == 0 || left > kTailBytes) return false;
        std::memcpy(g_state->tail, rest, left);
        std::memset(g_state->tail + left, 0, MAD_BUFFER_GUARD);
        mad_stream_buffer(&stream, g_state->tail, left + MAD_BUFFER_GUARD);
        g_in_tail = true;
    }
}

// Fills `out` with kFrames stereo frames; after the song's end, silence.
// False once the song has ended.
bool Render(std::int16_t* out) {
    const mad_pcm& pcm = g_state->synth.pcm;
    std::size_t done = 0;
    while (done < kFrames) {
        if (g_pcm_at >= pcm.length && !NextFrame()) {
            std::memset(out + done * 2, 0, (kFrames - done) * 2 * sizeof(std::int16_t));
            return false;
        }
        const bool stereo = pcm.channels == 2;
        for (; g_pcm_at < pcm.length && done < kFrames; ++g_pcm_at, ++done) {
            out[done * 2] = ToSample(pcm.samples[0][g_pcm_at]);
            out[done * 2 + 1] = ToSample(pcm.samples[stereo ? 1 : 0][g_pcm_at]);
        }
    }
    return true;
}

void CloseDecoder() {
    if (!g_decoder_open) return;
    mad_synth_finish(&g_state->synth);
    mad_frame_finish(&g_state->frame);
    mad_stream_finish(&g_state->stream);  // gives back libmad's own Layer III buffer
    g_decoder_open = false;
}

void VoiceCallback(s32) {
    // Nothing: the thread below feeds the voice; on an underrun it waits.
}

void* StreamThread(void*) {
    unsigned buffer = 1;
    bool more = true;
    while (more && g_running.load(std::memory_order_acquire)) {
        std::int16_t* data = g_state->out[buffer];
        if (ASND_TestPointer(0, data) != 0) {
            usleep(2000);
            continue;
        }
        more = Render(data);
        DCFlushRange(data, kBufferBytes);
        while (g_running.load() && ASND_TestVoiceBufferReady(0) != 1) usleep(2000);
        if (!g_running.load()) break;
        // As in wiimenusounds.cpp: AddVoice can refuse once while SetVoice's
        // first update is pending; the same buffer is offered again.
        unsigned retries = 0;
        while (g_running.load() && ASND_AddVoice(0, data, kBufferBytes) != SND_OK) {
            if (++retries == 250) {
                logf("Menu music: the mixer refused the next MP3 buffer for half a second; the song stops\n");
                more = false;
                break;
            }
            usleep(2000);
        }
        buffer ^= 1;
    }
    // The last two buffers still play: the song ends when they have.
    const unsigned tail_ms = static_cast<unsigned>(2 * kFrames * 1000 / (g_rate ? g_rate : 48000));
    for (unsigned waited = 0; waited < tail_ms && g_running.load(); waited += 10) usleep(10000);
    g_ended.store(true, std::memory_order_release);
    return nullptr;
}

}  // namespace

bool Mp3Start(const std::uint8_t* data, std::size_t bytes) {
    Mp3Stop();
    if (!g_state) {
        g_state = reinterpret_cast<State*>(skin::Mem2Alloc(sizeof(State)));
        if (!g_state) {
            logf("Menu music: there is no MEM2 for the MP3 decoder\n");
            return false;
        }
    }
    mad_stream_init(&g_state->stream);
    mad_frame_init(&g_state->frame);
    mad_synth_init(&g_state->synth);
    g_state->synth.pcm.length = 0;
    g_decoder_open = true;
    g_in_tail = false;
    mad_stream_buffer(&g_state->stream, data, bytes);
    // The first frame gives the rate the voice plays at.
    if (!NextFrame()) {
        logf("Menu music: no MP3 frame could be decoded\n");
        CloseDecoder();
        return false;
    }
    g_rate = g_state->synth.pcm.samplerate;
    Render(g_state->out[0]);
    DCFlushRange(g_state->out[0], kBufferBytes);
    g_ended.store(false);
    g_running.store(true, std::memory_order_release);
    const int volume = g_volume.load();
    ASND_StopVoice(0);
    if (ASND_SetVoice(0, VOICE_STEREO_16BIT, static_cast<s32>(g_rate), 0, g_state->out[0], kBufferBytes, volume,
                      volume, VoiceCallback) != SND_OK ||
        LWP_CreateThread(&g_thread, StreamThread, nullptr, g_state->stack, kStackBytes, 80) < 0) {
        g_running.store(false);
        g_thread = LWP_THREAD_NULL;
        ASND_StopVoice(0);
        CloseDecoder();
        logf("Menu music: the MP3 could not start playing\n");
        return false;
    }
    return true;
}

void Mp3Stop() {
    g_running.store(false, std::memory_order_release);
    if (g_thread != LWP_THREAD_NULL) {
        LWP_JoinThread(g_thread, nullptr);
        g_thread = LWP_THREAD_NULL;
        ASND_StopVoice(0);
    }
    if (g_state) CloseDecoder();
}

bool Mp3Playing() { return g_thread != LWP_THREAD_NULL && !g_ended.load(std::memory_order_acquire); }

void Mp3Volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 255) volume = 255;
    g_volume.store(volume);
    if (g_thread != LWP_THREAD_NULL) ASND_ChangeVolumeVoice(0, volume, volume);
}

}  // namespace riftwii::wii
