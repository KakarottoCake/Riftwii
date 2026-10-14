// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gamefault.hpp"

#include <cstdio>
#include <ctime>

#include "rtfault.h"

namespace riftwii {
namespace {

constexpr std::uint64_t kWiiEpoch = 946684800;  // 2000-01-01 in Unix time

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

std::string hex(std::uint32_t v) {
    char buf[12];
    std::snprintf(buf, sizeof(buf), "%08X", v);
    return buf;
}

std::string text_field(const std::uint8_t* p, std::size_t n) {
    std::string s;
    for (std::size_t i = 0; i < n && p[i] != 0; ++i) s += (p[i] >= 0x20 && p[i] < 0x7F) ? static_cast<char>(p[i]) : '?';
    return s;
}

const char* exception_name(std::uint32_t e) {
    switch (e) {
        case RTFAULT_MACHINE_CHECK: return "machine check";
        case RTFAULT_DSI: return "bad data address (DSI)";
        case RTFAULT_ISI: return "bad code address (ISI)";
        case RTFAULT_ALIGNMENT: return "misaligned access";
        case RTFAULT_PROGRAM: return "illegal instruction or trap";
        case RTFAULT_FLOATING_POINT: return "floating point unavailable";
        default: return "exception";
    }
}

}  // namespace

bool describe_game_fault(const std::uint8_t* bytes, std::size_t size, std::string& text, std::string& error) {
    if (size < RTFAULT_RECORD_BYTES || be32(bytes) != RTFAULT_MAGIC) {
        error = "not a crash record";
        return false;
    }
    if (be32(bytes + 4) != RTFAULT_VERSION) {
        error = "a crash record of another version (" + std::to_string(be32(bytes + 4)) + ")";
        return false;
    }
    const auto word = [bytes](std::size_t offset) { return be32(bytes + offset); };
    const std::uint32_t exception = word(offsetof(rtfault_record, exception));
    const std::uint32_t srr0 = word(offsetof(rtfault_record, srr0));
    const std::uint32_t dsisr = word(offsetof(rtfault_record, dsisr));
    const std::uint32_t dar = word(offsetof(rtfault_record, dar));
    const std::uint8_t* game = bytes + offsetof(rtfault_record, game);
    const std::string id = text_field(game, 6);

    text = "The game crashed: " + (id.empty() ? std::string("unknown game") : id) + " (disc " +
           std::to_string(game[6]) + ", version " + std::to_string(game[7]) + "), under RiftWii " +
           text_field(bytes + offsetof(rtfault_record, riftwii), RTFAULT_VERSION_BYTES) + "\n";
    const std::uint32_t ticks = word(offsetof(rtfault_record, ticks_per_second));
    if (ticks != 0) {
        const std::uint64_t tb = (static_cast<std::uint64_t>(word(offsetof(rtfault_record, tb_hi))) << 32) |
                                 word(offsetof(rtfault_record, tb_lo));
        const std::time_t when = static_cast<std::time_t>(kWiiEpoch + tb / ticks);
        char buf[48] = "unknown";
        if (const std::tm* t = std::gmtime(&when)) std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
        text += std::string("When: ") + buf + " (the console's clock)\n";
    }
    text += std::string("What: ") + exception_name(exception) + " (exception " + std::to_string(exception) + ") at " +
            hex(srr0);
    if (exception == RTFAULT_DSI) text += std::string((dsisr & 0x02000000u) ? ", writing to " : ", reading from ") + hex(dar);
    text += "\n";
    text += "SRR0 " + hex(srr0) + "  SRR1 " + hex(word(offsetof(rtfault_record, srr1))) + "  DSISR " + hex(dsisr) +
            "  DAR " + hex(dar) + "\n";
    text += "LR " + hex(word(offsetof(rtfault_record, lr))) + "  CR " + hex(word(offsetof(rtfault_record, cr))) +
            "  CTR " + hex(word(offsetof(rtfault_record, ctr))) + "  XER " + hex(word(offsetof(rtfault_record, xer))) +
            "\n";
    for (unsigned r = 0; r < 32; r += 4) {
        char line[96];
        const std::size_t g = offsetof(rtfault_record, gpr);
        std::snprintf(line, sizeof(line), "R%02u %08X  R%02u %08X  R%02u %08X  R%02u %08X\n", r, word(g + r * 4),
                      r + 1, word(g + r * 4 + 4), r + 2, word(g + r * 4 + 8), r + 3, word(g + r * 4 + 12));
        text += line;
    }
    std::uint32_t frames = word(offsetof(rtfault_record, frame_count));
    if (frames > RTFAULT_FRAMES) frames = RTFAULT_FRAMES;
    text += "Stack:";
    for (std::uint32_t i = 0; i < frames; ++i) text += " " + hex(word(offsetof(rtfault_record, frames) + i * 4));
    text += frames == 0 ? " (unreadable)\n" : "\n";
    if (word(offsetof(rtfault_record, code_count)) == RTFAULT_CODE_WORDS) {
        text += "Code at " + hex(srr0 - 16) + ":";
        for (std::uint32_t i = 0; i < RTFAULT_CODE_WORDS; ++i) {
            const std::string w = hex(word(offsetof(rtfault_record, code) + i * 4));
            text += i == 4 ? " [" + w + "]" : " " + w;
        }
        text += "\n";
    }
    error.clear();
    return true;
}

}  // namespace riftwii
