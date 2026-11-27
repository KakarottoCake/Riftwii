// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The controller finders' shared reading of PowerPC code (hbmsearch.cpp):
// where a framed function ends, which instructions write which register,
// and RegTrack, one forward scan's knowledge of what each register holds.
// Every finder that follows an argument or a table's address reads code the
// same way, so a rule learned once (a call clobbers r0 and r3-r12, the
// register save helpers don't, an early return's epilogue doesn't end what
// the code after its branch sees) holds in all of them.
#ifndef RIFTWII_CODESCAN_HPP
#define RIFTWII_CODESCAN_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include "riftwii/byteorder.hpp"
#include "riftwii/hbmsearch.hpp"

namespace riftwii {
namespace codescan {

inline std::int32_t simm(std::uint32_t insn) { return static_cast<std::int16_t>(insn & 0xFFFFu); }
inline std::uint32_t opcode(std::uint32_t insn) { return insn >> 26; }
inline std::uint32_t reg_d(std::uint32_t insn) { return (insn >> 21) & 31u; }
inline std::uint32_t reg_a(std::uint32_t insn) { return (insn >> 16) & 31u; }

constexpr std::uint32_t kBlr = 0x4E800020u;
constexpr std::uint32_t kBctr = 0x4E800420u;
constexpr std::uint32_t kNop = 0x60000000u;
constexpr std::uint32_t kMtlrR0 = 0x7C0803A6u;
constexpr std::uint32_t kVolatileRegisters = 1u | (0x3FFu << 3);  // r0, r3-r12

inline bool is_stwu_r1(std::uint32_t insn) { return (insn & 0xFFFF0000u) == 0x94210000u; }  // stwu r1,d(r1)
inline bool is_call(std::uint32_t insn) { return opcode(insn) == 18 && (insn & 3u) == 1u; }    // bl

// Whether `insn` writes register `reg`, for the common integer instructions.
bool writes_register(std::uint32_t insn, std::uint32_t reg);

// Where a framed function that starts at word `start` ends, before word
// `next` (the next `stwu r1`): after its last `blr` or tail call that follows
// the frame's undoing, else after its first `blr`, else at `next`.
std::size_t function_end(const std::uint8_t* bytes, std::size_t start, std::size_t next);

// Whether the word at `k` (before word `end`) restores a register in an
// epilogue: a load off r1 followed, through nothing but more such loads and
// `mtlr`, by the frame's undoing and then a return or tail call.
bool epilogue_restore(const std::uint8_t* bytes, std::size_t k, std::size_t end);

// Whether the call at word `k` ends an error path: only the return value set
// (`li`, `mr`, `nop`) before the function leaves. The code a branch around
// it reaches never runs its call, so the call clobbers nothing there.
bool call_then_returns(const std::uint8_t* bytes, std::size_t k, std::size_t end);

// Calls `visit(range, start, end)` (word indexes) for each framed function
// in `text`: from its `stwu r1` to where it ends (function_end), if that is
// at most `max_words` long.
template <typename Visit>
void each_framed_function(const std::vector<CodeRange>& text, std::size_t max_words, Visit visit) {
    for (const CodeRange& r : text) {
        if (r.bytes == nullptr) continue;
        const std::size_t words = r.size / 4;
        std::size_t i = 0;
        while (i < words) {
            while (i < words && !is_stwu_r1(be32(r.bytes + 4 * i))) ++i;
            if (i >= words) break;
            const std::size_t start = i++;
            while (i < words && !is_stwu_r1(be32(r.bytes + 4 * i))) ++i;
            // To its last return after the frame is undone (an early error
            // return doesn't end it), else its first.
            const std::size_t end = function_end(r.bytes, start, i);
            if (end - start <= max_words) visit(r, start, end);
        }
    }
}

// What each register holds as one forward scan of the code from word `start`
// to word `end` reads on: the table addresses being built (`lis`, then
// `addi` from it) and, for each argument register the finder follows, the
// registers holding its value (it and its `mr` copies). Read a word's
// registers as it finds them, then step() past it:
//   RegTrack t(text, range, start, end, RegTrack::kAddresses);
//   t.follow(4);
//   for (k = start; k < end; ++k) { ... t.copies(4), t.address(a) ...; t.step(k); }
// A register written loses what it held; a call clobbers r0 and r3-r12
// (not a register save helper's, nor one that ends an error path); an
// epilogue's restore leaves things as they were (the scan reads straight
// through an early return into the code its branch reaches).
class RegTrack {
public:
    static constexpr unsigned kArguments = 1u;  // follow() registers' copies
    static constexpr unsigned kAddresses = 2u;  // lis/addi-built addresses

    // A function in `range` of the game's code `text` (a call's target is
    // looked up across all of it).
    RegTrack(const std::vector<CodeRange>& text, const CodeRange& range, std::size_t start, std::size_t end,
             unsigned what);
    // `bytes` holding `words` words (a call's target is looked up within them).
    RegTrack(const std::uint8_t* bytes, std::size_t words, std::size_t start, std::size_t end, unsigned what);

    // From here on, follow what `reg` holds now (an argument, a loaded slot).
    void follow(unsigned reg);
    // The registers (one bit each) holding what `reg` held when follow(reg) began.
    std::uint32_t copies(unsigned reg) const { return copies_[reg & 31u]; }
    // The address `reg` holds, built with `lis` and `addi`; 0: not known.
    std::uint32_t address(unsigned reg) const { return address_[reg & 31u]; }
    // Whether the word at `k` changes `reg`: writes it, or is a call that
    // may (r0, r3-r12; not a save helper's, nor one ending an error path).
    bool clobbers(std::size_t k, unsigned reg) const;
    // The word at `k` done.
    void step(std::size_t k);
    // Whether the word at `k` calls (`bl`, `bcl`, `bctrl`, `blrl` and their
    // forms), the callee free to change r0 and r3-r12: not a call to the
    // compiler's register save or restore helpers (`addi r11,r1,N` just
    // before it, and its target saving or restoring through r11).
    bool calls(std::size_t k) const;

private:
    const std::uint8_t* bytes_;
    std::size_t words_;
    const std::vector<CodeRange>* text_ = nullptr;  // the game's code, for a call's target elsewhere
    std::uint32_t base_ = 0;                         // bytes_' address
    std::size_t start_, end_;
    unsigned what_;
    std::uint32_t followed_ = 0;  // registers whose copies are followed, one bit each
    std::uint32_t copies_[32] = {};
    std::uint32_t high_[32] = {};
    std::uint32_t address_[32] = {};
};

}  // namespace codescan
}  // namespace riftwii

#endif
