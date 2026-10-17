// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A memory card's text (save names, comments) as UTF-8 for the menu. A
// card's header says its encoding: Shift-JIS on a Japanese console's card,
// Windows-1252 otherwise. The bytes are the card's, untrusted: anything
// that is not a character shows as '?', and nothing reads past the string.
#include "riftwii/gccard.hpp"

#include <cstddef>

namespace riftwii {

namespace {

// Shift-JIS rows 0x81-0x84 and 0x88-0x98 (tools/make_sjis_table.py), the
// ones the menu font has glyphs for.
const std::uint16_t kSjisRows[] = {
#include "sjis_table.inc"
};
constexpr unsigned kSjisTrails = 188;

// Windows-1252's 0x80-0x9F, where it differs from Latin-1 (0: unused).
const std::uint16_t kCp1252High[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178,
};

void put_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

bool sjis_lead(unsigned b) { return (b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC); }

// The table's row for a lead byte, or -1 when the table has none.
int sjis_row(unsigned lead) {
    if (lead >= 0x81 && lead <= 0x84) return static_cast<int>(lead - 0x81);
    if (lead >= 0x88 && lead <= 0x98) return static_cast<int>(lead - 0x88 + 4);
    return -1;
}

// A trail byte's place in a row, or -1 when it cannot be a trail byte.
int sjis_column(unsigned trail) {
    if (trail >= 0x40 && trail <= 0x7E) return static_cast<int>(trail - 0x40);
    if (trail >= 0x80 && trail <= 0xFC) return static_cast<int>(trail - 0x80 + 63);
    return -1;
}

}  // namespace

std::string gc_card_text_utf8(const std::string& raw, bool shift_jis) {
    static_assert(sizeof kSjisRows / sizeof kSjisRows[0] == 21 * kSjisTrails, "sjis_table.inc's size");
    std::string out;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const unsigned b = static_cast<unsigned char>(raw[i]);
        if (b < 0x80) {
            // Control bytes would break the row's layout.
            out += b < 0x20 ? '?' : static_cast<char>(b);
            continue;
        }
        if (!shift_jis) {
            const std::uint32_t cp = b < 0xA0 ? kCp1252High[b - 0x80] : b;
            if (cp == 0) out += '?';
            else put_utf8(out, cp);
            continue;
        }
        if (b >= 0xA1 && b <= 0xDF) {  // JIS X 0201's half-width katakana
            put_utf8(out, 0xFF61 + (b - 0xA1));
            continue;
        }
        if (!sjis_lead(b) || i + 1 >= raw.size()) {
            out += '?';
            continue;
        }
        const unsigned trail = static_cast<unsigned char>(raw[i + 1]);
        const int column = sjis_column(trail);
        if (column < 0) {  // not a pair: the next byte is read on its own
            out += '?';
            continue;
        }
        ++i;
        const int row = sjis_row(b);
        const std::uint16_t cp = row < 0 ? 0 : kSjisRows[row * kSjisTrails + column];
        if (cp == 0) out += '?';
        else put_utf8(out, cp);
    }
    return out;
}

}  // namespace riftwii
