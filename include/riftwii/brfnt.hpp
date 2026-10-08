// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2012 giantpune
// SPDX-FileCopyrightText: 2012 Dimok
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The Wii Menu's bitmap font (wbf1.brfna, in a shared content of the NAND:
// a U8 archive also holding wbf2.brfna). It is Rodin DB drawn at 32 pixels
// by hand, and what the Wii Menu itself writes with: sharper and heavier
// than the TrueType collection beside it. Nothing of it ships with
// RiftWii: it is read from the player's Wii (Settings > Menu font).
namespace riftwii {

// SHA-1 of that content.
extern const std::uint8_t kWiiBitmapFontHash[20];

// Where the file named `name` of a U8 archive is (any folder), as an
// offset in the archive and a size. False when the archive is not U8, has
// no such file, or a node points past its end.
bool u8_find_file(const std::uint8_t* data, std::size_t size, const char* name, std::size_t& offset,
                  std::size_t& length);

// Nintendo's Huffman coding with 8-bit symbols (type 0x28; GBATEK's
// "Huffman decompression"): `src` from its 4-byte header on, `out_len`
// bytes out. False when the data is not that or runs out.
bool huff8_decode(const std::uint8_t* src, std::size_t src_len, std::uint8_t* out, std::size_t out_len);

// A BRFNT font (RFNT, or RFNA with Huffman-packed sheets) with I4 sheets.
class BitmapFont {
public:
    // One glyph, scaled: 8-bit coverage, `width` by `rows`; `left` from the
    // pen to its first column, `top` from the baseline up to its first row,
    // `advance` to the next pen position. As FreeType gives them.
    struct Glyph {
        std::vector<std::uint8_t> pixels;
        int width = 0, rows = 0, left = 0, top = 0, advance = 0;
    };

    // `data` (the font file) must outlive this. Unpacked sheets go into
    // `slots` buffers of `slot_bytes` each at `slot_memory` (one sheet each,
    // the least recently used one replaced): no allocation per sheet.
    bool load(const std::uint8_t* data, std::size_t size, std::uint8_t* slot_memory, std::size_t slot_bytes,
              unsigned slots, std::string& error);

    std::size_t characters() const { return cmap_.size(); }
    // The size of one unpacked sheet: what each slot must hold.
    std::size_t sheet_bytes() const { return sheet_bytes_; }

    // `ch` at FreeType pixel size `pixel_size` (a line as wide as the
    // TrueType font's at that size: the font's 32 pixels at 32.5). False
    // when the font has no such character.
    bool render(std::uint32_t ch, int pixel_size, Glyph& out);

    // Reads only the header, for the slots' size: false when not a BRFNT.
    static bool sheet_size(const std::uint8_t* data, std::size_t size, std::size_t& bytes);

private:
    struct Width {
        std::int8_t left = 0;
        std::uint8_t glyph = 0;
        std::int8_t advance = 0;
    };
    struct Slot {
        int sheet = -1;
        unsigned used = 0;
    };
    const std::uint8_t* sheet(unsigned index);
    bool glyph_index(std::uint32_t ch, std::uint16_t& index) const;

    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    bool packed_ = false;
    int cell_w_ = 0, cell_h_ = 0, baseline_ = 0;
    int columns_ = 0, rows_ = 0, sheet_w_ = 0, sheet_h_ = 0;
    std::size_t sheet_bytes_ = 0;
    std::vector<std::size_t> sheet_at_;            // each sheet's data in the file
    std::vector<std::uint32_t> cmap_;              // (character << 16) | glyph, sorted
    std::vector<Width> widths_;                    // by glyph index, from widths_first_
    unsigned widths_first_ = 0;
    std::uint8_t* slot_memory_ = nullptr;
    std::size_t slot_bytes_ = 0;
    std::vector<Slot> slots_;
    unsigned clock_ = 0;
};

}  // namespace riftwii
