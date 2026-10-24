// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/wiisound.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "riftwii/checkedalloc.hpp"
#include "wiisound_read.hpp"

namespace riftwii {
using namespace sound_detail;
namespace {

// Instrument references share the bank DATA body's base, even inside key
// splits. A modified bank cannot form an unbounded reference cycle here.
bool instrument(SoundBytes bank, std::size_t ref, unsigned key, unsigned velocity, SoundBytes& out) {
    for (unsigned depth = 0; depth < 8; ++depth) {
        if (!bank.contains(ref, 8)) return false;
        const unsigned type = bank.data[ref + 1];
        SoundBytes node;
        if (!reference(bank, ref, 4, node)) return false;
        if (type == 1) {
            if (node.size < 20) return false;
            out = node;
            return true;
        }
        const unsigned value = depth == 0 ? key : velocity;
        unsigned selected = 0;
        std::size_t references = 0;
        if (type == 2) {
            const unsigned count = node.data[0];
            if (count == 0 || count > 128 || !node.contains(1, count)) return false;
            unsigned previous = 0;
            for (unsigned i = 0; i < count; ++i) {
                const unsigned upper = node.data[1 + i];
                if (upper > 127 || (i && upper <= previous)) return false;
                previous = upper;
            }
            while (selected < count && value > node.data[1 + selected]) ++selected;
            if (selected == count) return false;
            references = (count + 4) & ~std::size_t(3);
            if (!node.contains(references, count * 8)) return false;
        } else if (type == 3) {
            const unsigned low = node.data[0], high = node.data[1];
            if (high < low || high > 127 || value < low || value > high) return false;
            selected = value - low;
            references = 4;
            if (!node.contains(references, (high - low + 1) * 8)) return false;
        } else {
            return false;
        }
        ref = static_cast<std::size_t>(node.data - bank.data) + references + selected * 8;
    }
    return false;
}

bool variable(SoundBytes sequence, std::size_t& position, std::uint32_t& value) {
    value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (!sequence.contains(position, 1)) return false;
        const unsigned byte = sequence.data[position++];
        value = (value << 7) | (byte & 127);
        if (byte < 128) return true;
    }
    return false;
}

float pitch_float(const std::uint8_t* data) {
    const std::uint32_t bits = word(data);
    float pitch = 0;
    std::memcpy(&pitch, &bits, sizeof pitch);
    return pitch;
}

// A compact envelope in sample time. The bank's 127 is immediate; lower
// values extend the stage exponentially. This is RiftWii's renderer, not
// Nintendo's DSP: reverb, filtering and the exact hardware envelope curve
// are deliberately not claimed to be bit-identical.
float envelope_step(unsigned value, unsigned rate) {
    if (value >= 127) return 1;
    const double seconds = 0.001 * std::pow(2.0, (127.0 - value) / 8.0);
    return static_cast<float>(1.0 / (seconds * rate));
}

}  // namespace

bool WiiSoundPlayer::fail(const char* why) {
    error_ = why;
    for (Track& track : tracks_) track.active = false;
    for (Voice& voice : voices_) voice.active = false;
    return false;
}

std::size_t WiiSoundPlayer::sample_bytes() const {
    std::size_t bytes = 0;
    for (const BannerSound& wave : waves_) bytes += wave.pcm.size() * sizeof(std::int16_t);
    return bytes;
}

