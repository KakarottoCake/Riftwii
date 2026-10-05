// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// TPL texture files (banners' arc/timg/*.tpl), from the public format page
// https://mkwiiki.org/wiki/TPL_(File_Format): a header (0x0020AF30, the
// image count, the offset of a table of {image header, palette header}
// offsets), then GX textures as the GPU reads them. The Wii draws them in
// place; tpl_to_rgba decodes one for the host's previews and tests.
namespace riftwii {

enum TplFormat : std::uint32_t {
    kTplI4 = 0, kTplI8 = 1, kTplIA4 = 2, kTplIA8 = 3, kTplRGB565 = 4, kTplRGB5A3 = 5, kTplRGBA8 = 6,
    kTplCI4 = 8, kTplCI8 = 9, kTplCI14x2 = 10, kTplCMPR = 14,
};

struct TplImage {
    std::uint16_t width = 0, height = 0;
    std::uint32_t format = 0;
    std::size_t data_offset = 0, data_size = 0;  // in the file
    std::uint32_t wrap_s = 0, wrap_t = 0;        // 0 clamp, 1 repeat, 2 mirror
    std::uint32_t min_filter = 1, mag_filter = 1;
    bool has_palette = false;
    std::uint32_t palette_format = 0;  // 0 IA8, 1 RGB565, 2 RGB5A3
    std::uint16_t palette_count = 0;
    std::size_t palette_offset = 0;
};

// The bytes a texture takes in GX's tiled layout (whole blocks), or 0 for
// a format this file does not know.
std::size_t gx_texture_size(std::uint32_t format, int width, int height);

bool parse_tpl(const std::uint8_t* data, std::size_t size, std::vector<TplImage>& out, std::string& error);

// One image as RGBA rows (width * height * 4), straight alpha.
bool tpl_to_rgba(const std::uint8_t* file, std::size_t size, const TplImage& image, std::vector<std::uint8_t>& rgba,
                 std::string& error);

}  // namespace riftwii
