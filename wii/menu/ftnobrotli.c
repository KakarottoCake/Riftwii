// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// FreeType's sfnt module unpacks WOFF2 web fonts through brotli. The menu
// font is a plain TTF (zstd-packed by the build, main.cpp), so instead of
// linking brotli (about 135 KiB of MEM1 with its dictionary) for that one
// path, the call fails, which FreeType reports as a broken WOFF2 font.
// Weak: a linked brotli replaces it.

#include <stddef.h>
#include <stdint.h>

// BROTLI_DECODER_RESULT_ERROR
__attribute__((weak)) int BrotliDecoderDecompress(size_t encoded_size, const uint8_t* encoded, size_t* decoded_size,
                                                  uint8_t* decoded) {
    (void)encoded_size;
    (void)encoded;
    (void)decoded;
    if (decoded_size) *decoded_size = 0;
    return 0;
}
