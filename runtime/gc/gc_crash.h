/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A GameCube game's crash screen: white text on a blue frame, like a
 * PowerPC crash handler's dump. A translated line at the top ("The game
 * stopped."), the dump under it (the exception, SRR0, SRR1, LR, CTR, CR,
 * XER, DSISR and DAR, the 32 GPRs in a grid, the callers up the stack,
 * the game's ID and RiftWii's version) and a translated line at the
 * bottom saying which button goes where.
 *
 * The menu draws everything in its own font and language before the
 * launch, so the blob carries no font. The two lines go into the card; the
 * dump is put together at the crash from pieces the menu drew into a
 * strip next to the card (the hex digits, the register names, the
 * exception names and a few words), as only the crash knows the values.
 * The digits go on a grid of equal cells, so the dump's columns line up as
 * in a fixed-width font.
 *
 * Shared by the blob (gc_rt.c), the loader (plugins/gamecube/gcnative.cpp, the
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
 * in the top bits, centred on the frame. Bands from the top: the header
 * line, the dump (rows of GC_CRASH_ROW_H, filled by the blob from the
 * strip) and the hint (up to two lines).
 * The card keeps inside a TV's title-safe area, as overscan can hide
 * about 5% of the picture on each side: on a 640-pixel-wide frame it
 * leaves 32 pixels at the left and right, and on a 448-line frame (the
 * shortest a game shows) 24 lines at the top and bottom. The menu sizes
 * the dump's cells so its four columns fit the card's width, at 4:3 and
 * at 16:9. */
#define GC_CRASH_SAFE_W 576u
#define GC_CRASH_SAFE_H 400u
#define GC_CRASH_CARD_W GC_CRASH_SAFE_W
#define GC_CRASH_HEADER_H 36u
#define GC_CRASH_DUMP_Y 44u
#define GC_CRASH_ROW_H 20u
#define GC_CRASH_DUMP_ROWS 15u
#define GC_CRASH_HINT_Y (GC_CRASH_DUMP_Y + GC_CRASH_DUMP_ROWS * GC_CRASH_ROW_H + 12u)
#define GC_CRASH_HINT_H 44u
#define GC_CRASH_CARD_H (GC_CRASH_HINT_Y + GC_CRASH_HINT_H)
#define GC_CRASH_CARD_BYTES (GC_CRASH_CARD_W * GC_CRASH_CARD_H / 4u)
typedef char gc_crash_card_safe[(GC_CRASH_CARD_H <= GC_CRASH_SAFE_H) ? 1 : -1];

/* The dump's grid: four columns of GC_CRASH_COLUMN_CELLS cells, each a
 * register's name and, GC_CRASH_VALUE_CELL cells in, its eight digits. */
#define GC_CRASH_COLUMNS 4u
#define GC_CRASH_COLUMN_CELLS 14u
#define GC_CRASH_VALUE_CELL 5u
/* The dump's rows (the others stay blank between the groups). */
#define GC_CRASH_ROW_EXCEPTION 0u
#define GC_CRASH_ROW_SPECIAL 2u   /* SRR0 SRR1 LR CTR, then CR XER DSISR DAR */
#define GC_CRASH_ROW_GPRS 5u      /* r0 r8 r16 r24 to r7 r15 r23 r31 */
#define GC_CRASH_ROW_CHAIN 14u

/* The strip's pieces: the hex digits 0-F, "x", one name for each
 * exception the hook can see (gc_crash_name_piece), the register names'
 * parts, then the menu's own words. */
#define GC_CRASH_PIECES 40u
#define GC_CRASH_PIECE_X 16u
#define GC_CRASH_PIECE_NAMES 17u
#define GC_CRASH_NAME_COUNT 9u
#define GC_CRASH_PIECE_OPEN 26u    /* "(" */
#define GC_CRASH_PIECE_CLOSE 27u   /* ")" */
#define GC_CRASH_PIECE_R 28u       /* "R", before a GPR's number (a capital: the menu font's small r reads as f at the dump's size) */
#define GC_CRASH_PIECE_SRR 29u     /* "SRR", before 0 or 1 */
#define GC_CRASH_PIECE_LR 30u
#define GC_CRASH_PIECE_CTR 31u
#define GC_CRASH_PIECE_CR 32u
#define GC_CRASH_PIECE_XER 33u
#define GC_CRASH_PIECE_DSISR 34u
#define GC_CRASH_PIECE_DAR 35u
#define GC_CRASH_PIECE_TEXT 36u    /* the menu's own words from here */
#define GC_CRASH_PIECE_GAME 36u    /* the game's ID */
#define GC_CRASH_PIECE_VERSION 37u /* "RiftWii 2610-545" */
#define GC_CRASH_PIECE_CALLED 38u  /* the translated "Called from" */
#define GC_CRASH_NO_PIECE 0xFFu
/* Columns copied past a piece's advance, for glyphs that reach beyond it. */
#define GC_CRASH_PIECE_PAD 3u

#define GC_CRASH_STRIP_W 2048u
#define GC_CRASH_STRIP_H GC_CRASH_ROW_H
#define GC_CRASH_STRIP_BYTES (GC_CRASH_STRIP_W * GC_CRASH_STRIP_H / 4u)

/* Right after the card in MEM2 (loader-placed): where each piece is in the
 * strip, how far it moves the pen, and the width of the dump's cells. */
struct gc_crash_strip {
    uint16_t at[GC_CRASH_PIECES];
    uint16_t advance[GC_CRASH_PIECES];  /* 0: the piece is missing */
    uint16_t cell;                      /* pixels; the widest digit's advance and a little */
    uint16_t reserved[15];
    uint8_t coverage[GC_CRASH_STRIP_BYTES];
};
#define GC_CRASH_STRIP_HEADER 192u
#define GC_CRASH_SCREEN_BYTES (GC_CRASH_CARD_BYTES + GC_CRASH_STRIP_HEADER + GC_CRASH_STRIP_BYTES)
typedef char gc_crash_strip_size[(sizeof(struct gc_crash_strip) == GC_CRASH_STRIP_HEADER + GC_CRASH_STRIP_BYTES) ? 1 : -1];

/* What the dump shows, from the crashed context (the SDK's OSContext) and
 * the crash's own record. */
struct gc_crash_regs {
    uint32_t exception;
    uint32_t srr0, srr1, lr, ctr, cr, xer, dsisr, dar;
    uint32_t gpr[32];
    uint32_t chain[4];  /* the saved LRs up the stack, 0 where it ended */
};

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

/* Copies a piece from the strip into the card's dump at `pen` on dump row
 * `row`; returns the pen after it. A missing piece moves nothing. */
static inline uint32_t gc_crash_put_piece(const struct gc_crash_strip* strip, uint8_t* card, uint32_t piece,
                                          uint32_t pen, uint32_t row) {
    const uint32_t top = GC_CRASH_DUMP_Y + row * GC_CRASH_ROW_H;
    uint32_t column, y;
    uint32_t advance;
    if (piece >= GC_CRASH_PIECES || row >= GC_CRASH_DUMP_ROWS) return pen;
    advance = strip->advance[piece];
    if (advance == 0) return pen;
    for (column = 0; column < advance + GC_CRASH_PIECE_PAD; ++column) {
        const uint32_t from = strip->at[piece] + column;
        const uint32_t to = pen + column;
        if (from >= GC_CRASH_STRIP_W || to >= GC_CRASH_CARD_W) break;
        for (y = 0; y < GC_CRASH_STRIP_H; ++y) {
            const uint32_t level = gc_crash_level(strip->coverage, GC_CRASH_STRIP_W, from, y);
            if (level != 0) gc_crash_raise(card, GC_CRASH_CARD_W, to, top + y, level);
        }
    }
    return pen + advance;
}

/* One character on the grid: the piece centred in the cell at `pen`;
 * returns the next cell. */
static inline uint32_t gc_crash_put_cell(const struct gc_crash_strip* strip, uint8_t* card, uint32_t piece,
                                         uint32_t pen, uint32_t row) {
    const uint32_t cell = strip->cell;
    const uint32_t advance = piece < GC_CRASH_PIECES ? strip->advance[piece] : 0u;
    const uint32_t inset = advance < cell ? (cell - advance) / 2u : 0u;
    gc_crash_put_piece(strip, card, piece, pen + inset, row);
    return pen + cell;
}

/* `value`'s hex digits on the grid: all eight, or from its first one that
 * is not zero when `all` is 0. Returns the pen after them. */
static inline uint32_t gc_crash_put_hex(const struct gc_crash_strip* strip, uint8_t* card, uint32_t value,
                                        int all, uint32_t pen, uint32_t row) {
    int shift;
    int started = all;
    for (shift = 28; shift >= 0; shift -= 4) {
        const uint32_t digit = (value >> shift) & 0xFu;
        if (digit != 0) started = 1;
        if (!started && shift != 0) continue;
        pen = gc_crash_put_cell(strip, card, digit, pen, row);
    }
    return pen;
}

/* A register in the grid: its name (one piece, then a number's decimal
 * digits when `number` is not 0xFF) at the column's start, its value
 * GC_CRASH_VALUE_CELL cells in. */
static inline void gc_crash_put_register(const struct gc_crash_strip* strip, uint8_t* card, uint32_t name,
                                         uint32_t number, uint32_t value, uint32_t column, uint32_t row) {
    const uint32_t start = column * GC_CRASH_COLUMN_CELLS * strip->cell;
    uint32_t pen = gc_crash_put_piece(strip, card, name, start, row);
    if (number != 0xFFu) {
        if (number >= 10u) pen = gc_crash_put_piece(strip, card, number / 10u, pen, row);
        gc_crash_put_piece(strip, card, number % 10u, pen, row);
    }
    gc_crash_put_hex(strip, card, value, 1, start + GC_CRASH_VALUE_CELL * strip->cell, row);
}

/* The whole dump into the card. */
static inline void gc_crash_dump(const struct gc_crash_strip* strip, uint8_t* card, const struct gc_crash_regs* regs) {
    const uint32_t cell = strip->cell;
    const uint32_t name = gc_crash_name_piece(regs->exception);
    uint32_t pen = 0;
    uint32_t i;
    if (cell == 0) return;
    /* "ISI (0x3)", then the game and the version from the third column. */
    if (name != GC_CRASH_NO_PIECE && strip->advance[name] != 0)
        pen = gc_crash_put_piece(strip, card, name, pen, GC_CRASH_ROW_EXCEPTION);
    pen += cell;
    pen = gc_crash_put_piece(strip, card, GC_CRASH_PIECE_OPEN, pen, GC_CRASH_ROW_EXCEPTION);
    pen = gc_crash_put_piece(strip, card, 0u, pen, GC_CRASH_ROW_EXCEPTION);
    pen = gc_crash_put_piece(strip, card, GC_CRASH_PIECE_X, pen, GC_CRASH_ROW_EXCEPTION);
    pen = gc_crash_put_hex(strip, card, regs->exception, 0, pen, GC_CRASH_ROW_EXCEPTION);
    pen = gc_crash_put_piece(strip, card, GC_CRASH_PIECE_CLOSE, pen, GC_CRASH_ROW_EXCEPTION);
    {
        const uint32_t third = 2u * GC_CRASH_COLUMN_CELLS * cell;
        pen = pen + cell > third ? pen + cell : third;
    }
    pen = gc_crash_put_piece(strip, card, GC_CRASH_PIECE_GAME, pen, GC_CRASH_ROW_EXCEPTION);
    gc_crash_put_piece(strip, card, GC_CRASH_PIECE_VERSION, pen + 2u * cell, GC_CRASH_ROW_EXCEPTION);

    gc_crash_put_register(strip, card, GC_CRASH_PIECE_SRR, 0u, regs->srr0, 0u, GC_CRASH_ROW_SPECIAL);
    gc_crash_put_register(strip, card, GC_CRASH_PIECE_SRR, 1u, regs->srr1, 1u, GC_CRASH_ROW_SPECIAL);
    gc_crash_put_register(strip, card, GC_CRASH_PIECE_LR, 0xFFu, regs->lr, 2u, GC_CRASH_ROW_SPECIAL);
    gc_crash_put_register(strip, card, GC_CRASH_PIECE_CTR, 0xFFu, regs->ctr, 3u, GC_CRASH_ROW_SPECIAL);
    gc_crash_put_register(strip, card, GC_CRASH_PIECE_CR, 0xFFu, regs->cr, 0u, GC_CRASH_ROW_SPECIAL + 1u);
    gc_crash_put_register(strip, card, GC_CRASH_PIECE_XER, 0xFFu, regs->xer, 1u, GC_CRASH_ROW_SPECIAL + 1u);
    gc_crash_put_register(strip, card, GC_CRASH_PIECE_DSISR, 0xFFu, regs->dsisr, 2u, GC_CRASH_ROW_SPECIAL + 1u);
    gc_crash_put_register(strip, card, GC_CRASH_PIECE_DAR, 0xFFu, regs->dar, 3u, GC_CRASH_ROW_SPECIAL + 1u);

    /* The GPRs down the columns: r0-r7, r8-r15, r16-r23, r24-r31. */
    for (i = 0; i < 32u; ++i)
        gc_crash_put_register(strip, card, GC_CRASH_PIECE_R, i, regs->gpr[i], i / 8u, GC_CRASH_ROW_GPRS + i % 8u);

    /* "Called from" and the callers, each on the grid two cells apart. */
    pen = gc_crash_put_piece(strip, card, GC_CRASH_PIECE_CALLED, 0u, GC_CRASH_ROW_CHAIN);
    pen = pen + 2u * cell > GC_CRASH_VALUE_CELL * cell ? pen + 2u * cell : GC_CRASH_VALUE_CELL * cell;
    for (i = 0; i < 4u; ++i) {
        if (regs->chain[i] == 0) break;
        pen = gc_crash_put_hex(strip, card, regs->chain[i], 1, pen, GC_CRASH_ROW_CHAIN) + 2u * cell;
    }
}

#endif
