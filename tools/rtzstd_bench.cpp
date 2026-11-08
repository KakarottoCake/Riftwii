// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Host timing of RiftWii's in-game Zstandard decoder against the vendored
// reference one, on the same frames (tests/fixtures/rtzstd and the RVZ
// fixtures' frames). Both are built at -Os, as on the Wii.
//
//     rtzstd_bench <tests/fixtures>
#include "rtzstd.h"
#include "../tests/rtzstd_cases.hpp"

#include <array>
#include <chrono>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    std::vector<rtzstd_test::Frame> frames;
    if (!rtzstd_test::load_frames(argv[1], frames)) return 1;
    static std::array<std::uint8_t, RTZSTD_WORKSPACE_BYTES> workspace alignas(8);
    std::size_t total_out = 0;
    for (const auto& f : frames) total_out += f.decoded.size();

    // Repeat each frame until about 64 MB has been decoded in all.
    const int rounds = static_cast<int>(64u * 1024 * 1024 / (total_out ? total_out : 1)) + 1;
    std::vector<std::uint8_t> out(3u << 20);
    auto time_it = [&](auto&& decode) {
        const auto start = std::chrono::steady_clock::now();
        std::size_t bytes = 0;
        for (int r = 0; r < rounds; ++r) {
            for (const auto& f : frames) bytes += decode(f);
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        return bytes / seconds / 1e6;
    };
    const double ours = time_it([&](const rtzstd_test::Frame& f) {
        const std::int32_t n = rtzstd_decode(workspace.data(), out.data(), static_cast<std::uint32_t>(f.decoded.size()),
                                             f.compressed.data(), static_cast<std::uint32_t>(f.compressed.size()));
        return n > 0 ? static_cast<std::size_t>(n) : 0;
    });
    const double reference = time_it([&](const rtzstd_test::Frame& f) {
        const std::size_t n = ZSTD_decompress(out.data(), f.decoded.size(), f.compressed.data(), f.compressed.size());
        return ZSTD_isError(n) ? 0 : n;
    });
    std::printf("%zu frames, %zu bytes each round, %d rounds\n", frames.size(), total_out, rounds);
    std::printf("RiftWii decoder: %.1f MB/s\nreference:       %.1f MB/s\nratio:           %.2fx\n", ours, reference,
                ours / reference);
    return 0;
}
