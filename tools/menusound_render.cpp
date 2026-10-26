// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Local listening aid. Keep console resources and every rendered WAV outside
// the source tree; regression tests use synthetic banks instead.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "riftwii/checkedalloc.hpp"
#include "riftwii/wiisound.hpp"

namespace {

riftwii::SoundBytes find_archive(riftwii::SoundBytes bytes, std::size_t& entries_left, unsigned depth = 0) {
    if (bytes.contains(0, 4) && std::memcmp(bytes.data, "RSAR", 4) == 0) return bytes;
    if (depth == 4) return {};
    std::vector<riftwii::SoundResource> files;
    std::string error;
    if (!riftwii::list_sound_u8(bytes, files, error)) return {};
    for (const auto& file : files) {
        if (entries_left == 0) return {};
        --entries_left;
        const auto found = find_archive(file.bytes, entries_left, depth + 1);
        if (found.data) return found;
    }
    return {};
}

bool write_little(FILE* file, std::uint32_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) {
        if (std::fputc((value >> (i * 8)) & 255, file) == EOF) return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(stderr, "Usage: menusound_render content.app sound_name output.wav seconds\n"
                             "Keep console files and rendered audio outside the repository.\n");
        return 2;
    }
    char* end = nullptr;
    const unsigned long seconds = std::strtoul(argv[4], &end, 10);
    if (*end || seconds < 1 || seconds > 180) return 2;
    FILE* input = std::fopen(argv[1], "rb");
    if (!input) return 1;
    std::fseek(input, 0, SEEK_END);
    const long size = std::ftell(input);
    std::rewind(input);
    std::vector<std::uint8_t> bytes;
    if (size < 1 || size > 16 * 1024 * 1024 ||
        !riftwii::try_resize(bytes, static_cast<std::size_t>(size))) {
        std::fclose(input);
        return 1;
    }
    const bool read = std::fread(bytes.data(), 1, bytes.size(), input) == bytes.size();
    std::fclose(input);
    riftwii::WiiSoundArchive archive;
    riftwii::WiiSoundPlayer player;
    std::string error;
    std::size_t entries_left = 10000;
    if (!read || !archive.load(find_archive({bytes.data(), bytes.size()}, entries_left), error) ||
        !player.prepare(archive, argv[2], true, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    FILE* output = std::fopen(argv[3], "wb");
    if (!output) return 1;
    const std::size_t frames = seconds * player.kRate;
    const std::uint32_t data_bytes = static_cast<std::uint32_t>(frames * 4);
    bool written = std::fwrite("RIFF", 1, 4, output) == 4 && write_little(output, 36 + data_bytes, 4) &&
        std::fwrite("WAVEfmt ", 1, 8, output) == 8 && write_little(output, 16, 4) &&
        write_little(output, 1, 2) && write_little(output, 2, 2) &&
        write_little(output, player.kRate, 4) && write_little(output, player.kRate * 4, 4) &&
        write_little(output, 4, 2) && write_little(output, 16, 2) &&
        std::fwrite("data", 1, 4, output) == 4 && write_little(output, data_bytes, 4);
    std::array<std::int16_t, 2048> pcm{};
    double render_seconds = 0;
    for (std::size_t position = 0; written && position < frames;) {
        const std::size_t count = std::min(frames - position, pcm.size() / 2);
        const auto start = std::chrono::steady_clock::now();
        if (!player.render(pcm.data(), count)) {
            std::fprintf(stderr, "%s\n", player.error().c_str());
            written = false;
            break;
        }
        render_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        for (std::size_t i = 0; written && i < count * 2; ++i)
            written = write_little(output, static_cast<std::uint16_t>(pcm[i]), 2);
        position += count;
    }
    if (std::fclose(output) != 0) written = false;
    std::printf("Rendered %zu frames at %u Hz in %.3f seconds of rendering.\n", frames, player.kRate, render_seconds);
    return written ? 0 : 1;
}
