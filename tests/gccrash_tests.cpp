// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "gc_rt.h"
#include "riftwii/gccrash.hpp"

namespace {

int failures = 0;

#define EXPECT_TRUE(x)                                                   \
    do {                                                                 \
        if (!(x)) {                                                      \
            std::printf("FAILED: %s at line %d\n", #x, __LINE__);        \
            ++failures;                                                  \
        }                                                                \
    } while (0)

void put(std::vector<std::uint8_t>& trace, std::size_t record, unsigned index, std::uint32_t v) {
    std::uint8_t* p = trace.data() + record * 512 + 4 * index;
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

void record(std::vector<std::uint8_t>& trace, std::size_t at, std::uint32_t seq, std::uint32_t where) {
    put(trace, at, GC_TR_MAGIC, GC_TRACE_MAGIC);
    put(trace, at, GC_TR_SEQ, seq);
    put(trace, at, GC_TR_WHERE, where);
}

void test_no_crash() {
    std::vector<std::uint8_t> trace(512 * 8, 0);
    record(trace, 0, 1, GC_TRACE_JUMP);
    record(trace, 1, 2, GC_TRACE_READ);
    std::string text;
    EXPECT_TRUE(!riftwii::describe_gc_crash(trace, "GMSE04", text));
    EXPECT_TRUE(!riftwii::describe_gc_crash({}, "GMSE04", text));
    // A record's worth of junk with the crash's number but no magic.
    std::vector<std::uint8_t> junk(512, 0);
    put(junk, 0, GC_TR_WHERE, GC_TRACE_FATAL);
    EXPECT_TRUE(!riftwii::describe_gc_crash(junk, "GMSE04", text));
}

void test_crash() {
    std::vector<std::uint8_t> trace(512 * 8 + 100, 0);  // a short tail is ignored
    record(trace, 0, 1, GC_TRACE_JUMP);
    record(trace, 2, 3, GC_TRACE_FATAL);
    put(trace, 2, GC_TR_EXCEPTION, 6);
    put(trace, 2, GC_TR_BEAT_PC, 0x80001234);
    // A later crash (the ring) is the one described.
    record(trace, 5, 9, GC_TRACE_FATAL);
    put(trace, 5, GC_TR_EXCEPTION, 2);
    put(trace, 5, GC_TR_BEAT_PC, 0x8034ACD8);
    put(trace, 5, GC_TR_DAR, 0x00000004);
    put(trace, 5, GC_TR_CRASH_CHAIN, 0x80005678);
    put(trace, 5, GC_TR_CRASH_CHAIN + 1, 0x80009ABC);
    put(trace, 5, GC_TR_READS, 1801);
    std::string text;
    EXPECT_TRUE(riftwii::describe_gc_crash(trace, "GLMP01", text));
    EXPECT_TRUE(text.find("GameCube game GLMP01 crashed: DSI") == 0);
    EXPECT_TRUE(text.find("SRR0 0x8034acd8") != std::string::npos);
    EXPECT_TRUE(text.find("DAR 0x00000004") != std::string::npos);
    EXPECT_TRUE(text.find("called from 0x80005678 0x80009abc\n") != std::string::npos);
    EXPECT_TRUE(text.find("disc reads 1801") != std::string::npos);
    // Before the game's OS: the vector's address.
    put(trace, 5, GC_TR_EXCEPTION, 0x10 + 3);
    EXPECT_TRUE(riftwii::describe_gc_crash(trace, "", text));
    EXPECT_TRUE(text.find("(unknown) crashed: vector 0x00000300, before the game's OS was up") != std::string::npos);
    // A jump into the memory card's struct, caught by its landing words.
    put(trace, 5, GC_TR_EXCEPTION, GC_EXCEPTION_STRAY);
    put(trace, 5, GC_TR_DAR, 0x817DE860);
    EXPECT_TRUE(riftwii::describe_gc_crash(trace, "GZLE01", text));
    EXPECT_TRUE(text.find("GZLE01 crashed: a jump into RiftWii's own data at SRR0 (DAR holds CTR)\n") !=
                std::string::npos);
    EXPECT_TRUE(text.find("DAR 0x817de860") != std::string::npos);
}

// The crash screen's blue is RGB (0, 120, 215) in BT.601's studio range,
// and full coverage is white.
void test_blue() {
    const double r = 0, g = 120, b = 215;
    const double y = 16 + (65.481 * r + 128.553 * g + 24.966 * b) / 255;
    const double cb = 128 + (-37.797 * r - 74.203 * g + 112.0 * b) / 255;
    const double cr = 128 + (112.0 * r - 93.786 * g - 18.214 * b) / 255;
    EXPECT_TRUE(GC_CRASH_BLUE_Y == static_cast<int>(std::lround(y)));
    EXPECT_TRUE(GC_CRASH_BLUE_CB == static_cast<int>(std::lround(cb)));
    EXPECT_TRUE(GC_CRASH_BLUE_CR == static_cast<int>(std::lround(cr)));
    EXPECT_TRUE(gc_crash_yuyv(0, 0) == 0x62BC6245u);
    EXPECT_TRUE(gc_crash_yuyv(255, 255) == 0xEB80EB80u);
    // Each pixel its own luma, the pair's colour from both.
    const std::uint32_t mixed = gc_crash_yuyv(255, 0);
    EXPECT_TRUE((mixed >> 24) == 235);
    EXPECT_TRUE(((mixed >> 8) & 0xFF) == 98);
    EXPECT_TRUE(((mixed >> 16) & 0xFF) == 188 - (188 - 128) * 127 / 255);
    EXPECT_TRUE((mixed & 0xFF) == 69 + (128 - 69) * 127 / 255);
    // The card's levels (0-3, times 85) go evenly from blue to white.
    std::uint32_t last = 0;
    for (std::uint32_t level = 0; level <= 3; ++level) {
        const std::uint32_t luma = gc_crash_yuyv(level * 85, level * 85) >> 24;
        EXPECT_TRUE(level == 0 || luma > last);
        last = luma;
    }
}

void test_names() {
    EXPECT_TRUE(riftwii::gc_crash_piece_text(gc_crash_name_piece(2)) == "DSI");
    EXPECT_TRUE(riftwii::gc_crash_piece_text(gc_crash_name_piece(3)) == "ISI");
    EXPECT_TRUE(riftwii::gc_crash_piece_text(gc_crash_name_piece(1)) == "Machine check");
    EXPECT_TRUE(riftwii::gc_crash_piece_text(gc_crash_name_piece(5)) == "Alignment");
    EXPECT_TRUE(riftwii::gc_crash_piece_text(gc_crash_name_piece(6)) == "Program");
    EXPECT_TRUE(riftwii::gc_crash_piece_text(gc_crash_name_piece(7)) == "Float");
    EXPECT_TRUE(riftwii::gc_crash_piece_text(gc_crash_name_piece(12)) == "Breakpoint");
    EXPECT_TRUE(riftwii::gc_crash_piece_text(gc_crash_name_piece(15)) == "Protection");
    EXPECT_TRUE(riftwii::gc_crash_piece_text(gc_crash_name_piece(GC_EXCEPTION_STRAY)) == "Stray jump");
    // Before the game's OS: 0x10 plus the vector's number, one more than
    // the OS's own (the loader's stubs cover vectors 0x200-0xD00).
    EXPECT_TRUE(gc_crash_name_piece(GC_EXCEPTION_EARLY + 3) == gc_crash_name_piece(2));
    EXPECT_TRUE(gc_crash_name_piece(GC_EXCEPTION_EARLY + 4) == gc_crash_name_piece(3));
    EXPECT_TRUE(gc_crash_name_piece(GC_EXCEPTION_EARLY + 7) == gc_crash_name_piece(6));
    EXPECT_TRUE(gc_crash_name_piece(GC_EXCEPTION_EARLY + 2) == gc_crash_name_piece(1));
    // Ones with no name are shown as their number.
    EXPECT_TRUE(gc_crash_name_piece(0) == GC_CRASH_NO_PIECE);
    EXPECT_TRUE(gc_crash_name_piece(8) == GC_CRASH_NO_PIECE);
    EXPECT_TRUE(gc_crash_name_piece(GC_EXCEPTION_EARLY) == GC_CRASH_NO_PIECE);
    EXPECT_TRUE(gc_crash_name_piece(GC_EXCEPTION_EARLY + 0xC) == GC_CRASH_NO_PIECE);
    // Every number maps inside the names, and every name has a text.
    for (std::uint32_t e = 0; e < 0x200; ++e) {
        const std::uint32_t piece = gc_crash_name_piece(e);
        if (piece == GC_CRASH_NO_PIECE) continue;
        EXPECT_TRUE(piece >= GC_CRASH_PIECE_NAMES && piece < GC_CRASH_PIECE_TEXT);
    }
    for (unsigned piece = 0; piece < GC_CRASH_PIECE_TEXT; ++piece)
        EXPECT_TRUE(!riftwii::gc_crash_piece_text(piece).empty());
    EXPECT_TRUE(riftwii::gc_crash_piece_text(10) == "A");
    EXPECT_TRUE(riftwii::gc_crash_piece_text(GC_CRASH_PIECE_X) == "x");
    EXPECT_TRUE(riftwii::gc_crash_piece_text(GC_CRASH_PIECE_SRR) == "SRR");
    EXPECT_TRUE(riftwii::gc_crash_piece_text(GC_CRASH_PIECE_DSISR) == "DSISR");
    EXPECT_TRUE(riftwii::gc_crash_piece_text(GC_CRASH_PIECE_R) == "R");
    EXPECT_TRUE(riftwii::gc_crash_piece_text(GC_CRASH_PIECE_TEXT).empty());
    EXPECT_TRUE(riftwii::gc_crash_piece_text(GC_CRASH_PIECE_CALLED).empty());
}

void test_pack() {
    const std::uint8_t coverage[5] = {0, 255, 128, 40, 255};
    std::uint8_t packed[2] = {0xFF, 0xFF};
    riftwii::gc_crash_pack(coverage, 5, packed);
    // Levels 0, 3, 2, 0 and 3: the leftmost pixel in the top bits.
    EXPECT_TRUE(packed[0] == 0x38);
    EXPECT_TRUE(packed[1] == 0xC0);
}

// Pieces drawn as a bar in their first column, so the composed dump shows
// where each one landed.
struct Bars {
    gc_crash_strip strip;
    std::vector<std::uint8_t> card = std::vector<std::uint8_t>(GC_CRASH_CARD_BYTES, 0);
};

unsigned advance_of(unsigned piece) { return 5 + piece % 3; }

void make_strip(gc_crash_strip& strip) {
    std::memset(&strip, 0, sizeof(strip));
    strip.cell = 8;
    unsigned next = 0;
    for (unsigned piece = 0; piece < GC_CRASH_PIECES; ++piece) {
        const unsigned advance = advance_of(piece);
        std::vector<std::uint8_t> drawn(advance * 2 * GC_CRASH_STRIP_H, 0);
        for (unsigned y = 0; y < GC_CRASH_STRIP_H; ++y) drawn[y * advance * 2] = 255;
        EXPECT_TRUE(riftwii::gc_crash_strip_add(strip, piece, drawn.data(), advance * 2, advance, next));
    }
}

bool bar_at(const std::vector<std::uint8_t>& card, unsigned x, unsigned row) {
    return gc_crash_level(card.data(), GC_CRASH_CARD_W, x, GC_CRASH_DUMP_Y + row * GC_CRASH_ROW_H) == 3;
}

// Where a digit lands on the grid: centred in its cell.
unsigned digit_x(unsigned cell_start, unsigned digit) { return cell_start + (8 - advance_of(digit)) / 2; }

void test_compose() {
    static gc_crash_strip strip;
    make_strip(strip);
    EXPECT_TRUE(strip.advance[7] == 6 && strip.at[1] == 5 + GC_CRASH_PIECE_PAD);
    // A piece that does not fit is left missing.
    std::vector<std::uint8_t> wide(GC_CRASH_STRIP_W * GC_CRASH_STRIP_H, 255);
    unsigned full = GC_CRASH_STRIP_W - 4;
    static gc_crash_strip spare;
    std::memset(&spare, 0, sizeof(spare));
    EXPECT_TRUE(!riftwii::gc_crash_strip_add(spare, GC_CRASH_PIECE_TEXT, wide.data(), GC_CRASH_STRIP_W, 40, full));
    EXPECT_TRUE(spare.advance[GC_CRASH_PIECE_TEXT] == 0 && full == GC_CRASH_STRIP_W - 4);

    gc_crash_regs regs;
    std::memset(&regs, 0, sizeof(regs));
    regs.exception = 3;
    regs.srr0 = 0x8023A1C4u;
    regs.dar = 0xFFFFFFFFu;
    for (unsigned i = 0; i < 32; ++i) regs.gpr[i] = 0x11111111u * (i % 16);
    regs.chain[0] = 0x80005678u;
    std::vector<std::uint8_t> card(GC_CRASH_CARD_BYTES, 0);
    gc_crash_dump(&strip, card.data(), &regs);

    // The first row: ISI, then "(0x3)".
    EXPECT_TRUE(bar_at(card, 0, GC_CRASH_ROW_EXCEPTION));
    unsigned pen = advance_of(GC_CRASH_PIECE_NAMES + 2) + 8;
    EXPECT_TRUE(bar_at(card, pen, GC_CRASH_ROW_EXCEPTION));  // "("
    pen += advance_of(GC_CRASH_PIECE_OPEN) + advance_of(0) + advance_of(GC_CRASH_PIECE_X);
    EXPECT_TRUE(bar_at(card, digit_x(pen, 3), GC_CRASH_ROW_EXCEPTION));
    // The game's ID from the third column.
    EXPECT_TRUE(bar_at(card, 2 * GC_CRASH_COLUMN_CELLS * 8, GC_CRASH_ROW_EXCEPTION));

    // SRR0: "SRR" and "0" from the left, its digits six cells in.
    EXPECT_TRUE(bar_at(card, 0, GC_CRASH_ROW_SPECIAL));
    EXPECT_TRUE(bar_at(card, advance_of(GC_CRASH_PIECE_SRR), GC_CRASH_ROW_SPECIAL));
    const unsigned srr0_digits[8] = {8, 0, 2, 3, 0xA, 1, 0xC, 4};
    for (unsigned i = 0; i < 8; ++i)
        EXPECT_TRUE(bar_at(card, digit_x((GC_CRASH_VALUE_CELL + i) * 8, srr0_digits[i]), GC_CRASH_ROW_SPECIAL));
    // DAR in the fourth column of the next row, all F.
    const unsigned dar = 3 * GC_CRASH_COLUMN_CELLS * 8;
    EXPECT_TRUE(bar_at(card, dar, GC_CRASH_ROW_SPECIAL + 1));
    for (unsigned i = 0; i < 8; ++i)
        EXPECT_TRUE(bar_at(card, digit_x(dar + (GC_CRASH_VALUE_CELL + i) * 8, 0xF), GC_CRASH_ROW_SPECIAL + 1));

    // r13 in the second column, sixth row of the GPRs: "r", "1", "3", D...
    const unsigned r13 = GC_CRASH_COLUMN_CELLS * 8;
    const unsigned row = GC_CRASH_ROW_GPRS + 5;
    EXPECT_TRUE(bar_at(card, r13, row));
    EXPECT_TRUE(bar_at(card, r13 + advance_of(GC_CRASH_PIECE_R), row));
    EXPECT_TRUE(bar_at(card, r13 + advance_of(GC_CRASH_PIECE_R) + advance_of(1), row));
    EXPECT_TRUE(bar_at(card, digit_x(r13 + GC_CRASH_VALUE_CELL * 8, 0xD), row));
    // r7, single digit, in the first column's last row.
    EXPECT_TRUE(bar_at(card, advance_of(GC_CRASH_PIECE_R), GC_CRASH_ROW_GPRS + 7));
    EXPECT_TRUE(!bar_at(card, advance_of(GC_CRASH_PIECE_R) + advance_of(7), GC_CRASH_ROW_GPRS + 7));

    // The callers: "Called from", then one address, then nothing.
    EXPECT_TRUE(bar_at(card, 0, GC_CRASH_ROW_CHAIN));
    const unsigned chain = GC_CRASH_VALUE_CELL * 8;
    EXPECT_TRUE(bar_at(card, digit_x(chain, 8), GC_CRASH_ROW_CHAIN));
    EXPECT_TRUE(bar_at(card, digit_x(chain + 7 * 8, 8), GC_CRASH_ROW_CHAIN));
    for (unsigned x = chain + 8 * 8; x < GC_CRASH_CARD_W; ++x) EXPECT_TRUE(!bar_at(card, x, GC_CRASH_ROW_CHAIN));

    // Nothing in the blank rows, the header or the hint.
    for (unsigned x = 0; x < GC_CRASH_CARD_W; ++x) {
        EXPECT_TRUE(!bar_at(card, x, 1));
        EXPECT_TRUE(!bar_at(card, x, 4));
        EXPECT_TRUE(!bar_at(card, x, 13));
        EXPECT_TRUE(gc_crash_level(card.data(), GC_CRASH_CARD_W, x, 0) == 0);
        EXPECT_TRUE(gc_crash_level(card.data(), GC_CRASH_CARD_W, x, GC_CRASH_HINT_Y) == 0);
    }
    // The dump's four columns fit across the card at an 8-pixel cell, and
    // the hint stays inside it.
    EXPECT_TRUE(GC_CRASH_COLUMNS * GC_CRASH_COLUMN_CELLS * 8 <= GC_CRASH_CARD_W);
    EXPECT_TRUE(GC_CRASH_HINT_Y + GC_CRASH_HINT_H == GC_CRASH_CARD_H);
}

}  // namespace

int main() {
    test_no_crash();
    test_crash();
    test_blue();
    test_names();
    test_pack();
    test_compose();
    if (failures == 0) std::printf("gccrash: all passed\n");
    return failures == 0 ? 0 : 1;
}