bool WiiSoundPlayer::prepare(const WiiSoundArchive& archive, const std::string& name,
                             bool repeat, std::string& error) {
    *this = WiiSoundPlayer{};
    repeat_ = repeat;
    // Error reporting on the producer thread must not allocate either.
    if (!try_reserve(error_, 128)) return reject(error, "no memory for the sound renderer");
    ArchiveSound sound;
    SoundBytes file, audio;
    if (!archive.find(name, sound, error) || !archive.file(sound.file_id, file, audio, error)) return false;
    archive_volume_ = std::min(sound.volume, 127u);
    const SoundBytes detail = archive.info().slice(sound.detail, 12);
    if (!detail.data) return reject(error, "the sound details are truncated");
    if (sound.type == 3) {
        SoundBytes data;
        if (!header(file, "RWSD", error) || !section(file, 16, "DATA", data, error)) return false;
        const SoundBytes body = data.slice(8, data.size - 8);
        SoundBytes entry, notes, first_note, metadata, samples;
        if (!body.contains(0, 4) || word(body.data) > 4096 || word(detail.data) >= word(body.data) ||
            !body.contains(4, std::size_t(word(body.data)) * 8) ||
            !item(body, body, word(detail.data), 24, entry) ||
            !table(body, static_cast<std::size_t>(entry.data - body.data) + 16, notes) ||
            word(notes.data) != 1 || !item(body, notes, 0, 20, first_note))
            return reject(error, "the RWSD note table is damaged or is not a single wave");
        if (!menu_wave_at(file, audio, word(first_note.data), metadata, samples, error) ||
            !try_resize(waves_, 1) || !decode_menu_wave(metadata, samples, waves_[0], error)) return false;
        Voice& voice = voices_[0];
        voice.active = true;
        voice.ignore_off = true;
        voice.envelope = 1;
        voice.stage = 2;
        voice.gain_left = voice.gain_right = 1;
        voice.step = static_cast<double>(waves_[0].rate) / kRate;
        voice.remaining = std::numeric_limits<std::uint32_t>::max();
        return true;
    }
    if (sound.type != 1) return reject(error, "the System Menu sound is an unsupported external stream");
    SoundBytes sequence_data, bank_file, bank_audio, bank_section;
    if (!header(file, "RSEQ", error) || !section(file, 16, "DATA", sequence_data, error) ||
        !sequence_data.contains(8, 4)) return false;
    const std::size_t start = word(sequence_data.data + 8);
    if (start < 12 || !sequence_data.contains(start, 1)) return reject(error, "the RSEQ command start is invalid");
    sequence_ = sequence_data.slice(start, sequence_data.size - start);
    if (!sequence_.contains(word(detail.data), 1)) return reject(error, "the RSEQ entry offset is invalid");
    if (!archive.bank(word(detail.data + 4), bank_file, bank_audio, error) ||
        !header(bank_file, "RBNK", error) || !section(bank_file, 16, "DATA", bank_section, error)) return false;
    bank_data_ = bank_section.slice(8, bank_section.size - 8);
    if (!bank_data_.contains(0, 4) || word(bank_data_.data) > 256 ||
        !bank_data_.contains(4, std::size_t(word(bank_data_.data)) * 8))
        return reject(error, "the RBNK instrument count is invalid");
    // Decode the bank up front. There are six short samples in the known
    // menu music bank; music never needs a whole decoded song in memory.
    SoundBytes wave_section;
    if (!section(bank_file, 24, "WAVE", wave_section, error) || !wave_section.contains(8, 4)) return false;
    const unsigned count = word(wave_section.data + 8);
    if (count == 0 || count > 128 || !try_resize(waves_, count))
        return reject(error, "the RBNK wave count exceeds the menu sound budget");
    std::size_t bytes = 0;
    for (unsigned i = 0; i < count; ++i) {
        SoundBytes metadata, samples;
        if (!menu_wave_at(bank_file, bank_audio, i, metadata, samples, error) ||
            !decode_menu_wave(metadata, samples, waves_[i], error)) return false;
        bytes += waves_[i].pcm.size() * sizeof(std::int16_t);
        if (bytes > 2 * 1024 * 1024) return reject(error, "the decoded bank exceeds two MiB");
    }
    tracks_[0].active = true;
    tracks_[0].position = word(detail.data);
    return true;
}

bool WiiSoundPlayer::note(unsigned track_index, unsigned key, unsigned velocity, std::uint32_t duration) {
    Track& track = tracks_[track_index];
    if (track.program >= word(bank_data_.data)) return fail("the sequence chose an absent instrument");
    SoundBytes entry;
    if (!instrument(bank_data_, 4 + track.program * 8, key, velocity, entry))
        return fail("the instrument split is damaged or unsupported");
    const unsigned wave_index = word(entry.data);
    const float pitch = pitch_float(entry.data + 16);
    if (wave_index >= waves_.size() || entry.data[9] != 0 || !std::isfinite(pitch) || pitch < 0.125f || pitch > 8)
        return fail("the instrument wave, address type or pitch is invalid");
    Voice* available = nullptr;
    for (Voice& voice : voices_) {
        if (!voice.active) {
            available = &voice;
            break;
        }
    }
    if (!available) return fail("the sequence exceeds 64 simultaneous voices");
    Voice& voice = *available;
    voice = {};
    voice.active = true;
    voice.wave = wave_index;
    voice.track = track_index;
    const double semitones = static_cast<int>(key) + track.transpose - entry.data[12] +
                             track.bend * static_cast<double>(track.bend_range) / 128;
    voice.step = waves_[wave_index].rate / static_cast<double>(kRate) * pitch * std::pow(2.0, semitones / 12);
    if (!std::isfinite(voice.step) || voice.step < 0.001 || voice.step > 256)
        return fail("the sequence pitch is outside the renderer's range");
    const float velocity_gain = static_cast<float>(velocity) / 127;
    const float gain = velocity_gain * velocity_gain * entry.data[13] / 127;
    const int pan = std::clamp(static_cast<int>(track.pan) + entry.data[14] - 64, 0, 127);
    voice.gain_left = gain * static_cast<float>(std::sqrt((127 - pan) / 127.0));
    voice.gain_right = gain * static_cast<float>(std::sqrt(pan / 127.0));
    const auto parameter = [&](int override_value, unsigned field) {
        return override_value >= 0 ? static_cast<unsigned>(override_value) : std::min<unsigned>(entry.data[field], 127);
    };
    voice.attack = envelope_step(parameter(track.attack, 4), kRate);
    voice.decay = envelope_step(parameter(track.decay, 5), kRate);
    voice.sustain = parameter(track.sustain, 6) / 127.0f;
    voice.release = envelope_step(parameter(track.release, 7), kRate);
    voice.ignore_off = entry.data[10] == 1 || duration == 0;
    voice.remaining = duration == 0 ? std::numeric_limits<std::uint32_t>::max() : duration;
    return true;
}

