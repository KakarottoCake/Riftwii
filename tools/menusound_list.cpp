// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "riftwii/checkedalloc.hpp"
#include "riftwii/wiisound.hpp"

namespace {

bool inspect(riftwii::SoundBytes bytes, const std::string& path, const char* selected, unsigned depth,
             std::size_t& entries_left, bool& recognized, bool& rendered) {
    if (depth > 4) return true;
    std::string error;
    if (bytes.contains(0, 4) && std::memcmp(bytes.data, "RSAR", 4) == 0) {
        riftwii::WiiSoundArchive archive;
        std::vector<riftwii::ArchiveSound> sounds;
        if (!archive.load(bytes, error) || !archive.sounds(sounds, error)) {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
            return false;
        }
        recognized = true;
        bool selected_found = false;
        for (const auto& sound : sounds) {
            if (selected && sound.name == selected) selected_found = true;
            if (entries_left == 0) {
                std::fprintf(stderr, "Listing exceeds 10000 resources and sound names.\n");
                return false;
            }
            --entries_left;
            riftwii::SoundBytes file, audio;
            if (!archive.file(sound.file_id, file, audio, error)) {
                std::fprintf(stderr, "%s: %s\n", sound.name.c_str(), error.c_str());
                return false;
            }
            std::printf("  %s type=%u volume=%u file=%u %.4s header=%zu audio=%zu\n",
                        sound.name.c_str(), sound.type, sound.volume, sound.file_id,
                        file.size >= 4 ? reinterpret_cast<const char*>(file.data) : "????", file.size, audio.size);
        }
        // A U8 may hold several RSARs; keep looking until the name matches.
        if (selected_found && !rendered) {
            riftwii::WiiSoundPlayer player;
            if (!player.prepare(archive, selected, true, error)) {
                std::fprintf(stderr, "%s: %s\n", selected, error.c_str());
                return false;
            }
            std::array<std::int16_t, 2048> pcm{};
            int peak = 0;
            std::size_t frames = 0;
            // Runs past a complete menu music cycle without ever writing audio.
            while (!player.finished() && frames < player.kRate * 180) {
                if (!player.render(pcm.data(), pcm.size() / 2)) {
                    std::fprintf(stderr, "%s: %s\n", selected, player.error().c_str());
                    return false;
                }
                for (const auto sample : pcm) peak = std::max(peak, std::abs(static_cast<int>(sample)));
                frames += pcm.size() / 2;
            }
            std::printf("Rendered %s: %zu frames, %zu sample bytes, peak %d; no audio written.\n",
                        selected, frames, player.sample_bytes(), peak);
            rendered = true;
        }
        return true;
    }
    if (!bytes.contains(0, 4) || std::memcmp(bytes.data, "\x55\xaa\x38\x2d", 4) != 0) return true;
    std::vector<riftwii::SoundResource> files;
    if (!riftwii::list_sound_u8(bytes, files, error)) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
        return false;
    }
    recognized = true;
    for (const auto& file : files) {
        if (entries_left == 0) {
            std::fprintf(stderr, "Nested listing exceeds 10000 resources.\n");
            return false;
        }
        --entries_left;
        const std::string name = path + "/" + file.name;
        std::printf("%s: %zu bytes, magic %02x %02x %02x %02x\n", name.c_str(), file.bytes.size,
                    file.bytes.size > 0 ? file.bytes.data[0] : 0, file.bytes.size > 1 ? file.bytes.data[1] : 0,
                    file.bytes.size > 2 ? file.bytes.data[2] : 0, file.bytes.size > 3 ? file.bytes.data[3] : 0);
        if (!inspect(file.bytes, name, selected, depth + 1, entries_left, recognized, rendered)) return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::fprintf(stderr, "Usage: menusound_list content.app [sound_name_to_check]\nLists only; never extracts audio.\n");
        return 2;
    }
    FILE* file = std::fopen(argv[1], "rb");
    if (!file) {
        std::perror(argv[1]);
        return 1;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::rewind(file);
    std::vector<std::uint8_t> bytes;
    if (size <= 0 || size > 16 * 1024 * 1024 || !riftwii::try_resize(bytes, static_cast<std::size_t>(size))) {
        std::fclose(file);
        std::fprintf(stderr, "Content must be between one byte and 16 MiB.\n");
        return 1;
    }
    const bool read = std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    std::fclose(file);
    std::size_t entries_left = 10000;
    bool recognized = false;
    bool rendered = false;
    if (!read) {
        std::fprintf(stderr, "%s: could not read the complete file.\n", argv[1]);
        return 1;
    }
    if (!inspect({bytes.data(), bytes.size()}, argv[1], argc == 3 ? argv[2] : nullptr,
                 0, entries_left, recognized, rendered)) return 1;
    if (!recognized) {
        std::fprintf(stderr, "%s: no supported RSAR or U8 archive was recognized.\n", argv[1]);
        return 1;
    }
    if (argc == 3 && !rendered) {
        std::fprintf(stderr, "%s: the sound was not found and rendered.\n", argv[2]);
        return 1;
    }
    return 0;
}
