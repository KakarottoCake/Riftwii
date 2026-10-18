// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcclock.hpp"

#include <cstring>

namespace riftwii {

namespace {

constexpr std::uint32_t kOpcodeAddi = 14;
constexpr std::uint32_t kOpcodeAddis = 15;  // lis rD,value is addis rD,0,value
constexpr std::uint32_t kOpcodeOri = 24;
constexpr unsigned kMaxGap = 4;  // instructions from the upper half to the lower

std::uint32_t opcode(std::uint32_t word) { return word >> 26; }
std::uint32_t field_d(std::uint32_t word) { return (word >> 21) & 31; }
std::uint32_t field_a(std::uint32_t word) { return (word >> 16) & 31; }

bool is_lis(std::uint32_t word) { return opcode(word) == kOpcodeAddis && field_a(word) == 0; }

// Branches of every kind (b, bc, and bclr/bcctr in opcode 19). Opcode 19
// also holds the CR operations, such as the `crclr 6` the compiler puts
// before every call with variable arguments; they end the search too,
// which only costs a fix, never makes a wrong one.
bool is_branch(std::uint32_t word) {
    const std::uint32_t op = opcode(word);
    return op == 16 || op == 18 || op == 19;
}

// The GameCube clock `value` stands for, as the running console's.
// 0 when it is none of them.
std::uint32_t running_value_of(std::uint32_t value, const GcClocks& running) {
    if (value == kGameCubeClocks.bus) return running.bus;
    if (value == kGameCubeClocks.cpu) return running.cpu;
    if (value == gc_time_base_hz(kGameCubeClocks)) return gc_time_base_hz(running);
    return 0;
}

// The value `lis` and the lower-half instruction build together.
std::uint32_t built_value(std::uint32_t lis, std::uint32_t low) {
    const std::uint32_t high = (lis & 0xFFFF) << 16;
    if (opcode(low) == kOpcodeOri) return high | (low & 0xFFFF);
    const std::int32_t signed_low = static_cast<std::int16_t>(low & 0xFFFF);
    return high + static_cast<std::uint32_t>(signed_low);
}

// Rewrites the immediates of the pair so that it builds `value`. addi adds
// its lower half sign-extended, so the upper half goes up by one when the
// lower half's top bit is set.
void rebuild(GcClockFix& fix, std::uint32_t lis, std::uint32_t low, std::uint32_t value) {
    std::uint32_t high = value >> 16;
    if (opcode(low) == kOpcodeAddi && (value & 0x8000) != 0) high = (high + 1) & 0xFFFF;
    fix.high_word = (lis & 0xFFFF0000) | high;
    fix.low_word = (low & 0xFFFF0000) | (value & 0xFFFF);
}

}  // namespace

GcClocks gc_clocks_from_hw_clocks(std::uint32_t hw_clocks) {
    const bool gamecube_speed = (hw_clocks & kHwClocksGameCubeSpeed) != 0;
    return gamecube_speed ? kGameCubeClocks : kWiiClocks;
}

std::vector<GcClockFix> find_gc_clock_values(const GcCodeReader& read, const std::vector<GcTextRange>& text,
                                             const GcClocks& running) {
    std::vector<GcClockFix> fixes;
    const bool already_gamecube = running.bus == kGameCubeClocks.bus && running.cpu == kGameCubeClocks.cpu;
    if (already_gamecube) return fixes;

    for (const GcTextRange& range : text) {
        for (std::uint32_t at = range.start; at + 4 <= range.end; at += 4) {
            std::uint32_t lis = 0;
            if (!read(at, lis) || !is_lis(lis)) continue;
            const std::uint32_t reg = field_d(lis);

            for (unsigned step = 1; step <= kMaxGap; ++step) {
                const std::uint32_t low_at = at + 4 * step;
                std::uint32_t low = 0;
                if (low_at + 4 > range.end || !read(low_at, low)) break;

                // addi with rA 0 adds to the number 0, not to r0: it is
                // `li`, which replaces the upper half rather than finishing
                // it. So a value built in r0 is only taken with ori.
                const bool addi_finishes = opcode(low) == kOpcodeAddi && reg != 0;
                const bool lower_half = (opcode(low) == kOpcodeOri || addi_finishes) && field_d(low) == reg &&
                                        field_a(low) == reg;
                if (lower_half) {
                    const std::uint32_t value = built_value(lis, low);
                    const std::uint32_t replacement = running_value_of(value, running);
                    if (replacement != 0) {
                        GcClockFix fix;
                        fix.high_at = at;
                        fix.low_at = low_at;
                        fix.old_value = value;
                        fix.new_value = replacement;
                        rebuild(fix, lis, low, replacement);
                        fixes.push_back(fix);
                    }
                    break;
                }
                // Anything naming the register in its first two fields
                // ends the search for this pair: most instructions write
                // the first, the logical ones and rotates (mr, rlwinm)
                // the second. Loads and stores that only read it there
                // end it too, which costs nothing as they are rare
                // between a constant's halves. So does a branch away.
                if (field_d(low) == reg || field_a(low) == reg || is_branch(low)) break;
            }
        }
    }
    return fixes;
}

std::vector<GcClockConstant> find_gc_clock_constants(const GcCodeReader& read, const std::vector<GcTextRange>& data) {
    const std::uint32_t clocks_hz[] = {kGameCubeClocks.bus, kGameCubeClocks.cpu, gc_time_base_hz(kGameCubeClocks)};

    // The big-endian words each clock has as a float, and as a double's
    // upper and lower halves.
    struct Encoding {
        std::uint32_t hz;
        std::uint32_t float_word;
        std::uint32_t double_high;
        std::uint32_t double_low;
    };
    std::vector<Encoding> encodings;
    for (std::uint32_t hz : clocks_hz) {
        const float as_float = static_cast<float>(hz);
        const double as_double = static_cast<double>(hz);
        std::uint32_t float_word = 0;
        std::uint64_t double_bits = 0;
        std::memcpy(&float_word, &as_float, sizeof(float_word));
        std::memcpy(&double_bits, &as_double, sizeof(double_bits));
        encodings.push_back({hz, float_word, static_cast<std::uint32_t>(double_bits >> 32),
                             static_cast<std::uint32_t>(double_bits)});
    }

    std::vector<GcClockConstant> found;
    for (const GcTextRange& range : data) {
        for (std::uint32_t at = range.start; at + 4 <= range.end; at += 4) {
            std::uint32_t word = 0;
            if (!read(at, word)) continue;
            for (const Encoding& e : encodings) {
                if (word == e.float_word) found.push_back({at, false, e.hz});
                if (word != e.double_high || at + 8 > range.end) continue;
                std::uint32_t next = 0;
                if (read(at + 4, next) && next == e.double_low) found.push_back({at, true, e.hz});
            }
        }
    }
    return found;
}

}  // namespace riftwii
