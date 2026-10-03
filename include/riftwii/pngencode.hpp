// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// A PNG writer for screenshots: 8-bit RGB, rows handed in one at a time,
// the file streamed out in pieces, so a whole screen never needs a second
// copy in memory. Each row takes the filter (None, Sub, Up or Paeth) whose
// bytes sum smallest, and the rows go through one fixed-Huffman deflate
// block (RFC 1951) with a 32 KiB window and a greedy LZ77 match search:
// larger than zlib's best, far smaller than a BMP, with no zlib needed
// (the host tests have none). Memory: about 400 KiB plus five rows.
namespace riftwii {

class PngWriter {
public:
    // Takes `size` bytes of the file; false stops the writer.
    using Sink = bool (*)(void* user, const std::uint8_t* data, std::size_t size);

    // Writes the signature and the header at once. `work`: work_bytes()
    // of memory for the encoder's tables (8-byte aligned), which the menu
    // keeps in MEM2; nullptr takes them from the heap.
    PngWriter(std::uint32_t width, std::uint32_t height, Sink sink, void* user, void* work = nullptr);
    ~PngWriter();
    PngWriter(const PngWriter&) = delete;
    PngWriter& operator=(const PngWriter&) = delete;

    // `rgb`: width * 3 bytes. False once anything failed, or past the last row.
    bool add_row(const std::uint8_t* rgb);
    // After the last row: the end of the image data and IEND.
    bool finish();

    static std::size_t work_bytes();

private:
    struct Impl;
    Impl* impl_;
    bool own_;
};

// The whole image (`rgb` rows of width * 3 bytes) into `out`.
bool encode_png_rgb(const std::uint8_t* rgb, std::uint32_t width, std::uint32_t height,
                    std::vector<std::uint8_t>& out);

}  // namespace riftwii
