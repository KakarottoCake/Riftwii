// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "zstd.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace rtzstd_test {

using Bytes = std::vector<std::uint8_t>;

struct Frame {
    std::string name;
    Bytes compressed;
    Bytes decoded;
    std::string sha256;
};

inline Bytes read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(file), {});
}

inline bool reference_decode(Frame& frame, std::size_t capacity) {
    frame.decoded.resize(capacity);
    const auto result = ZSTD_decompress(frame.decoded.data(), capacity,
                                        frame.compressed.data(), frame.compressed.size());
    if (ZSTD_isError(result)) {
        std::cerr << frame.name << ": reference decoder: " << ZSTD_getErrorName(result) << '\n';
        return false;
    }
    frame.decoded.resize(result);
    return true;
}

inline bool load_frames(const std::filesystem::path& fixtures, std::vector<Frame>& frames) {
    std::ifstream manifest(fixtures / "rtzstd" / "manifest.txt");
    if (!manifest) return false;
    std::string line;
    while (std::getline(manifest, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        Frame frame;
        std::size_t size = 0;
        if (!(fields >> frame.name >> size >> frame.sha256) || size > (2u << 20)) return false;
        frame.compressed = read_file(fixtures / "rtzstd" / frame.name);
        if (frame.compressed.empty() || !reference_decode(frame, size) || frame.decoded.size() != size) return false;
        frames.push_back(std::move(frame));
    }
    // Scan the actual RVZ fixture bytes for self-delimiting Zstandard
    // frames. This includes compressed metadata and packed/unpacked groups,
    // without sharing any RVZ table parsing with the production reader.
    for (const auto& entry : std::filesystem::directory_iterator(fixtures / "rvz")) {
        if (entry.path().extension() != ".rvz") continue;
        const Bytes file = read_file(entry.path());
        for (std::size_t offset = 0; offset + 4 <= file.size(); ++offset) {
            if (file[offset] != 0x28 || file[offset + 1] != 0xB5 ||
                file[offset + 2] != 0x2F || file[offset + 3] != 0xFD) continue;
            const auto bytes = ZSTD_findFrameCompressedSize(file.data() + offset, file.size() - offset);
            if (ZSTD_isError(bytes)) continue;
            Frame frame;
            frame.name = entry.path().filename().string() + ":" + std::to_string(offset);
            frame.compressed.assign(file.begin() + offset, file.begin() + offset + bytes);
            if (!reference_decode(frame, 3u << 20)) return false;
            frames.push_back(std::move(frame));
            offset += bytes - 1;
        }
    }
    return frames.size() > 76;
}

} // namespace rtzstd_test
