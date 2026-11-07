// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>

// Address-independent transcripts let the unchanged suites run against both
// engines. Record actual transfer bytes, statuses and public results; pointers
// and internal state numbers are deliberately absent.
namespace fs_trace {
inline std::FILE* output = nullptr;
inline std::uint64_t transfers = 0;

inline void Open(int argc, char** argv) {
    if (argc < 2) return;
    output = std::fopen(argv[1], "wb");
    if (!output) std::exit(1);
}

inline void Bytes(const void* data, std::size_t size) {
    if (output && std::fwrite(data, 1, size, output) != size) std::exit(1);
}

template <typename T>
void Value(const T& value) {
    Bytes(&value, sizeof(value));
}

inline void Image(const std::uint8_t* data, std::size_t size) {
    if (!output) return;
    // Images can be large; compare all transferred bytes exactly and also
    // fingerprint the entire image to detect writes outside the request.
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    Value(hash);
}

inline void Close() {
    if (!output) return;
    Value(transfers);
    if (std::fclose(output) != 0) std::exit(1);
    output = nullptr;
    std::printf("Compared %llu device transfers.\n", static_cast<unsigned long long>(transfers));
}
}
