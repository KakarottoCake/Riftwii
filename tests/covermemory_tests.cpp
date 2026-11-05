// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/checkedalloc.hpp"
#include "riftwii/coverart.hpp"
#include "riftwii/pngdecode.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

unsigned checks = 0;
unsigned refuse_check = 0;
bool one_large_buffer = false;
unsigned large_checks = 0;
int failures = 0;

void expect(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    ++failures;
}

// Refuse each checked buffer in turn, including the allocations after
// earlier buffers succeeded. This models a fragmented or exhausted heap
// without depending on the host's available memory.
template <typename Make>
void check_failures(Make make) {
    checks = 0;
    expect(!make().empty(), "Art should be made when memory is available");
    const unsigned total_checks = checks;
    expect(total_checks != 0, "Art buffers must ask for memory through checkedalloc");
    for (unsigned check = 1; check <= total_checks; ++check) {
        checks = 0;
        refuse_check = check;
        riftwii::OutOfMemoryScope memory;
        const auto result = make();
        refuse_check = 0;
        expect(result.empty(), "A refused buffer must stop art conversion");
        expect(memory.failed(), "A refused art buffer must report the memory shortage");
    }
}

void append_word(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void append_chunk(std::vector<std::uint8_t>& png, const char* type, const std::vector<std::uint8_t>& data) {
    append_word(png, static_cast<std::uint32_t>(data.size()));
    const std::size_t start = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    append_word(png, riftwii::png_crc32(png.data() + start, data.size() + 4));
}

unsigned png_channels = 0;

bool inflate_cover(const std::uint8_t*, std::size_t, std::uint8_t* out, std::size_t size) {
    const std::size_t row_bytes = 1024 * png_channels + 1;
    if (size != row_bytes * 680) return false;
    std::fill(out, out + size, 127);
    for (unsigned y = 0; y < 680; ++y) {
        out[y * row_bytes] = 0;
    }
    return true;
}

void check_png_memory(unsigned type) {
    // A full cover's dimensions, without needing a real downloaded image.
    // The inflater supplies valid unfiltered rows; the ordinary PNG tests
    // separately check the pixels for every filter, gamma and transparency.
    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> header;
    append_word(header, 1024);
    append_word(header, 680);
    header.insert(header.end(), {8, static_cast<std::uint8_t>(type), 0, 0, 0});
    append_chunk(png, "IHDR", header);
    append_chunk(png, "IDAT", {1});
    append_chunk(png, "IEND", {});
    std::vector<std::uint8_t> pixels;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string error;
    png_channels = type == 2 ? 3 : 4;
    large_checks = 0;
    one_large_buffer = true;
    riftwii::OutOfMemoryScope memory;
    const bool decoded = riftwii::decode_png(png.data(), png.size(), inflate_cover, 1024,
                                             pixels, width, height, error);
    one_large_buffer = false;
    expect(decoded, "A full cover must decode with room for only one large pixel buffer");
    expect(!memory.failed(), "Decoding must not ask for a second large pixel buffer");
    expect(large_checks == 1, "PNG rows and RGBA pixels must share their large allocation");
    expect(pixels.size() == 1024 * 680 * 4, "The decoded cover must be whole");
}

}  // namespace

extern "C" void* __real_malloc(std::size_t bytes);
extern "C" void* __wrap_malloc(std::size_t bytes) {
    ++checks;
    if (refuse_check != 0 && checks == refuse_check) return nullptr;
    if (one_large_buffer && bytes >= 512 * 1024 && ++large_checks > 1) return nullptr;
    return __real_malloc(bytes);
}

int main() {
    using namespace riftwii;
    check_png_memory(2);
    check_png_memory(6);
    const std::vector<std::uint8_t> rgba(512 * 340 * 4, 255);
    check_failures([&] { return make_cover_file(rgba.data(), 512, 340); });
    check_failures([&] { return make_box_file(rgba.data(), 512, 340); });
    check_failures([&] { return make_cover3d_file(rgba.data(), 512, 340); });
    check_failures([&] {
        int width = 0;
        int height = 0;
        return disc_art_texture(rgba.data(), 512, 340, width, height);
    });

    const auto box = make_box_file(rgba.data(), 512, 340);
    // Captured from the old strip-and-assembly implementation, including
    // fractional source pixels and alpha: direct tiling keeps its output.
    std::vector<std::uint8_t> patterned(rgba.size());
    for (std::size_t i = 0; i < patterned.size(); ++i) {
        patterned[i] = static_cast<std::uint8_t>((i * 71 + i / 127) % 256);
    }
    const auto patterned_box = make_box_file(patterned.data(), 512, 340);
    std::uint32_t hash = 2166136261u;
    for (std::uint8_t byte : patterned_box) {
        hash = (hash ^ byte) * 16777619u;
    }
    expect(hash == 0xA94FB7E7u, "Direct box tiling must keep the previous pixel output");
    std::array<std::uint8_t, kBackdropBytes> backdrop;
    checks = 0;
    make_backdrop(box.data() + kBoxHeaderSize, kBoxWidth, kBoxHeight, kBoxSpineWidth,
                  kBoxSpineWidth + kBoxFrontWidth, backdrop.data());
    const unsigned backdrop_checks = checks;
    expect(backdrop_checks != 0, "Backdrop buffers must ask for memory through checkedalloc");
    for (unsigned check = 1; check <= backdrop_checks; ++check) {
        backdrop.fill(0xEE);
        checks = 0;
        refuse_check = check;
        OutOfMemoryScope memory;
        make_backdrop(box.data() + kBoxHeaderSize, kBoxWidth, kBoxHeight, kBoxSpineWidth,
                      kBoxSpineWidth + kBoxFrontWidth, backdrop.data());
        refuse_check = 0;
        expect(memory.failed(), "A refused backdrop buffer must report the memory shortage");
        expect(std::all_of(backdrop.begin(), backdrop.end(), [](std::uint8_t byte) { return byte == 0; }),
               "A refused backdrop must leave transparent pixels for the plain band");
    }

    expect(scale_rgba(rgba.data(), 512, 340, -1, 340).empty(), "Negative output sizes must be rejected");
    expect(scale_rgba(rgba.data(), 512, 340, 0x7FFFFFFF, 340).empty(), "Oversized output sizes must be rejected");
    if (failures != 0) return 1;
    std::cout << "Art conversion handles every refused buffer.\n";
    return 0;
}
