// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Entirely synthetic resources. No samples or bytes from a console.
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/settingsfile.hpp"
#include "riftwii/wiisound.hpp"

namespace {
using Bytes = std::vector<std::uint8_t>;
using namespace riftwii;
int failures = 0;
#define EXPECT(condition) do { if (!(condition)) { std::cerr << "Failed at " << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

void word(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(at + i) = static_cast<std::uint8_t>(value >> (24 - i * 8));
}
void half(Bytes& bytes, std::size_t at, unsigned value) {
    bytes.at(at) = static_cast<std::uint8_t>(value >> 8);
    bytes.at(at + 1) = static_cast<std::uint8_t>(value);
}
void tag(Bytes& bytes, std::size_t at, const char* value) { std::memcpy(bytes.data() + at, value, 4); }
void reference(Bytes& bytes, std::size_t at, std::size_t offset, unsigned type = 0) {
    bytes.at(at) = 1;
    bytes.at(at + 1) = static_cast<std::uint8_t>(type);
    word(bytes, at + 4, static_cast<std::uint32_t>(offset));
}
SoundBytes view(const Bytes& bytes) { return {bytes.data(), bytes.size()}; }

void file_header(Bytes& file, const char* magic, unsigned version = 0x101) {
    tag(file, 0, magic);
    half(file, 4, 0xfeff);
    half(file, 6, version);
    word(file, 8, static_cast<std::uint32_t>(file.size()));
    half(file, 12, 32);
    half(file, 14, 2);
}
void section(Bytes& file, unsigned slot, std::size_t at, std::size_t size, const char* magic) {
    word(file, slot, static_cast<std::uint32_t>(at));
    word(file, slot + 4, static_cast<std::uint32_t>(size));
    tag(file, at, magic);
    word(file, at + 4, static_cast<std::uint32_t>(size));
}

Bytes wave_metadata(unsigned encoding = 1) {
    Bytes metadata(96);
    metadata[0] = static_cast<std::uint8_t>(encoding);
    metadata[2] = 1;
    half(metadata, 4, 8000);
    word(metadata, 12, encoding == 2 ? 16 : 4);
    word(metadata, 16, 28);
    word(metadata, 28, 32);
    word(metadata, 36, 48);
    return metadata;
}

Bytes rwav(unsigned encoding = 1) {
    Bytes file(160);
    file_header(file, "RWAV");
    section(file, 16, 32, 104, "INFO");
    section(file, 24, 136, 24, "DATA");
    const Bytes metadata = wave_metadata(encoding);
    std::copy(metadata.begin(), metadata.end(), file.begin() + 40);
    if (encoding == 2) {
        // Predictor 0, shift 8, no history: each nibble is exactly +/-256.
        file[144] = 8;
        std::fill(file.begin() + 145, file.begin() + 152, 0x1f);
    } else {
        for (unsigned i = 0; i < 4; ++i) half(file, 144 + i * 2, i % 2 ? 0xfc18 : 1000);
    }
    return file;
}

// One program and one synthetic waveform, addressed through old-style
// WAVE references exactly as an RBNK 1.1 reader expects.
Bytes bank() {
    Bytes file(256);
    file_header(file, "RBNK");
    section(file, 16, 32, 80, "DATA");
    section(file, 24, 112, 144, "WAVE");
    word(file, 40, 1);
    reference(file, 44, 12, 1);
    std::fill(file.begin() + 56, file.begin() + 60, 127);
    file[64] = 60;
    file[65] = 127;
    file[66] = 64;
    word(file, 68, 0x3f800000);
    word(file, 120, 1);
    reference(file, 124, 16);
    const Bytes metadata = wave_metadata();
    std::copy(metadata.begin(), metadata.end(), file.begin() + 136);
    return file;
}

Bytes sequence(const Bytes& commands) {
    Bytes file(64 + commands.size());
    file_header(file, "RSEQ", 0x100);
    section(file, 16, 32, file.size() - 32, "DATA");
    word(file, 40, 12);
    std::copy(commands.begin(), commands.end(), file.begin() + 44);
    return file;
}

Bytes archive(const Bytes& commands = {0x81, 0, 60, 127, 0, 0xff}, const Bytes& instrument_bank = bank()) {
    const Bytes seq = sequence(commands);
    const std::size_t group_start = 640;
    const std::size_t audio_start = group_start + seq.size() + instrument_bank.size();
    Bytes file(audio_start + 8);
    file_header(file, "RSAR");
    half(file, 12, 64);
    half(file, 14, 3);
    section(file, 16, 64, 64, "SYMB");
    word(file, 72, 20);
    word(file, 92, 1);
    word(file, 96, 32);
    std::memcpy(file.data() + 104, "test", 5);
    section(file, 24, 128, 480, "INFO");
    const std::size_t b = 136;
    reference(file, b, 48);
    reference(file, b + 8, 60);
    reference(file, b + 24, 72);
    reference(file, b + 32, 92);
    word(file, b + 48, 1);
    reference(file, b + 52, 104);
    word(file, b + 60, 1);
    reference(file, b + 64, 148);
    word(file, b + 72, 2);
    reference(file, b + 76, 160);
    reference(file, b + 84, 208);
    word(file, b + 92, 1);
    reference(file, b + 96, 256);
    file[b + 124] = 127;
    file[b + 126] = 1;
    reference(file, b + 128, 136, 1);
    word(file, b + 152, 1);  // bank's file ID
    for (unsigned i = 0; i < 2; ++i) {
        const std::size_t at = b + 160 + i * 48;
        word(file, at, static_cast<std::uint32_t>(i ? instrument_bank.size() : seq.size()));
        word(file, at + 4, i ? 8 : 0);
        reference(file, at + 20, at - b + 28);
        word(file, at + 28, 1);
        reference(file, at + 32, at - b + 40);
        word(file, at + 44, i);
    }
    word(file, b + 272, static_cast<std::uint32_t>(group_start));
    word(file, b + 276, static_cast<std::uint32_t>(seq.size() + instrument_bank.size()));
    word(file, b + 280, static_cast<std::uint32_t>(audio_start));
    word(file, b + 284, 8);
    reference(file, b + 288, 296);
    word(file, b + 296, 2);
    reference(file, b + 300, 316);
    reference(file, b + 308, 340);
    word(file, b + 324, static_cast<std::uint32_t>(seq.size()));
    word(file, b + 340, 1);
    word(file, b + 344, static_cast<std::uint32_t>(seq.size()));
    word(file, b + 348, static_cast<std::uint32_t>(instrument_bank.size()));
    word(file, b + 356, 8);
    section(file, 32, 608, file.size() - 608, "FILE");
    std::copy(seq.begin(), seq.end(), file.begin() + group_start);
    std::copy(instrument_bank.begin(), instrument_bank.end(), file.begin() + group_start + seq.size());
    for (unsigned i = 0; i < 4; ++i) half(file, audio_start + i * 2, i % 2 ? 0xfc18 : 1000);
    return file;
}

void test_wave() {
    std::string error;
    BannerSound sound;
    const Bytes file = rwav();
    EXPECT(decode_menu_rwav(view(file), sound, error));
    EXPECT(sound.rate == 8000 && sound.frames() == 4);
    EXPECT(sound.pcm[0] == 1000 && sound.pcm[1] == 1000 && sound.pcm[2] == -1000);
    for (std::size_t size = 0; size < file.size(); ++size)
        EXPECT(!decode_menu_rwav({file.data(), size}, sound, error));
    for (const unsigned at : {8u, 16u, 20u, 24u, 28u, 52u, 56u, 68u, 72u}) {
        Bytes damaged = file;
        word(damaged, at, 0xffffffff);
        EXPECT(!decode_menu_rwav(view(damaged), sound, error));
    }
    Bytes adpcm = rwav(2);
    EXPECT(decode_menu_rwav(view(adpcm), sound, error));
    EXPECT(sound.frames() == 14 && sound.pcm[0] == 256 && sound.pcm[2] == -256);
    adpcm[144] = 0xf0;
    EXPECT(!decode_menu_rwav(view(adpcm), sound, error));
    EXPECT(!decode_menu_rwav(view(file), sound, error, 3));
}

void test_wave_containers() {
    std::string error;
    SoundBytes metadata, samples;
    BannerSound decoded;
    const Bytes audio = {0x7f, 0x80, 0, 1, 0x40, 0xc0, 0, 2};
    Bytes stereo = wave_metadata(0);
    stereo[1] = 1;
    stereo[2] = 2;
    word(stereo, 8, 1);
    word(stereo, 28, 40);
    word(stereo, 32, 48);
    word(stereo, 48, 4);
    EXPECT(decode_menu_wave(view(stereo), view(audio), decoded, error));
    EXPECT(decoded.loop && decoded.loop_start == 1 && decoded.frames() == 4);
    EXPECT(decoded.pcm[0] == 32512 && decoded.pcm[1] == 16384);
    EXPECT(decoded.pcm[2] == -32768 && decoded.pcm[3] == -16384);
    stereo[6] = 1;
    EXPECT(!decode_menu_wave(view(stereo), view(audio), decoded, error));
    stereo[6] = 0;
    word(stereo, 8, 4);
    EXPECT(!decode_menu_wave(view(stereo), view(audio), decoded, error));

    // Both old reference tables and RWSD 1.2's plain WAVE offsets.
    for (const unsigned version : {0x101u, 0x102u}) {
        Bytes container = bank();
        tag(container, 0, "RWSD");
        half(container, 6, version);
        if (version == 0x102) {
            word(container, 124, 16);
            const Bytes wave = wave_metadata();
            std::copy(wave.begin(), wave.end(), container.begin() + 128);
        }
        EXPECT(menu_wave_at(view(container), view(audio), 0, metadata, samples, error));
        EXPECT(decode_menu_wave(metadata, samples, decoded, error));
        EXPECT(!menu_wave_at(view(container), view(audio), 1, metadata, samples, error));
        word(container, 120, 0xffffffff);
        EXPECT(!menu_wave_at(view(container), view(audio), 0, metadata, samples, error));
    }

    // Modern RWSD has no WAVE section; its audio group is a RWAR.
    Bytes container(40);
    file_header(container, "RWSD", 0x103);
    section(container, 16, 32, 8, "DATA");
    const Bytes wave = rwav();
    Bytes wave_archive(64 + wave.size());
    file_header(wave_archive, "RWAR");
    section(wave_archive, 16, 32, 24, "TABL");
    section(wave_archive, 24, 56, 8 + wave.size(), "DATA");
    word(wave_archive, 40, 1);
    reference(wave_archive, 44, 8);
    word(wave_archive, 52, static_cast<std::uint32_t>(wave.size()));
    std::copy(wave.begin(), wave.end(), wave_archive.begin() + 64);
    EXPECT(menu_wave_at(view(container), view(wave_archive), 0, metadata, samples, error));
    EXPECT(decode_menu_wave(metadata, samples, decoded, error));
    EXPECT(decoded.frames() == 4 && decoded.pcm[0] == 1000);
    for (const unsigned field : {8u, 16u, 20u, 24u, 28u, 40u, 48u, 52u}) {
        Bytes damaged = wave_archive;
        word(damaged, field, 0xffffffff);
        EXPECT(!menu_wave_at(view(container), view(damaged), 0, metadata, samples, error));
    }
    for (std::size_t length = 0; length < wave_archive.size(); ++length) {
        EXPECT(!menu_wave_at(view(container), {wave_archive.data(), length}, 0, metadata, samples, error));
    }
}

void test_u8() {
    Bytes u8(96);
    word(u8, 0, 0x55aa382d);
    word(u8, 4, 32);
    word(u8, 8, 48);
    word(u8, 12, 80);
    u8[32] = 1;
    word(u8, 40, 3);
    word(u8, 44, 0x01000001);
    word(u8, 52, 3);
    word(u8, 56, 5);
    word(u8, 60, 80);
    word(u8, 64, 16);
    std::memcpy(u8.data() + 68, "\0snd\0a.wav\0", 11);
    std::vector<SoundResource> entries;
    std::string error;
    EXPECT(list_sound_u8(view(u8), entries, error));
    EXPECT(entries.size() == 1 && entries[0].name == "snd/a.wav" && entries[0].bytes.size == 16);
    for (unsigned at : {4u, 8u, 12u, 40u, 48u, 52u, 56u, 60u, 64u}) {
        Bytes damaged = u8;
        word(damaged, at, 0xffffffff);
        EXPECT(!list_sound_u8(view(damaged), entries, error));
    }
    Bytes damaged = u8;
    std::memcpy(damaged.data() + 73, "../x", 4);
    EXPECT(!list_sound_u8(view(damaged), entries, error));

    // Aliasing the same long name in many nodes must not multiply the
    // directory's in-memory paths beyond its aggregate budget.
    constexpr unsigned count = 1101;
    constexpr unsigned header_size = count * 12 + 257;
    constexpr unsigned data_start = 32 + header_size;
    Bytes many(data_start + 1);
    word(many, 0, 0x55aa382d);
    word(many, 4, 32);
    word(many, 8, header_size);
    word(many, 12, data_start);
    many[32] = 1;
    word(many, 40, count);
    std::fill(many.begin() + 32 + count * 12 + 1, many.begin() + data_start - 1, 'x');
    for (unsigned i = 1; i < count; ++i) {
        word(many, 32 + i * 12, 1);
        word(many, 36 + i * 12, data_start);
        word(many, 40 + i * 12, 1);
    }
    EXPECT(!list_sound_u8(view(many), entries, error));
    EXPECT(error.find("memory budget") != std::string::npos);
}

void test_archive_and_sequence() {
    std::string error;
    const Bytes file = archive();
    WiiSoundArchive parsed;
    EXPECT(parsed.load(view(file), error));
    std::vector<ArchiveSound> names;
    EXPECT(parsed.sounds(names, error));
    EXPECT(names.size() == 1 && names[0].name == "test");
    WiiSoundPlayer player;
    EXPECT(player.prepare(parsed, "test", false, error));
    std::array<std::int16_t, 128> pcm{};
    EXPECT(player.render(pcm.data(), pcm.size() / 2));
    EXPECT(player.finished());
    EXPECT(pcm[0] > 600 && pcm[0] < 800 && pcm[8] < -600);
    EXPECT(!player.prepare(parsed, "absent", false, error));
    for (std::size_t size = 0; size < file.size(); ++size) EXPECT(!parsed.load({file.data(), size}, error));
    for (unsigned at : {8u, 16u, 20u, 24u, 28u, 32u, 36u, 72u, 92u, 96u, 140u, 184u, 188u}) {
        Bytes damaged = file;
        word(damaged, at, 0xffffffff);
        EXPECT(!parsed.load(view(damaged), error));
    }
    // A caller can inspect a structurally valid archive with broken file
    // extents, but preparing a sound must reject those extents.
    Bytes damaged = file;
    word(damaged, 136 + 344, 0xffffffff);
    EXPECT(parsed.load(view(damaged), error));
    EXPECT(!player.prepare(parsed, "test", false, error));
    for (const Bytes& commands : {Bytes{0x89, 0, 0, 0}, Bytes{0x8a, 0, 0, 0}, Bytes{0x81, 0x80},
                                 Bytes{0xfd}, Bytes{0xa1}, Bytes{0xe1, 0, 0}, Bytes{0x88, 16, 0, 0, 0}}) {
        const Bytes bad_sequence = archive(commands);
        EXPECT(parsed.load(view(bad_sequence), error));
        EXPECT(player.prepare(parsed, "test", true, error));
        EXPECT(!player.render(pcm.data(), pcm.size() / 2));
    }
    // A delayed call and return, then a note, exercises both time and stack.
    const Bytes called = archive({0x8a, 0, 0, 10, 60, 127, 0, 0xff, 0xff, 0xff, 0x80, 1, 0xfd});
    EXPECT(parsed.load(view(called), error));
    EXPECT(player.prepare(parsed, "test", false, error));
    std::array<std::int16_t, 2048> delayed{};
    EXPECT(player.render(delayed.data(), delayed.size() / 2));
    EXPECT(delayed[0] == 0 && *std::max_element(delayed.begin(), delayed.end()) > 600);
}

void test_settings() {
    LoaderSettings settings;
    EXPECT(!settings.wii_menu_sounds && !settings.wii_menu_music);
    settings.parse("wii_menu_sounds = on\nwii_menu_music = on\n");
    EXPECT(settings.wii_menu_sounds && settings.wii_menu_music);
    settings.parse("wii_menu_sounds = invalid\nwii_menu_music = invalid\n");
    EXPECT(settings.wii_menu_sounds && settings.wii_menu_music);
    LoaderSettings roundtrip;
    roundtrip.parse(settings.serialize());
    EXPECT(roundtrip.wii_menu_sounds && roundtrip.wii_menu_music);
    roundtrip.parse("wii_menu_sounds = off\nwii_menu_music = off\n");
    EXPECT(!roundtrip.wii_menu_sounds && !roundtrip.wii_menu_music);
}

// A constant, looping wave makes the renderer's amplitude directly
// observable without borrowing a bank or any samples from a console.
Bytes constant_archive(const Bytes& commands, unsigned attack = 127, unsigned decay = 127,
                       unsigned sustain = 127, unsigned release = 127) {
    Bytes file = archive(commands);
    const std::size_t instrument_bank = 640 + sequence(commands).size();
    file[instrument_bank + 56] = static_cast<std::uint8_t>(attack);
    file[instrument_bank + 57] = static_cast<std::uint8_t>(decay);
    file[instrument_bank + 58] = static_cast<std::uint8_t>(sustain);
    file[instrument_bank + 59] = static_cast<std::uint8_t>(release);
    file[instrument_bank + 137] = 1;
    for (unsigned i = 0; i < 4; ++i) half(file, file.size() - 8 + i * 2, 10000);
    return file;
}

std::vector<std::int16_t> render_synthetic(const Bytes& file, std::size_t frames) {
    WiiSoundArchive parsed;
    WiiSoundPlayer player;
    std::string error;
    std::vector<std::int16_t> pcm(frames * 2);
    EXPECT(parsed.load(view(file), error));
    EXPECT(player.prepare(parsed, "test", true, error));
    EXPECT(player.render(pcm.data(), frames));
    return pcm;
}

void test_envelopes() {
    const Bytes note = {60, 127, 0, 0xff};
    const auto decay = render_synthetic(constant_archive(note, 127, 80, 0), 8000);
    // Decay 80 falls about 26 dB/s. At 100 ms a constant note must still
    // have roughly 74% of its peak, rather than having vanished at 59 ms.
    EXPECT(decay[3200 * 2] > 5100 && decay[3200 * 2] < 5400);
    EXPECT(decay[6400 * 2] > 3800 && decay[6400 * 2] < 4000);
    const auto attack = render_synthetic(constant_archive(note, 32), 8000);
    EXPECT(attack[0] == 0);
    EXPECT(attack[3200 * 2] > 3500 && attack[3200 * 2] < 3900);
    EXPECT(attack[6400 * 2] > 6700);
    const auto sustain = render_synthetic(constant_archive(note, 127, 127, 64), 8000);
    EXPECT(sustain[3200 * 2] > 1750 && sustain[3200 * 2] < 1850);
    const auto release = render_synthetic(constant_archive({60, 127, 48, 0xff}, 127, 127, 127, 122), 20000);
    EXPECT(release[15900 * 2] > 7000);
    EXPECT(release[16640 * 2] > 3400 && release[16640 * 2] < 3900);
    EXPECT(release[19900 * 2] < 200);
}

void test_volume_and_pan() {
    const auto full = render_synthetic(constant_archive({60, 127, 0, 0xff}), 1600);
    EXPECT(full[0] == full[1]);
    const auto volume = render_synthetic(constant_archive({0xc1, 64, 60, 127, 0, 0xff}), 1600);
    EXPECT(volume[0] > 1750 && volume[0] < 1850);
    const auto expression = render_synthetic(constant_archive({0xd5, 64, 60, 127, 0, 0xff}), 1600);
    EXPECT(volume == expression);
    const auto master = render_synthetic(constant_archive({0xc2, 64, 60, 127, 0, 0xff}), 1600);
    EXPECT(volume == master);
    Bytes bank_volume = constant_archive({60, 127, 0, 0xff});
    const std::size_t bank_at = 640 + sequence({60, 127, 0, 0xff}).size();
    bank_volume[bank_at + 65] = 64;
    const auto instrument = render_synthetic(bank_volume, 1600);
    EXPECT(instrument[0] > 3500 && instrument[0] < 3600);
    bank_volume[136 + 124] = 64;
    const auto archive_volume = render_synthetic(bank_volume, 1600);
    EXPECT(archive_volume[0] > 1750 && archive_volume[0] < 1850);

    // Controllers affect an already sounding voice, including after the
    // track ends. Initial pan belongs only to subsequently started notes.
    const auto live = render_synthetic(constant_archive({0xc7, 0, 60, 127, 0, 0x80, 1,
        0xc0, 127, 0xc1, 64, 0xff}), 1600);
    EXPECT(live[0] == live[1]);
    EXPECT(live[1000 * 2] == 0);
    EXPECT(live[1000 * 2 + 1] > 2500 && live[1000 * 2 + 1] < 2600);
    const auto left = render_synthetic(constant_archive({0xc0, 1, 60, 127, 0, 0xff}), 16);
    EXPECT(left[0] == 10000 && left[1] == 0);
    const auto initial = render_synthetic(constant_archive({0xdc, 127, 60, 127, 0, 0xff}), 16);
    EXPECT(initial[0] == 0 && initial[1] == 10000);
}

void test_interpolation_and_chunking() {
    const Bytes commands = {60, 127, 0, 0xff};
    Bytes file = constant_archive(commands);
    // Four samples at 8 kHz, played at 32 kHz. Check the fractional
    // samples between frames and across the end-to-loop-start boundary.
    for (unsigned i = 0; i < 4; ++i) half(file, file.size() - 8 + i * 2, i * 1000);
    const auto pcm = render_synthetic(file, 3200);
    EXPECT(pcm[2] > 170 && pcm[2] < 180);
    EXPECT(pcm[12 * 2] > 2100 && pcm[12 * 2] < 2130);
    EXPECT(pcm[14 * 2] > 1050 && pcm[14 * 2] < 1070);
    EXPECT(pcm[16 * 2] == 0);

    const Bytes enveloped = constant_archive(commands, 32, 80, 0);
    const auto whole = render_synthetic(enveloped, 8000);
    WiiSoundArchive parsed;
    WiiSoundPlayer player;
    std::string error;
    EXPECT(parsed.load(view(enveloped), error));
    EXPECT(player.prepare(parsed, "test", true, error));
    std::vector<std::int16_t> split(whole.size());
    for (std::size_t frame = 0; frame < 8000;) {
        const std::size_t count = std::min<std::size_t>(137, 8000 - frame);
        EXPECT(player.render(split.data() + frame * 2, count));
        frame += count;
    }
    EXPECT(whole == split);
}

void test_instrument_regions() {
    const Bytes original = bank();
    Bytes split_bank(512);
    file_header(split_bank, "RBNK");
    section(split_bank, 16, 32, 192, "DATA");
    section(split_bank, 24, 224, 288, "WAVE");
    word(split_bank, 40, 1);
    reference(split_bank, 44, 12, 2);
    split_bank[52] = 2;
    split_bank[53] = 60;
    split_bank[54] = 127;
    reference(split_bank, 56, 64, 1);
    reference(split_bank, 64, 84, 1);
    for (unsigned offset : {104u, 124u})
        std::copy(original.begin() + 52, original.begin() + 72, split_bank.begin() + offset);
    split_bank[118] = 1;
    split_bank[138] = 127;
    word(split_bank, 232, 1);
    reference(split_bank, 236, 16);
    const Bytes metadata = wave_metadata();
    std::copy(metadata.begin(), metadata.end(), split_bank.begin() + 248);
    const auto low_key = render_synthetic(archive({60, 64, 0, 0xff}, split_bank), 16);
    const auto high_key = render_synthetic(archive({61, 64, 0, 0xff}, split_bank), 16);
    EXPECT(low_key[0] > 200 && low_key[1] == 0);
    EXPECT(high_key[0] == 0 && high_key[1] > 200);

    // A single key leads to a velocity split. All references are relative
    // to the DATA body, including this second level of the instrument.
    reference(split_bank, 44, 12, 3);
    split_bank[52] = split_bank[53] = 60;
    reference(split_bank, 56, 32, 2);
    split_bank[72] = 2;
    split_bank[73] = 64;
    split_bank[74] = 127;
    reference(split_bank, 76, 64, 1);
    reference(split_bank, 84, 84, 1);
    const auto low_velocity = render_synthetic(archive({60, 64, 0, 0xff}, split_bank), 16);
    const auto high_velocity = render_synthetic(archive({60, 100, 0, 0xff}, split_bank), 16);
    EXPECT(low_velocity[0] > 200 && low_velocity[1] == 0);
    EXPECT(high_velocity[0] == 0 && high_velocity[1] > 600);
}

}  // namespace

int main() {
    test_wave();
    test_wave_containers();
    test_u8();
    test_archive_and_sequence();
    test_settings();
    test_envelopes();
    test_volume_and_pan();
    test_interpolation_and_chunking();
    test_instrument_regions();
    return failures ? 1 : 0;
}