bool WiiSoundPlayer::commands(unsigned index) {
    Track& track = tracks_[index];
    // A zero-time jump/call cycle must never lock the menu's audio thread.
    for (unsigned budget = 0; budget < 4096 && track.active && track.wait == 0; ++budget) {
        if (!sequence_.contains(track.position, 1)) return fail("the sequence command is truncated");
        unsigned command = sequence_.data[track.position++];
        bool random = false;
        int random_value = 0;
        if (command == 0xa0) {
            if (!sequence_.contains(track.position, 5)) return fail("the random sequence command is truncated");
            command = sequence_.data[track.position++];
            const int low = static_cast<std::int16_t>(half(sequence_.data + track.position));
            const int high = static_cast<std::int16_t>(half(sequence_.data + track.position + 2));
            track.position += 4;
            if (low > high) return fail("the random sequence range is invalid");
            // Deterministic midpoint keeps menu playback reproducible.
            random_value = low + (high - low) / 2;
            random = true;
        }
        if (command < 0x80) {
            std::uint32_t duration = 0;
            if (random || !sequence_.contains(track.position, 1)) return fail("the sequence note is truncated");
            const unsigned velocity = sequence_.data[track.position++];
            if (velocity > 127 || !variable(sequence_, track.position, duration)) return fail("the note length is invalid");
            if (!note(index, command, velocity, duration)) return false;
            if (track.note_wait) track.wait = duration;
        } else if (command == 0x80 || command == 0x81) {
            std::uint32_t value = 0;
            if (random || !variable(sequence_, track.position, value)) return fail("the sequence variable is truncated");
            if (command == 0x80) track.wait = value;
            else track.program = value;
        } else if (command == 0x88 || command == 0x89 || command == 0x8a) {
            const unsigned arguments = command == 0x88 ? 4 : 3;
            if (random || !sequence_.contains(track.position, arguments)) return fail("the sequence branch is truncated");
            unsigned target_track = index;
            if (command == 0x88) target_track = sequence_.data[track.position++];
            const std::uint8_t* p = sequence_.data + track.position;
            const std::size_t target = (std::size_t(p[0]) << 16) | (std::size_t(p[1]) << 8) | p[2];
            track.position += 3;
            if (target_track >= tracks_.size() || !sequence_.contains(target, 1)) return fail("the sequence branch target is invalid");
            if (command == 0x88) {
                if (target_track == index || tracks_[target_track].active) return fail("the sequence reopened an active track");
                tracks_[target_track] = {};
                tracks_[target_track].active = true;
                tracks_[target_track].position = target;
            } else {
                if (command == 0x8a) {
                    if (track.depth == track.returns.size()) return fail("the sequence call stack is full");
                    track.returns[track.depth++] = track.position;
                } else if (!repeat_ && target < track.position) {
                    track.active = false;
                    continue;
                }
                track.position = target;
            }
        } else if (command >= 0xb0 && command <= 0xdf) {
            if (!random && !sequence_.contains(track.position, 1)) return fail("the sequence parameter is truncated");
            const int value = random ? random_value : sequence_.data[track.position++];
            switch (command) {
                case 0xb0:
                    if (value < 1 || value > 192) return fail("the sequence time base is invalid");
                    timebase_ = static_cast<unsigned>(value);
                    break;
                case 0xc0: track.pan = static_cast<unsigned>(std::clamp(value, 0, 127)); break;
                case 0xc1: track.volume = static_cast<unsigned>(std::clamp(value, 0, 127)); break;
                case 0xc2: master_ = static_cast<unsigned>(std::clamp(value, 0, 127)); break;
                case 0xc3: track.transpose = static_cast<std::int8_t>(value); break;
                case 0xc4: track.bend = static_cast<std::int8_t>(value); break;
                case 0xc5:
                    if (value < 0 || value > 24) return fail("the pitch bend range is invalid");
                    track.bend_range = static_cast<unsigned>(value);
                    break;
                case 0xc7: track.note_wait = value != 0; break;
                case 0xd0: track.attack = std::clamp(value, 0, 127); break;
                case 0xd1: track.decay = std::clamp(value, 0, 127); break;
                case 0xd2: track.sustain = std::clamp(value, 0, 127); break;
                case 0xd3: track.release = std::clamp(value, 0, 127); break;
                case 0xd4:
                    if (track.loop_count) return fail("nested sequence loops are unsupported");
                    track.loop_position = track.position;
                    track.loop_count = value == 0 ? 256 : static_cast<unsigned>(value);
                    break;
                // These describe console mixing effects or allocation priority;
                // they do not change the bytecode's control flow.
                case 0xc6: case 0xd7: case 0xd8: case 0xd9: case 0xda: case 0xdb: case 0xdc: case 0xde:
                    break;
                default: return fail("the sequence uses an unsupported controller");
            }
        } else if (command == 0xe1 || command == 0xfe) {
            if (random || !sequence_.contains(track.position, 2)) return fail("the sequence tempo or track mask is truncated");
            const unsigned value = half(sequence_.data + track.position);
            track.position += 2;
            if (command == 0xe1) {
                if (value < 20 || value > 600) return fail("the sequence tempo is invalid");
                tempo_ = value;
            }
        } else if (command == 0xfc) {
            if (!track.loop_count) return fail("the sequence ended a loop that was not open");
            if (--track.loop_count) track.position = track.loop_position;
        } else if (command == 0xfd) {
            if (!track.depth) return fail("the sequence returned with an empty stack");
            track.position = track.returns[--track.depth];
        } else if (command == 0xff) {
            track.active = false;
        } else {
            return fail("the sequence uses an unsupported command");
        }
    }
    if (track.active && track.wait == 0) return fail("the sequence exceeded its command budget");
    return true;
}

