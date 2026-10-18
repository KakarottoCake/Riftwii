// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gccrash.hpp"

#include <algorithm>
#include <cstdio>

#include "gc_rt.h"

namespace riftwii {
namespace {

constexpr std::size_t kRecordBytes = 512;

std::uint32_t word(const std::uint8_t* record, unsigned index) {
    const std::uint8_t* p = record + 4 * index;
    return static_cast<std::uint32_t>(p[0]) << 24 | static_cast<std::uint32_t>(p[1]) << 16 |
           static_cast<std::uint32_t>(p[2]) << 8 | p[3];
}

std::string hex(std::uint32_t v) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08x", static_cast<unsigned>(v));
    return text;
}

// gc_on_fatal's exception: the SDK's number from __OSUnhandledException,
// 0x10 + the vector's number for a crash before the game's OS set up its
// handlers (gc_entry.S's gc_dsi_entry), or a jump into RiftWii's own data
// (gc_stray_entry), with CTR in DAR.
std::string exception_name(std::uint32_t exception) {
    if (exception == GC_EXCEPTION_STRAY) return "a jump into RiftWii's own data at SRR0 (DAR holds CTR)";
    if (exception >= 0x10) return "vector " + hex((exception - 0x10) * 0x100) + ", before the game's OS was up";
    switch (exception) {
        case 1: return "machine check";
        case 2: return "DSI (a bad data access)";
        case 3: return "ISI (a jump to a bad address)";
        case 4: return "external interrupt";
        case 5: return "alignment";
        case 6: return "program (an illegal instruction or a trap)";
        case 7: return "floating point unavailable";
        case 8: return "decrementer";
        default: return "exception " + std::to_string(exception);
    }
}

}  // namespace

bool describe_gc_crash(const std::vector<std::uint8_t>& trace, const std::string& game_id, std::string& text) {
    const std::uint8_t* crash = nullptr;
    std::uint32_t newest = 0;
    for (std::size_t at = 0; at + kRecordBytes <= trace.size(); at += kRecordBytes) {
        const std::uint8_t* record = trace.data() + at;
        if (word(record, GC_TR_MAGIC) != GC_TRACE_MAGIC || word(record, GC_TR_WHERE) != GC_TRACE_FATAL) continue;
        const std::uint32_t seq = word(record, GC_TR_SEQ);
        if (crash == nullptr || seq > newest) {
            crash = record;
            newest = seq;
        }
    }
    if (crash == nullptr) return false;
    text = "GameCube game " + (game_id.empty() ? std::string("(unknown)") : game_id) + " crashed: " +
           exception_name(word(crash, GC_TR_EXCEPTION)) + "\n";
    text += "SRR0 " + hex(word(crash, GC_TR_BEAT_PC)) + "  LR " + hex(word(crash, GC_TR_BEAT_LR)) + "  SRR1 " +
            hex(word(crash, GC_TR_BEAT_MSR)) + "  DSISR " + hex(word(crash, GC_TR_DSISR)) + "  DAR " +
            hex(word(crash, GC_TR_DAR)) + "\n";
    text += "r1 " + hex(word(crash, GC_TR_CRASH_R1)) + "  r3 " + hex(word(crash, GC_TR_CRASH_R3)) + "  r4 " +
            hex(word(crash, GC_TR_CRASH_R4)) + "  r5 " + hex(word(crash, GC_TR_CRASH_R5)) + "  r31 " +
            hex(word(crash, GC_TR_CRASH_R31)) + "\n";
    text += "instruction at SRR0 " + hex(word(crash, GC_TR_CRASH_WORD)) + "\ncalled from";
    for (unsigned i = 0; i < 4; ++i) {
        const std::uint32_t lr = word(crash, GC_TR_CRASH_CHAIN + i);
        if (lr == 0) break;
        text += " " + hex(lr);
    }
    text += "\ndisc reads " + std::to_string(word(crash, GC_TR_READS)) + ", SD requests " +
            std::to_string(word(crash, GC_TR_SD_REQUESTS)) + " (" + std::to_string(word(crash, GC_TR_FAILURES)) +
            " failed), pad reads " + std::to_string(word(crash, GC_TR_PAD_READS)) + ", ARAM transfers " +
            std::to_string(word(crash, GC_TR_AR_DMAS)) + ", memory card reads " +
            std::to_string(word(crash, GC_TR_CARD_READS)) + "\n";
    return true;
}

std::string gc_crash_piece_text(unsigned piece) {
    if (piece < 16) return std::string(1, "0123456789ABCDEF"[piece]);
    if (piece == GC_CRASH_PIECE_X) return "x";
    // In gc_crash_name_piece's order. PowerPC's own names, kept in English
    // like the register names and the addresses next to them; RiftWii's
    // words around them are translated.
    if (piece >= GC_CRASH_PIECE_NAMES && piece < GC_CRASH_PIECE_NAMES + GC_CRASH_NAME_COUNT) {
        switch (piece - GC_CRASH_PIECE_NAMES) {
            case 0: return "Machine check";
            case 1: return "DSI";
            case 2: return "ISI";
            case 3: return "Alignment";
            case 4: return "Program";
            case 5: return "Float";
            case 6: return "Breakpoint";
            case 7: return "Protection";
            default: return "Stray jump";
        }
    }
    switch (piece) {
        case GC_CRASH_PIECE_OPEN: return "(";
        case GC_CRASH_PIECE_CLOSE: return ")";
        case GC_CRASH_PIECE_R: return "R";
        case GC_CRASH_PIECE_SRR: return "SRR";
        case GC_CRASH_PIECE_LR: return "LR";
        case GC_CRASH_PIECE_CTR: return "CTR";
        case GC_CRASH_PIECE_CR: return "CR";
        case GC_CRASH_PIECE_XER: return "XER";
        case GC_CRASH_PIECE_DSISR: return "DSISR";
        case GC_CRASH_PIECE_DAR: return "DAR";
        default: return "";
    }
}

void gc_crash_pack(const std::uint8_t* coverage, std::size_t pixels, std::uint8_t* packed) {
    for (std::size_t i = 0; i < (pixels + 3) / 4; ++i) packed[i] = 0;
    for (std::size_t i = 0; i < pixels; ++i) {
        // Four levels: the edges keep some of their smoothing.
        const unsigned level = (coverage[i] + 42u) / 85u;
        packed[i / 4] |= static_cast<std::uint8_t>(level << (6 - 2 * (i % 4)));
    }
}

bool gc_crash_strip_add(gc_crash_strip& strip, unsigned piece, const std::uint8_t* coverage, unsigned width,
                        unsigned advance, unsigned& next) {
    if (piece >= GC_CRASH_PIECES) return false;
    strip.at[piece] = 0;
    strip.advance[piece] = 0;
    // The columns the blob copies: the advance and a little past it.
    const unsigned columns = std::min(width, advance + GC_CRASH_PIECE_PAD);
    if (advance == 0 || next + advance + GC_CRASH_PIECE_PAD > GC_CRASH_STRIP_W) return false;
    for (unsigned y = 0; y < GC_CRASH_STRIP_H; ++y) {
        for (unsigned x = 0; x < columns; ++x) {
            const unsigned level = (coverage[y * width + x] + 42u) / 85u;
            if (level != 0) gc_crash_raise(strip.coverage, GC_CRASH_STRIP_W, next + x, y, level);
        }
    }
    strip.at[piece] = static_cast<std::uint16_t>(next);
    strip.advance[piece] = static_cast<std::uint16_t>(advance);
    next += advance + GC_CRASH_PIECE_PAD;
    return true;
}

}  // namespace riftwii
