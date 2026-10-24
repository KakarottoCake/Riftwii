// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/wiisound.hpp"

#include <algorithm>
#include <cstring>

#include "riftwii/checkedalloc.hpp"
#include "wiisound_read.hpp"

namespace riftwii {
using namespace sound_detail;

bool SoundBytes::contains(std::size_t offset, std::size_t length) const {
    return data != nullptr && offset <= size && length <= size - offset;
}

SoundBytes SoundBytes::slice(std::size_t offset, std::size_t length) const {
    if (!contains(offset, length)) return {};
    return {data + offset, length};
}

bool list_sound_u8(SoundBytes archive, std::vector<SoundResource>& files, std::string& error) {
    files.clear();
    if (!archive.contains(0, 32) || word(archive.data) != 0x55aa382d)
        return reject(error, "the content is not a U8 archive");
    const std::size_t root = word(archive.data + 4);
    const std::size_t header_size = word(archive.data + 8);
    const std::size_t data_start = word(archive.data + 12);
    if (root < 32 || !archive.contains(root, header_size) || header_size < 12 || archive.data[root] != 1 ||
        data_start < root + header_size || data_start > archive.size)
        return reject(error, "the U8 root or header is outside the content");
    const std::size_t count = word(archive.data + root + 8);
    if (count == 0 || count > 4096 || count > header_size / 12)
        return reject(error, "the U8 node count is invalid");
    const SoundBytes names = archive.slice(root + count * 12, header_size - count * 12);
    struct Directory { std::size_t node, end; std::string path; };
    std::array<Directory, 32> parents{};
    parents[0] = {0, count, ""};
    unsigned depth = 1;
    std::size_t path_bytes = 0;
    if (!try_reserve(files, count)) return reject(error, "no memory for the U8 directory");
    for (std::size_t i = 1; i < count; ++i) {
        while (depth > 1 && i == parents[depth - 1].end) --depth;
        const std::uint8_t* node = archive.data + root + i * 12;
        const std::size_t name_at = word(node) & 0xffffff;
        if (!names.contains(name_at, 1)) return reject(error, "a U8 name is outside its string table");
        const char* name = reinterpret_cast<const char*>(names.data + name_at);
        const char* end = static_cast<const char*>(std::memchr(name, 0, std::min<std::size_t>(256, names.size - name_at)));
        if (!end || end == name) return reject(error, "a U8 name is missing or too long");
        const std::string component(name, end);
        if (component == "." || component == ".." || component.find_first_of("/\\:") != std::string::npos)
            return reject(error, "a U8 name is not a path component");
        const std::size_t path_size = parents[depth - 1].path.size() + component.size();
        if (path_size > 256 * 1024 - path_bytes)
            return reject(error, "the U8 paths exceed the directory memory budget");
        path_bytes += path_size;
        const std::string path = parents[depth - 1].path + component;
        if (node[0] == 1) {
            const std::size_t last = word(node + 8);
            if (depth == parents.size() || word(node + 4) != parents[depth - 1].node ||
                last <= i || last > parents[depth - 1].end)
                return reject(error, "the U8 directory nesting is invalid");
            parents[depth++] = {i, last, path + "/"};
        } else if (node[0] == 0) {
            const std::size_t offset = word(node + 4), length = word(node + 8);
            if (offset < data_start || !archive.contains(offset, length))
                return reject(error, "a U8 file is outside the content");
            files.push_back({path, archive.slice(offset, length)});
        } else {
            return reject(error, "a U8 node has an unknown type");
        }
    }
    return true;
}

bool WiiSoundArchive::load(SoundBytes bytes, std::string& error) {
    bytes_ = symbols_ = info_ = files_ = {};
    if (!header(bytes, "RSAR", error) || half(bytes.data + 12) != 64 || half(bytes.data + 14) != 3 ||
        half(bytes.data + 6) < 0x101 || half(bytes.data + 6) > 0x104)
        return reject(error, "the BRSAR header or version is unsupported");
    SoundBytes symbols, info, files;
    if (!section(bytes, 16, "SYMB", symbols, error) || !section(bytes, 24, "INFO", info, error) ||
        !section(bytes, 32, "FILE", files, error)) return false;
    bytes_ = bytes;
    symbols_ = symbols.slice(8, symbols.size - 8);
    info_ = info.slice(8, info.size - 8);
    files_ = files;
    std::vector<ArchiveSound> validate;
    return sounds(validate, error);
}

bool WiiSoundArchive::sounds(std::vector<ArchiveSound>& out, std::string& error) const {
    out.clear();
    if (!symbols_.contains(0, 20)) return reject(error, "the BRSAR symbol header is truncated");
    const std::size_t name_table = word(symbols_.data);
    if (!symbols_.contains(name_table, 4)) return reject(error, "the BRSAR string table is missing");
    const std::size_t names = word(symbols_.data + name_table);
    if (names > 4096 || !symbols_.contains(name_table + 4, names * 4))
        return reject(error, "the BRSAR string count is invalid");
    SoundBytes sound_table;
    if (!table(info_, 0, sound_table)) return reject(error, "the BRSAR sound table is damaged");
    if (!try_reserve(out, word(sound_table.data))) return reject(error, "no memory for the BRSAR sound names");
    for (std::uint32_t i = 0; i < word(sound_table.data); ++i) {
        SoundBytes sound, detail;
        if (!item(info_, sound_table, i, 32, sound) || !reference(info_,
                static_cast<std::size_t>(sound.data - info_.data) + 24, 12, detail))
            return reject(error, "a BRSAR sound reference is damaged");
        const std::size_t name_id = word(sound.data);
        if (name_id >= names) return reject(error, "a BRSAR sound name index is invalid");
        const std::size_t name_at = word(symbols_.data + name_table + 4 + name_id * 4);
        if (!symbols_.contains(name_at, 1)) return reject(error, "a BRSAR sound name is outside SYMB");
        const char* name = reinterpret_cast<const char*>(symbols_.data + name_at);
        const char* end = static_cast<const char*>(std::memchr(name, 0, std::min<std::size_t>(256, symbols_.size - name_at)));
        if (!end || end == name) return reject(error, "a BRSAR sound name is missing or too long");
        out.push_back({std::string(name, end), sound.data[22], sound.data[20], word(sound.data + 4),
                       static_cast<std::uint32_t>(detail.data - info_.data)});
    }
    return true;
}

bool WiiSoundArchive::find(const std::string& name, ArchiveSound& out, std::string& error) const {
    std::vector<ArchiveSound> entries;
    if (!sounds(entries, error)) return false;
    for (const ArchiveSound& sound : entries) {
        if (sound.name == name) {
            out = sound;
            return true;
        }
    }
    return reject(error, "the System Menu has no sound with that name");
}

bool WiiSoundArchive::file(std::uint32_t id, SoundBytes& header_out, SoundBytes& audio,
                           std::string& error) const {
    SoundBytes file_table, file_info, positions, position, groups, group, items, entry;
    if (!table(info_, 24, file_table) || !item(info_, file_table, id, 28, file_info) ||
        !table(info_, static_cast<std::size_t>(file_info.data - info_.data) + 20, positions) ||
        !item(info_, positions, 0, 8, position) || !table(info_, 32, groups) ||
        !item(info_, groups, word(position.data), 40, group) ||
        !table(info_, static_cast<std::size_t>(group.data - info_.data) + 32, items) ||
        !item(info_, items, word(position.data + 4), 24, entry) || word(entry.data) != id)
        return reject(error, "a BRSAR file or group reference is damaged or external");
    const auto group_slice = [&](unsigned group_field, unsigned entry_field, SoundBytes& out) {
        const std::size_t group_at = word(group.data + group_field);
        const std::size_t group_size = word(group.data + group_field + 4);
        const std::size_t offset = word(entry.data + entry_field);
        const std::size_t length = word(entry.data + entry_field + 4);
        if (group_at < static_cast<std::size_t>(files_.data - bytes_.data) ||
            !bytes_.contains(group_at, group_size) || offset > group_size || length > group_size - offset ||
            !files_.contains(group_at - static_cast<std::size_t>(files_.data - bytes_.data), group_size)) return false;
        out = bytes_.slice(group_at + offset, length);
        return true;
    };
    if (!group_slice(16, 4, header_out) || !group_slice(24, 12, audio) ||
        header_out.size != word(file_info.data) || audio.size != word(file_info.data + 4))
        return reject(error, "a BRSAR file extent is outside FILE or disagrees with its length");
    return true;
}

bool WiiSoundArchive::bank(std::uint32_t id, SoundBytes& header_out, SoundBytes& audio,
                           std::string& error) const {
    SoundBytes banks, entry;
    if (!table(info_, 8, banks) || !item(info_, banks, id, 12, entry))
        return reject(error, "the BRSAR bank index is invalid");
    return file(word(entry.data + 4), header_out, audio, error);
}

bool menu_wave_at(SoundBytes file, SoundBytes audio, std::uint32_t index,
                  SoundBytes& metadata, SoundBytes& samples, std::string& error) {
    if (!file.contains(0, 32)) return reject(error, "the wave container is truncated");
    const bool bank_file = std::memcmp(file.data, "RBNK", 4) == 0;
    if (!header(file, bank_file ? "RBNK" : "RWSD", error)) return false;
    SoundBytes wave;
    if (!section(file, 24, "WAVE", wave, error)) {
        // RWSD 1.3+ puts its waves in the separate RWAR audio group.
        SoundBytes rwar = audio, list, data;
        if (!header(rwar, "RWAR", error) || !section(rwar, 16, "TABL", list, error) ||
            !section(rwar, 24, "DATA", data, error) || !list.contains(8, 4)) return false;
        const std::size_t count = word(list.data + 8);
        if (count > 4096 || index >= count || !list.contains(12, count * 12))
            return reject(error, "the RWAR wave count or index is invalid");
        const std::uint8_t* entry = list.data + 12 + index * 12;
        if (entry[0] != 1) return reject(error, "the RWAR wave uses a console address");
        SoundBytes rwav = data.slice(word(entry + 4), word(entry + 8));
        SoundBytes info;
        if (!header(rwav, "RWAV", error) || !section(rwav, 16, "INFO", info, error) ||
            !section(rwav, 24, "DATA", samples, error)) return false;
        metadata = info.slice(8, info.size - 8);
        samples = samples.slice(8, samples.size - 8);
        return true;
    }
    if (!wave.contains(8, 4)) return reject(error, "the WAVE table is truncated");
    const std::size_t count = word(wave.data + 8);
    const bool references = bank_file || half(file.data + 6) < 0x102;
    const std::size_t stride = references ? 8 : 4;
    if (count > 4096 || index >= count || !wave.contains(12, count * stride))
        return reject(error, "the WAVE count or index is invalid");
    std::size_t offset = 0;
    if (references) {
        SoundBytes body = wave.slice(8, wave.size - 8);
        if (!reference(body, 4 + index * 8, 28, metadata))
            return reject(error, "a WAVE reference is invalid");
    } else {
        offset = word(wave.data + 12 + index * 4);
        if (!wave.contains(offset, 28)) return reject(error, "a WAVE offset is invalid");
        metadata = wave.slice(offset, wave.size - offset);
    }
    samples = audio;
    return true;
}

bool decode_menu_wave(SoundBytes metadata, SoundBytes audio, BannerSound& out,
                      std::string& error, std::size_t max_frames) {
    out = {};
    if (!metadata.contains(0, 28)) return reject(error, "the wave metadata is truncated");
    const std::uint8_t* m = metadata.data;
    const unsigned encoding = m[0], channels = m[2];
    const unsigned rate = (unsigned(m[3]) << 16) | half(m + 4);
    if (encoding > 2 || channels == 0 || channels > 2 || rate < 4000 || rate > 48000 || m[6] != 0)
        return reject(error, "the wave encoding, channels, rate or address type is unsupported");
    const auto sample_index = [encoding](std::uint32_t value) -> std::size_t {
        if (encoding != 2) return value;
        return (std::size_t(value) / 16) * 14 + (value % 16 > 2 ? value % 16 - 2 : 0);
    };
    const std::size_t frames = sample_index(word(m + 12));
    const std::size_t loop_at = sample_index(word(m + 8));
    if (frames == 0 || frames > max_frames || max_frames > 48000 * 10 || (m[1] && loop_at >= frames))
        return reject(error, "the wave length or loop is outside its sound budget");
    const std::size_t channel_table = word(m + 16);
    if (!metadata.contains(channel_table, channels * 4)) return reject(error, "the wave channel table is truncated");
    if (!try_assign(out.pcm, frames * 2, std::int16_t(0))) return reject(error, "no memory for decoded wave samples");
    for (unsigned channel = 0; channel < channels; ++channel) {
        const std::size_t channel_at = word(m + channel_table + channel * 4);
        if (!metadata.contains(channel_at, 8)) return reject(error, "a wave channel is outside its metadata");
        const std::uint8_t* c = m + channel_at;
        const std::size_t base = word(m + 20), offset = word(c);
        const std::size_t encoded_bytes = encoding == 2 ? ((frames + 13) / 14) * 8 : frames * (encoding + 1);
        if (base > audio.size || !audio.contains(base, offset) || !audio.contains(base + offset, encoded_bytes))
            return reject(error, "wave samples are outside the audio group");
        const std::uint8_t* samples = audio.data + base + offset;
        if (encoding == 2) {
            const std::size_t adpcm = word(c + 4);
            if (!metadata.contains(adpcm, 40)) return reject(error, "the ADPCM coefficients are truncated");
            std::int16_t coefficients[16];
            for (unsigned i = 0; i < 16; ++i) coefficients[i] = static_cast<std::int16_t>(half(m + adpcm + i * 2));
            // Reject predictors outside the eight documented coefficient pairs.
            for (std::size_t i = 0; i < encoded_bytes; i += 8)
                if ((samples[i] >> 4) > 7) return reject(error, "an ADPCM predictor is invalid");
            decode_dsp_adpcm(samples, encoded_bytes, frames, coefficients,
                             static_cast<std::int16_t>(half(m + adpcm + 36)),
                             static_cast<std::int16_t>(half(m + adpcm + 38)), out.pcm.data() + channel, 2);
        } else {
            for (std::size_t i = 0; i < frames; ++i) {
                out.pcm[i * 2 + channel] = encoding == 0
                    ? static_cast<std::int16_t>(static_cast<std::int8_t>(samples[i]) * 256)
                    : static_cast<std::int16_t>(half(samples + i * 2));
            }
        }
    }
    if (channels == 1) {
        for (std::size_t i = 0; i < frames; ++i) out.pcm[i * 2 + 1] = out.pcm[i * 2];
    }
    out.rate = rate;
    out.loop = m[1] != 0;
    out.loop_start = loop_at;
    return true;
}

bool decode_menu_rwav(SoundBytes file, BannerSound& out, std::string& error, std::size_t max_frames) {
    SoundBytes info, audio;
    if (!header(file, "RWAV", error) || !section(file, 16, "INFO", info, error) ||
        !section(file, 24, "DATA", audio, error)) return false;
    return decode_menu_wave(info.slice(8, info.size - 8), audio.slice(8, audio.size - 8), out, error, max_frames);
}

}  // namespace riftwii