bool WiiSoundPlayer::tick() {
    for (Voice& voice : voices_) {
        if (voice.active && !voice.ignore_off && voice.remaining && --voice.remaining == 0) voice.stage = 3;
    }
    for (unsigned i = 0; i < tracks_.size(); ++i) {
        Track& track = tracks_[i];
        if (!track.active) continue;
        if (track.wait) --track.wait;
        if (!commands(i)) return false;
    }
    return true;
}

bool WiiSoundPlayer::finished() const {
    for (const Track& track : tracks_) if (track.active) return false;
    for (const Voice& voice : voices_) if (voice.active) return false;
    return true;
}

bool WiiSoundPlayer::render(std::int16_t* stereo, std::size_t frames) {
    if (!stereo || frames > kRate * 5 || !error_.empty()) return false;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (until_tick_ <= 0) {
            if (!tick()) return false;
            until_tick_ += kRate * 60.0 / (tempo_ * timebase_);
        }
        --until_tick_;
        float left = 0, right = 0;
        for (Voice& voice : voices_) {
            if (!voice.active) continue;
            const BannerSound& wave = waves_[voice.wave];
            if (voice.position >= wave.frames()) {
                if (wave.loop && (repeat_ || !voice.ignore_off)) {
                    const double loop_length = static_cast<double>(wave.frames() - wave.loop_start);
                    voice.position = wave.loop_start + std::fmod(voice.position - wave.loop_start, loop_length);
                } else {
                    voice.active = false;
                    continue;
                }
            }
            if (voice.stage == 0) {
                voice.envelope = std::min(1.0f, voice.envelope + voice.attack);
                if (voice.envelope == 1) voice.stage = 1;
            } else if (voice.stage == 1) {
                voice.envelope = std::max(voice.sustain, voice.envelope - voice.decay);
                if (voice.envelope == voice.sustain) voice.stage = 2;
            } else if (voice.stage == 3) {
                voice.envelope = std::max(0.0f, voice.envelope - voice.release);
                if (voice.envelope == 0) {
                    voice.active = false;
                    continue;
                }
            }
            const std::size_t at = static_cast<std::size_t>(voice.position);
            const std::size_t next = std::min(at + 1, wave.frames() - 1);
            const float fraction = static_cast<float>(voice.position - at);
            const float sample_left = wave.pcm[at * 2] + (wave.pcm[next * 2] - wave.pcm[at * 2]) * fraction;
            const float sample_right = wave.pcm[at * 2 + 1] + (wave.pcm[next * 2 + 1] - wave.pcm[at * 2 + 1]) * fraction;
            const float volume = tracks_[voice.track].volume * master_ * archive_volume_ / (127.0f * 127 * 127);
            left += sample_left * voice.gain_left * voice.envelope * volume;
            right += sample_right * voice.gain_right * voice.envelope * volume;
            voice.position += voice.step;
        }
        stereo[frame * 2] = static_cast<std::int16_t>(std::clamp(left, -32768.0f, 32767.0f));
        stereo[frame * 2 + 1] = static_cast<std::int16_t>(std::clamp(right, -32768.0f, 32767.0f));
    }
    return true;
}

}  // namespace riftwii
