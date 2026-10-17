/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A GameCube game's crash screen: white text on a blue frame, a large
 * ":(" at the top left, the menu's message under it and a smaller details
 * line (the game, the exception and where, RiftWii's version).
 *
 * The menu draws everything in its own font and language before the
 * launch, so the blob carries no font. The face and the message go into
 * the card; the details line is made at the crash from pieces the menu
 * drew into a strip next to the card (hex digits, the exception names and
 * the line's fixed words), as only the crash knows which exception it was
 * and where.
 *
 * Shared by the blob (gc_rt.c), the loader (wii/launch/gcnative.cpp, the
 * menu's MakeGcCrashCard) and the host tests, so the functions here are
 * plain arithmetic: no tables (the blob has no .rodata) and no pointers
 * kept anywhere. */
#ifndef RIFTWII_GC_CRASH_H
#define RIFTWII_GC_CRASH_H

#include <stdint.h>

/* gc_on_fatal's exception number for one caught by the loader's own
 * vectors, before the game's OS replaced them: 0x10 plus the vector's
 * number (0x13 DSI at 0x300, 0x14 ISI, 0x17 program...), where the OS's
 * own numbers are one less (2 DSI). */
#define GC_EXCEPTION_EARLY 0x10u
/* gc_on_fatal's exception number for a jump into RiftWii's own data, caught
 * by the two landing words at its start (gc_card.h): SRR0 is where it
 * landed, LR as the jump left it (the jumper's return address after a
 * bctrl or blrl; the landing keeps it in r0, so r0 itself is lost), and
 * DAR holds CTR (the target after a bctr). */
#define GC_EXCEPTION_STRAY 0x100u

/* The card: 2 bits of coverage a pixel, four pixels a byte, the leftmost
 * in the top bits, centred on the frame. Bands from the top: the face,
 * the message (four lines of the menu font at 24 pixels) and the details
 * line, which the blob fills from the strip at the crash. */
#define GC_CRASH_CARD_W 576u
#define GC_CRASH_FACE_H 112u
#define GC_CRASH_MESSAGE_Y 120u
#define GC_CRASH_MESSAGE_H 128u
#define GC_CRASH_DETAILS_Y 264u
#define GC_CRASH_STRIP_H 24u
#define GC_CRASH_CARD_H (GC_CRASH_DETAILS_Y + GC_CRASH_STRIP_H)
#define GC_CRASH_CARD_BYTES (GC_CRASH_CARD_W * GC_CRASH_CARD_H / 4u)

/* The strip's pieces: the hex digits 0-F, then "x", then one name for each
 * exception the hook can see (gc_crash_name_piece), then the details
 * line's own words (the game's ID, the translation's "at", the version). */
#define GC_CRASH_PIECES 32u
#define GC_CRASH_PIECE_X 16u
#define GC_CRASH_PIECE_NAMES 17u
#define GC_CRASH_NAME_COUNT 9u
#define GC_CRASH_PIECE_TEXT (GC_CRASH_PIECE_NAMES + GC_CRASH_NAME_COUNT)
#define GC_CRASH_TEXT_COUNT (GC_CRASH_PIECES - GC_CRASH_PIECE_TEXT)
#define GC_CRASH_NO_PIECE 0xFFu
/* Columns copied past a piece's advance, for glyphs that reach beyond it. */
#define GC_CRASH_PIECE_PAD 3u

/* The details line's order: pieces, or these. */
#define GC_CRASH_ORDER 16u
#define GC_CRASH_ITEM_NAME 0xF0u     /* the exception's name (its number in hex if it has none) */
#define GC_CRASH_ITEM_ADDRESS 0xF1u  /* SRR0 as 0x and eight digits */
#define GC_CRASH_ITEM_END 0xFFu

#define GC_CRASH_STRIP_W 2048u
#define GC_CRASH_STRIP_BYTES (GC_CRASH_STRIP_W * GC_CRASH_STRIP_H / 4u)

/* Right after the card in MEM2 (loader-placed): where each piece is in the
 * strip, how far it moves the pen, and the details line's order. */
struct gc_crash_strip {
    uint16_t at[GC_CRASH_PIECES];
    uint16_t advance[GC_CRASH_PIECES];  /* 0: the piece is missing */
    uint8_t order[GC_CRASH_ORDER];
    uint8_t reserved[16];
    uint8_t coverage[GC_CRASH_STRIP_BYTES];
};
#define GC_CRASH_STRIP_HEADER 160u
#define GC_CRASH_SCREEN_BYTES (GC_CRASH_CARD_BYTES + GC_CRASH_STRIP_HEADER + GC_CRASH_STRIP_BYTES)
typedef char gc_crash_strip_size[(sizeof(struct gc_crash_strip) == GC_CRASH_STRIP_HEADER + GC_CRASH_STRIP_BYTES) ? 1 : -1];

/* The blue: RGB (0, 120, 215) in BT.601's studio range. White text is
 * Y 235 with no colour (Cb and Cr 128). */
#define GC_CRASH_BLUE_Y 98
#define GC_CRASH_BLUE_CB 188
#define GC_CRASH_BLUE_CR 69
#define GC_CRASH_WHITE_Y 235
#define GC_CRASH_WHITE_C 128

/* From `from` towards `to` by coverage (0-255). */
static inline uint32_t gc_crash_mix(int32_t from, int32_t to, uint32_t coverage) {
    return (uint32_t)(from + (to - from) * (int32_t)coverage / 255);
}

/* Two pixels' coverage as one YUYV word (Y0 Cb Y1 Cr): each pixel's own
 * luma, the colour from both, as the pair shares it. */
static inline uint32_t gc_crash_yuyv(uint32_t coverage0, uint32_t coverage1) {
    const uint32_t both = (coverage0 + coverage1) / 2u;
    const uint32_t y0 = gc_crash_mix(GC_CRASH_BLUE_Y, GC_CRASH_WHITE_Y, coverage0);
    const uint32_t y1 = gc_crash_mix(GC_CRASH_BLUE_Y, GC_CRASH_WHITE_Y, coverage1);
    const uint32_t cb = gc_crash_mix(GC_CRASH_BLUE_CB, GC_CRASH_WHITE_C, both);
    const uint32_t cr = gc_crash_mix(GC_CRASH_BLUE_CR, GC_CRASH_WHITE_C, both);
    return (y0 << 24) | (cb << 16) | (y1 << 8) | cr;
}

/* A 2-bit picture's level (0-3) at (x, y); `width` in pixels. */
static inline uint32_t gc_crash_level(const uint8_t* bits, uint32_t width, uint32_t x, uint32_t y) {
    const uint32_t at = y * width + x;
    return (bits[at / 4u] >> (6u - 2u * (at % 4u))) & 3u;
}
/* Raises a pixel to `level`; a darker one never covers a brighter one, so
 * pieces that touch keep both their edges. */
static inline void gc_crash_raise(uint8_t* bits, uint32_t width, uint32_t x, uint32_t y, uint32_t level) {
    const uint32_t at = y * width + x;
    const uint32_t shift = 6u - 2u * (at % 4u);
    const uint32_t old = (bits[at / 4u] >> shift) & 3u;
    if (level <= old) return;
    bits[at / 4u] = (uint8_t)((bits[at / 4u] & ~(3u << shift)) | (level << shift));
}

/* The strip piece that names an exception, GC_CRASH_NO_PIECE when none
 * does. One caught before the game's OS was up (0x10 + the vector's
 * number, 0x12-0x19 one more than the OS's) is named as the OS would name
 * it. The names (gccrash.cpp's
 * gc_crash_piece_text): machine check, DSI, ISI, alignment, program,
 * floating point unavailable, breakpoint (the IABR), memory protection
 * (the SDK's error 15) and a stray jump into RiftWii. */
static inline uint32_t gc_crash_name_piece(uint32_t exception) {
    uint32_t number = exception;
    if (number == GC_EXCEPTION_STRAY) return GC_CRASH_PIECE_NAMES + 8u;
    if (number >= GC_EXCEPTION_EARLY + 2u && number <= GC_EXCEPTION_EARLY + 9u) number -= GC_EXCEPTION_EARLY + 1u;
    else if (number >= GC_EXCEPTION_EARLY) return GC_CRASH_NO_PIECE;
    if (number >= 1u && number <= 3u) return GC_CRASH_PIECE_NAMES + number - 1u;
    if (number >= 5u && number <= 7u) return GC_CRASH_PIECE_NAMES + number - 2u;
    if (number == 12u) return GC_CRASH_PIECE_NAMES + 6u;
    if (number == 15u) return GC_CRASH_PIECE_NAMES + 7u;
    return GC_CRASH_NO_PIECE;
}

/* Copies a piece from the strip into the card's details band at `pen`;
 * returns the pen after it. A missing piece moves nothing. */
static inline uint32_t gc_crash_put_piece(const struct gc_crash_strip* strip, uint8_t* card, uint32_t piece,
                                          uint32_t pen) {
    uint32_t column, row;
    uint32_t advance;
    if (piece >= GC_CRASH_PIECES) return pen;
    advance = strip->advance[piece];
    if (advance == 0) return pen;
    for (column = 0; column < advance + GC_CRASH_PIECE_PAD; ++column) {
        const uint32_t from = strip->at[piece] + column;
        const uint32_t to = pen + column;
        if (from >= GC_CRASH_STRIP_W || to >= GC_CRASH_CARD_W) break;
        for (row = 0; row < GC_CRASH_STRIP_H; ++row) {
            const uint32_t level = gc_crash_level(strip->coverage, GC_CRASH_STRIP_W, from, row);
            if (level != 0) gc_crash_raise(card, GC_CRASH_CARD_W, to, GC_CRASH_DETAILS_Y + row, level);
        }
    }
    return pen + advance;
}

/* "0x" and `value`'s hex digits: all eight, or from its first one that is
 * not zero when `all` is 0. */
static inline uint32_t gc_crash_put_hex(const struct gc_crash_strip* strip, uint8_t* card, uint32_t value,
                                        int all, uint32_t pen) {
    int shift;
    int started = all;
    pen = gc_crash_put_piece(strip, card, 0u, pen);
    pen = gc_crash_put_piece(strip, card, GC_CRASH_PIECE_X, pen);
    for (shift = 28; shift >= 0; shift -= 4) {
        const uint32_t digit = (value >> shift) & 0xFu;
        if (digit != 0) started = 1;
        if (!started && shift != 0) continue;
        pen = gc_crash_put_piece(strip, card, digit, pen);
    }
    return pen;
}

/* The details line into the card, left-aligned like the message; returns
 * its width. */
static inline uint32_t gc_crash_details(const struct gc_crash_strip* strip, uint8_t* card, uint32_t exception,
                                        uint32_t address) {
    uint32_t pen = 0;
    uint32_t i;
    for (i = 0; i < GC_CRASH_ORDER; ++i) {
        const uint32_t item = strip->order[i];
        if (item == GC_CRASH_ITEM_END) break;
        if (item == GC_CRASH_ITEM_NAME) {
            const uint32_t name = gc_crash_name_piece(exception);
            if (name != GC_CRASH_NO_PIECE && strip->advance[name] != 0)
                pen = gc_crash_put_piece(strip, card, name, pen);
            else
                pen = gc_crash_put_hex(strip, card, exception, 0, pen);
        } else if (item == GC_CRASH_ITEM_ADDRESS) {
            pen = gc_crash_put_hex(strip, card, address, 1, pen);
        } else {
            pen = gc_crash_put_piece(strip, card, item, pen);
        }
    }
    return pen;
}

#endif
