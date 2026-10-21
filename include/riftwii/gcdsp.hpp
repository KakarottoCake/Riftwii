// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace riftwii {

// A GameCube sound program (DSP microcode) the game carries, patched so its
// sample reader looks in MEM2, where the loader keeps ARAM (gcnative.cpp).
// Which programs, and their lengths and SHA-1 hashes, follows Nintendont's
// DSP table (kernel/Patch.c, FIX94 and the Nintendont contributors, used
// with permission; NOTICE.md): the values are theirs, the code is RiftWii's
// own. A program is recognised by its first 16 bytes (its jump table), its
// length and its SHA-1, never by the jump words alone: the two start kinds
// below share jump words with programs that must not match.
//
// There are two kinds of program. The simple kind (Super Mario Sunshine,
// Luigi's Mansion) loads its sample address twice with the same two-word
// instruction. The patch puts `ori $AC0.M, #0x1000`, those two words and
// `ret, ret` into six unused words and calls them from both places. Both
// call sites hold the same two words, so one copy serves them. That fixed
// bit is right only for one sample format.
//
// The AX kind (Melee, Sonic Mega Collection, Mario Party 4 and 5, Beyond Good and Evil,
// Star Fox Assault, Paper Mario: The Thousand-Year Door) carries three
// copies of a longer sample-address routine (dup1, dup2, dup3). It cannot
// take the fixed bit: the DSP's accelerator keeps the address in the
// samples' own units, nibbles for 4-bit samples, bytes or words for wider
// ones depending on the format, so the bit that means MEM2 (0x1000 in bytes)
// lands on a different bit of the register for each format. The patch reads
// the format (@SampleFormat), keeps its low two bits and builds the bit
// from them, setting it alongside the loaded address instead of forcing one
// fixed bit.
//
// What the AX rewrite writes, in DSP words (16-bit, big-endian; a word
// address is a byte offset halved):
//   - At dup1 and dup2, `call dup1+4` then `jmp dup+0x22`: the 0x22 words at
//     dup1 are moved out of the way and both copies run the moved code.
//   - The moved 0x22 words go in two halves: the upper source words to
//     [dup2+4, dup2+0x17), the lower ones to [dup1+4, dup1+0x20), joined by
//     a `jmp dup2+4` at dup1+0x1E. While moving, two sample-format reads
//     are rewritten in place: `lrri $AC0.M srs @SampleFormat, $AC0.M`
//     (0x195E2ED1) becomes a seven-word read of the format into $AC1.M
//     (`andi #0x0003`, clear, `lri #0x2000`, `lsrn`, `mrr`) with the load
//     retargeted at $AC1.M, and after each `srs @ACxAH, $AC0.M` an `orc
//     $AC0.M, $AC1.M` word (0x3E00) is slipped between its two words, so
//     the loaded address is ORed with the format-built bit.
//   - A `ret` (0x02DF) at dup2+0x16 ends the moved tail; the 11 words at
//     dup2+0x17 (Dup2PAddr) are a helper the two redirected loads call: the
//     original pair from dup1+0x98 around a `lrs $AC1.M, @SampleFormat` /
//     `andi #0x0003` / clear / `lri #0x1FFF` / `lsrn` / `andc` sequence and
//     a `ret`. The loads at dup1+0x98 and dup2+dup2_call become calls to it.
//   - Dup3, when present, gets its own 12-word helper at dup3+4 (the same
//     format sequence around its original pairs from dup3+0x5D and
//     dup3+0x5F), its first words become `call dup1+4` / `jmp dup3+0x22`,
//     and the load at dup3+0x5D becomes a `jmp dup3+4` to that helper.
//
// DSP words below are byte offsets halved; dup3 is 0xFFFF when a program
// has only two copies, and dup2_call is counted in words from dup2.
enum class GcDspKind { Simple, Ax };

struct GcDspSimpleFix {
    std::uint16_t free_at = 0;  // six unused words
    std::uint16_t calls[2] = {};  // the two loads of the sample address
};

struct GcDspAxFix {
    std::uint16_t dup1 = 0;
    std::uint16_t dup2 = 0;
    std::uint16_t dup3 = 0;      // 0xFFFF: only two copies
    std::uint16_t dup2_call = 0;  // words past dup2 of the redirected load
};

struct GcDspFix {
    const char* name = nullptr;
    std::uint32_t length = 0;  // bytes
    std::array<std::uint8_t, 16> pattern = {};
    std::array<std::uint8_t, 20> sha1 = {};
    GcDspKind kind = GcDspKind::Simple;
    GcDspSimpleFix simple;
    GcDspAxFix ax;
};

constexpr std::uint16_t kGcDspNoDup3 = 0xFFFF;

// The known programs, simple ones first.
std::size_t gcdsp_fix_count();
const GcDspFix& gcdsp_fix_at(std::size_t index);

// Whether the bytes at `program` (of which `available` are readable) are
// the fix's program: its 16 pattern bytes match, `available` holds the
// whole length, and its SHA-1 matches. False otherwise.
bool gcdsp_matches(const std::uint8_t* program, std::size_t available, const GcDspFix& fix);

// The known program at `program`, or null when none matches.
const GcDspFix* gcdsp_find(const std::uint8_t* program, std::size_t available);

// The simple rewrite on `program` (of `available` bytes): the ori plus the
// two call sites' words into the free words, both sites calling them.
// False when anything it touches lies past `available`.
bool gcdsp_apply_simple(std::uint8_t* program, std::size_t available, std::uint16_t free_at,
                        std::uint16_t call_a, std::uint16_t call_b);

// The AX rewrite on `program`: the move and helpers above for dup1, dup2,
// dup3 (kGcDspNoDup3 for none) and the load dup2_call words past dup2.
// False when anything it touches lies past `available`.
bool gcdsp_apply_ax(std::uint8_t* program, std::size_t available, std::uint16_t dup1, std::uint16_t dup2,
                    std::uint16_t dup3, std::uint16_t dup2_call);

// The fix's rewrite (simple or AX) on `program`. False when it does not
// fit, and then `program` is left as it was.
bool gcdsp_apply(const GcDspFix& fix, std::uint8_t* program, std::size_t available);

}  // namespace riftwii
