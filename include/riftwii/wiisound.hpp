// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/bnrsound.hpp"

// Read-only views of the player's System Menu resources. No console data is
// compiled in or written out. Layouts: WiiBrew U8 archive and Custom Mario
// Kart's BRSAR, BRWSD, BRWAR, BRWAV, BRBNK and BRSEQ format documentation.
namespace riftwii {

struct SoundBytes {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    bool contains(std::size_t offset, std::size_t length) const;
    SoundBytes slice(std::size_t offset, std::size_t length) const;
};

struct SoundResource {
    std::string name;
    SoundBytes bytes;
};

// Lists files, with full paths; validates directory nesting, names and file
// extents. At most 4096 entries, 32 directory levels, 255-byte names and
// 256 KiB of combined paths.
bool list_sound_u8(SoundBytes archive, std::vector<SoundResource>& files, std::string& error);

struct ArchiveSound {
    std::string name;
    unsigned type = 0;  // 1 sequence, 2 stream, 3 wave
    unsigned volume = 127;
    std::uint32_t file_id = 0;
    std::uint32_t detail = 0;
};

class WiiSoundArchive {
public:
    // The input must outlive this view and any player prepared from it.
    bool load(SoundBytes bytes, std::string& error);
    bool sounds(std::vector<ArchiveSound>& out, std::string& error) const;
    bool find(const std::string& name, ArchiveSound& out, std::string& error) const;
    bool file(std::uint32_t id, SoundBytes& header, SoundBytes& audio, std::string& error) const;
    bool bank(std::uint32_t id, SoundBytes& header, SoundBytes& audio, std::string& error) const;
    SoundBytes info() const { return info_; }

private:
    SoundBytes bytes_, symbols_, info_, files_;
};

// A wave's metadata, shared by old RBNK/RWSD WAVE blocks and RWAV INFO.
// Offsets stay relative to the bounded metadata/audio views.
bool decode_menu_wave(SoundBytes metadata, SoundBytes audio, BannerSound& out,
                      std::string& error, std::size_t max_frames = 48000 * 5);
bool decode_menu_rwav(SoundBytes file, BannerSound& out, std::string& error,
                      std::size_t max_frames = 48000 * 5);
bool menu_wave_at(SoundBytes file, SoundBytes audio, std::uint32_t index,
                  SoundBytes& metadata, SoundBytes& samples, std::string& error);

// A small sample-bank synthesizer. Preparation does every allocation; render
// uses fixed track/voice slots and never allocates. It supports the commands
// used by the System Menu, with bounded stacks, command counts and polyphony.
// Unknown commands fail closed instead of interpreting their operands as notes.
// Envelopes, pitch interpolation and stereo pan are rendered here; the console's
// optional reverb/filter DSP effects are not emulated.
class WiiSoundPlayer {
public:
    static constexpr unsigned kRate = 32000;
    bool prepare(const WiiSoundArchive& archive, const std::string& name, bool repeat, std::string& error);
    bool render(std::int16_t* stereo, std::size_t frames);
    bool finished() const;
    const std::string& error() const { return error_; }
    std::size_t sample_bytes() const;

private:
    struct Track {
        std::size_t position = 0;
        std::array<std::size_t, 8> returns{};
        unsigned depth = 0;
        std::size_t loop_position = 0;
        unsigned loop_count = 0;
        std::uint32_t wait = 0;
        unsigned program = 0, volume = 127, pan = 64;
        int transpose = 0, bend = 0;
        unsigned bend_range = 2;
        int attack = -1, decay = -1, sustain = -1, release = -1;
        bool active = false, note_wait = true;
    };
    struct Voice {
        unsigned wave = 0, track = 0;
        double position = 0, step = 1;
        float gain_left = 0, gain_right = 0, envelope = 0;
        float attack = 1, decay = 0, sustain = 1, release = 1;
        std::uint32_t remaining = 0;
        unsigned stage = 0;
        bool active = false, ignore_off = false;
    };
    bool tick();
    bool commands(unsigned track);
    bool note(unsigned track, unsigned key, unsigned velocity, std::uint32_t duration);
    bool fail(const char* why);
    SoundBytes sequence_, bank_data_;
    std::vector<BannerSound> waves_;
    std::array<Track, 16> tracks_{};
    std::array<Voice, 64> voices_{};
    unsigned tempo_ = 120, timebase_ = 48, master_ = 127, archive_volume_ = 127;
    double until_tick_ = 0;
    bool repeat_ = false;
    std::string error_;
};

}  // namespace riftwii
