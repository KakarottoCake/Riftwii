// SPDX-License-Identifier: GPL-3.0-or-later
// FreeType's sfnt module reads colour bitmap glyphs stored as PNGs
// (CBDT/sbix strikes) through libpng. The menu font has no bitmap strikes
// at all, and the covers read their PNGs with src/pngdecode.cpp, so
// instead of linking libpng (about 85 KiB of MEM1) for that one path,
// png_create_read_struct fails, which FreeType reports as an error for
// such a glyph before it calls anything else; the rest only satisfy the
// linker and are never reached. All weak: a linked libpng replaces them.

#define RIFTWII_NO_PNG(name) \
    __attribute__((weak)) void* name(void) { return 0; }

RIFTWII_NO_PNG(png_create_read_struct)
RIFTWII_NO_PNG(png_create_info_struct)
RIFTWII_NO_PNG(png_destroy_read_struct)
RIFTWII_NO_PNG(png_error)
RIFTWII_NO_PNG(png_get_IHDR)
RIFTWII_NO_PNG(png_get_error_ptr)
RIFTWII_NO_PNG(png_get_io_ptr)
RIFTWII_NO_PNG(png_get_valid)
RIFTWII_NO_PNG(png_read_end)
RIFTWII_NO_PNG(png_read_image)
RIFTWII_NO_PNG(png_read_info)
RIFTWII_NO_PNG(png_read_update_info)
RIFTWII_NO_PNG(png_set_expand_gray_1_2_4_to_8)
RIFTWII_NO_PNG(png_set_filler)
RIFTWII_NO_PNG(png_set_gray_to_rgb)
RIFTWII_NO_PNG(png_set_interlace_handling)
RIFTWII_NO_PNG(png_set_longjmp_fn)
RIFTWII_NO_PNG(png_set_packing)
RIFTWII_NO_PNG(png_set_palette_to_rgb)
RIFTWII_NO_PNG(png_set_read_fn)
RIFTWII_NO_PNG(png_set_read_user_transform_fn)
RIFTWII_NO_PNG(png_set_strip_16)
RIFTWII_NO_PNG(png_set_tRNS_to_alpha)
