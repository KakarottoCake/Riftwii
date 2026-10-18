// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gccrash.hpp"

#include <algorithm>
#include <cstdio>

#include "gc_bt.h"
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

// The steps a Wii Remote goes through, in order, with their names in a
// report. A remote alone skips the extension's three.
struct BtStep {
    std::uint32_t bit;
    const char* name;
};
constexpr BtStep kBtSteps[] = {
    {GC_BT_SEEN_OPEN, "Bluetooth opened"},
    {GC_BT_SEEN_RESET, "controller reset"},
    {GC_BT_SEEN_KEYS, "stored link keys read"},
    {GC_BT_SEEN_SCAN, "waiting for remotes"},
    {GC_BT_SEEN_CONN_REQUEST, "a remote asked to connect"},
    {GC_BT_SEEN_CONNECTED, "connected"},
    {GC_BT_SEEN_AUTH_OK, "authenticated"},
    {GC_BT_SEEN_L2CAP, "HID channel opened"},
    {GC_BT_SEEN_READY, "channels ready, LED set"},
    {GC_BT_SEEN_STATUS, "status report"},
    {GC_BT_SEEN_EXTENSION, "extension plugged in"},
    {GC_BT_SEEN_EXT_WRITE, "extension write acknowledged"},
    {GC_BT_SEEN_EXT_ID, "extension identified"},
    {GC_BT_SEEN_MODE, "report mode set"},
    {GC_BT_SEEN_DATA, "buttons reported"},
};
// What went wrong on the way, when it did.
constexpr BtStep kBtProblems[] = {
    {GC_BT_SEEN_STEP_TIMEOUT, "a start-up step had no reply and was passed"},
    {GC_BT_SEEN_COMMAND_ERROR, "a command completed with an error"},
    {GC_BT_SEEN_CONN_FAILED, "a connection failed"},
    {GC_BT_SEEN_NO_KEY, "a remote had no stored link key"},
    {GC_BT_SEEN_PIN_REQUEST, "a PIN was asked for (refused: the remote is not synced to this console)"},
    {GC_BT_SEEN_AUTH_FAILED, "authentication failed"},
    {GC_BT_SEEN_DISCONNECT, "a remote disconnected"},
    {GC_BT_SEEN_EXT_ERROR, "the remote refused a step of the extension's start"},
    {GC_BT_SEEN_EXT_RETRY, "the extension's start was tried again"},
    {GC_BT_SEEN_EXT_GIVEN_UP, "the extension's start was given up: the remote played alone"},
};

std::string byte_hex(std::uint32_t v) {
    char text[8];
    std::snprintf(text, sizeof(text), "0x%02x", static_cast<unsigned>(v & 0xFFu));
    return text;
}

std::string bt_stage_name(std::uint32_t stage) {
    switch (stage) {
        case GC_BT_STAGE_CLOSED: return "not opened (no pad read yet)";
        case GC_BT_STAGE_RESET: return "resetting the controller";
        case GC_BT_STAGE_KEYS: return "reading stored link keys";
        case GC_BT_STAGE_SCAN: return "turning on page scan";
        case GC_BT_STAGE_READY: return "ready for remotes";
        case GC_BT_STAGE_FAILED: return "given up (the device did not open or a write failed)";
        default: return "stage " + std::to_string(stage);
    }
}

}  // namespace

bool describe_gc_remotes(const std::vector<std::uint8_t>& trace, std::string& text) {
    const std::uint8_t* newest = nullptr;
    std::uint32_t newest_seq = 0;
    for (std::size_t at = 0; at + kRecordBytes <= trace.size(); at += kRecordBytes) {
        const std::uint8_t* record = trace.data() + at;
        if (word(record, GC_TR_MAGIC) != GC_TRACE_MAGIC) continue;
        const std::uint32_t seq = word(record, GC_TR_SEQ);
        if (newest == nullptr || seq > newest_seq) {
            newest = record;
            newest_seq = seq;
        }
    }
    if (newest == nullptr) return false;
    const std::uint32_t progress = word(newest, GC_TR_BT_PROGRESS);
    const std::uint32_t keys = word(newest, GC_TR_BT_KEYS);
    const std::uint32_t link = word(newest, GC_TR_BT_LINK);
    const std::uint32_t ext = word(newest, GC_TR_BT_EXT);
    const std::uint32_t first_remote = word(newest, GC_TR_BT_REMOTES);
    text = "Wii Remotes in the last GameCube game (trace record " + std::to_string(newest_seq) + ", after " +
           std::to_string(word(newest, GC_TR_PAD_READS)) + " pad reads):\n";
    text += "Bluetooth " + bt_stage_name(word(newest, GC_TR_BT_STAGE)) + ", last error " +
            hex(word(newest, GC_TR_BT_ERROR)) + ", " + std::to_string(keys >> 16) + " stored link key(s), " +
            std::to_string(keys & 0xFFFFu) + " connection(s), " + std::to_string(word(newest, GC_TR_BT_REPORTS)) +
            " button report(s).\n";
    std::string reached, missing;
    for (const BtStep& step : kBtSteps) {
        std::string& list = (progress & step.bit) ? reached : missing;
        list += list.empty() ? "" : ", ";
        list += step.name;
    }
    text += "Reached: " + (reached.empty() ? std::string("nothing") : reached) + ".\n";
    if (!missing.empty()) text += "Not reached: " + missing + ".\n";
    std::string problems;
    for (const BtStep& problem : kBtProblems) {
        if (!(progress & problem.bit)) continue;
        problems += problems.empty() ? "" : "; ";
        problems += problem.name;
    }
    if (!problems.empty()) text += "On the way: " + problems + ".\n";
    if (progress & GC_BT_SEEN_PRO)
        text += "A Wii U Pro Controller connected (extension ID 01 20) and played with the Classic Controller's "
                "controls.\n";
    if (progress & GC_BT_SEEN_PRO) {
        // The raw 12-bit range each stick axis reached, to size GC_PRO_STICK_REACH.
        static const char* const kAxes[4] = {"left X", "right X", "left Y", "right Y"};
        text += "Pro Controller stick range (raw, lowest-highest):";
        for (std::uint32_t axis = 0; axis < 4; ++axis) {
            const std::uint32_t range = word(newest, GC_TR_BT_PRO_RANGE + axis);
            text += std::string(axis == 0 ? " " : ", ") + kAxes[axis] + " " + std::to_string(range >> 16) + "-" +
                    std::to_string(range & 0xFFFFu);
        }
        text += ".\n";
    }
    text += "Last connection status " + byte_hex(link >> 24) + ", authentication status " + byte_hex(link >> 16) +
            ", disconnect reason " + byte_hex(link >> 8) + ", extension error " + byte_hex(link) + ".\n";
    text += "Last extension ID " + byte_hex(ext >> 24) + " " + byte_hex(ext >> 16) + ", last report mode " +
            byte_hex(ext >> 8) + ", extensions given up " + std::to_string(ext & 0xFFu) + "; the first remote: " +
            "channels " + byte_hex(first_remote >> 24) + ", ready " + std::to_string((first_remote >> 16) & 0xFFu) +
            ", extension step " + std::to_string((first_remote >> 8) & 0xFFu) + " of 4, kind " +
            std::to_string(first_remote & 0xFFu) + ".\n";
    return true;
}

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
