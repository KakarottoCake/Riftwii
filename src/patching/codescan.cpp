// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The controller finders' shared reading of PowerPC code (codescan.hpp).
#include "codescan.hpp"

namespace riftwii {
namespace codescan {

// Whether `insn` writes register `reg`, for the common integer instructions:
// the D-form arithmetic and loads write rD; the logical immediates and the
// rotates write rA; among the opcode-31 forms, the logical and shift ones
// write rA, the stores write nothing, and the rest (arithmetic, indexed
// loads, mfspr) write rD.
bool writes_register(std::uint32_t insn, std::uint32_t reg) {
    const std::uint32_t op = opcode(insn);
    // The update forms (lwzu, lbzu, lhzu, lhau and the stores stwu, stbu,
    // sthu) also write their base, rA.
    const bool update = op == 33 || op == 35 || op == 37 || op == 39 || op == 41 || op == 43 || op == 45;
    if (update && reg_a(insn) == reg) return true;
    if (op == 46) return reg >= reg_d(insn);  // lmw: rD through r31
    const bool d_form = op == 7 || op == 8 || op == 12 || op == 13 || op == 14 || op == 15 ||
                        (op >= 32 && op <= 35) || (op >= 40 && op <= 43);  // mulli..addis, the loads
    if (d_form) return reg_d(insn) == reg;
    const bool a_form = op == 20 || op == 21 || op == 23 || (op >= 24 && op <= 29);  // rlwimi..andis.
    if (a_form) return reg_a(insn) == reg;
    if (op != 31) return false;
    const std::uint32_t xo = (insn >> 1) & 0x3FFu;
    switch (xo) {
        case 28: case 60: case 124: case 284: case 316: case 412: case 444: case 476:  // and..nand
        case 24: case 536: case 792: case 824: case 26: case 922: case 954:            // shifts, cntlzw, extsh/b
            return reg_a(insn) == reg;
        case 183: case 247: case 439:  // stwux, stbux, sthux: their base
            return reg_a(insn) == reg;
        case 55: case 119: case 311: case 375:  // lwzux, lbzux, lhzux, lhaux: rD and their base
            return reg_d(insn) == reg || reg_a(insn) == reg;
        case 151: case 215: case 407: case 662: case 918: case 150:  // the other stores
            return false;
        case 0: case 32: case 4:                          // cmp, cmpl, tw: compare only
        case 144: case 146: case 467:                     // mtcrf, mtmsr, mtspr: rS is read
        case 54: case 86: case 470: case 982: case 1014:  // dcbst, dcbf, dcbi, icbi, dcbz
            return false;
        default:
            return reg_d(insn) == reg;
    }
}

// Where a framed function that starts at word `start` ends, before word
// `next` (the next `stwu r1`): after its last `blr`, or tail call (a `b` out
// of the function or a `bctr`), that follows a stack frame's undoing (`addi
// r1,r1,N` or `lwz r1,...` within the four words before, with no other
// return or tail call between), else after its first `blr`, else at `next`.
// Leaf code (no frame of its own) between it and the next framed function
// isn't counted, so its instructions can't lend the function marks it
// doesn't have.
std::size_t function_end(const std::uint8_t* bytes, std::size_t start, std::size_t next) {
    // A tail call: `bctr`, or `b` (no link) to somewhere outside start..next.
    const auto tail_call = [&](std::size_t k) {
        const std::uint32_t insn = be32(bytes + 4 * k);
        if (insn == kBctr) return true;
        if (opcode(insn) != 18 || (insn & 1u) != 0) return false;
        if ((insn & 2u) != 0) return true;  // absolute: never this function's own code
        std::int32_t offset = static_cast<std::int32_t>(insn & 0x03FFFFFCu);
        if (offset & 0x02000000) offset -= 0x04000000;
        const std::int64_t target = static_cast<std::int64_t>(k) + offset / 4;
        return target < static_cast<std::int64_t>(start) || target >= static_cast<std::int64_t>(next);
    };
    std::size_t last_framed = 0, first = 0;
    for (std::size_t k = start; k < next; ++k) {
        const std::uint32_t word = be32(bytes + 4 * k);
        const bool ret = word == kBlr;
        // A plain `b` within start..next counts too, but only just after the
        // frame's undoing (below): a branch to a shared epilogue comes
        // before the frame is undone, while one after it can only be a tail
        // call, here to a leaf placed before the next framed function
        // (CodeRabbit on #616).
        const bool near_b = opcode(word) == 18 && (word & 3u) == 0u;
        if (!ret && !tail_call(k) && !near_b) continue;
        if (ret && first == 0) first = k + 1;
        for (std::size_t back = 1; back <= 4 && back <= k - start; ++back) {
            const std::uint32_t insn = be32(bytes + 4 * (k - back));
            // Another function's return or tail call: its restore isn't this
            // one's; nor across a plain `b`, which leaves this path (a
            // restore before it belongs to where it went; CodeRabbit CLI).
            if (insn == kBlr || tail_call(k - back)) break;
            if (opcode(insn) == 18 && (insn & 1u) == 0) break;
            const bool restore = (opcode(insn) == 14 && reg_d(insn) == 1 && reg_a(insn) == 1) ||  // addi r1,r1,N
                                 (opcode(insn) == 32 && reg_d(insn) == 1);                         // lwz r1,...
            if (restore) {
                last_framed = k + 1;
                break;
            }
        }
    }
    return last_framed != 0 ? last_framed : first != 0 ? first : next;
}

// Whether the word at `k` (before word `end`) restores a register in an
// epilogue: a load off r1 (`lwz`/`lmw`) followed, through nothing but more
// such loads and `mtlr`, by the frame's undoing (`addi r1,r1,N`, N > 0) and
// then a return or tail call, within a few words. An ordinary reload
// through the stack isn't one.
bool epilogue_restore(const std::uint8_t* bytes, std::size_t k, std::size_t end) {
    constexpr std::size_t kWithin = 8;
    const auto restore_load = [](std::uint32_t w) { return (opcode(w) == 32 || opcode(w) == 46) && reg_a(w) == 1; };
    if (!restore_load(be32(bytes + 4 * k))) return false;
    for (std::size_t j = k + 1; j < end && j <= k + kWithin; ++j) {
        const std::uint32_t w = be32(bytes + 4 * j);
        if (opcode(w) == 14 && reg_d(w) == 1 && reg_a(w) == 1 && simm(w) > 0) {
            // The frame undone, then the function leaves (a return or a
            // tail call, maybe after reloading the return address off the
            // caller's frame and mtlr): only then is it an epilogue.
            for (std::size_t n = j + 1; n < end && n <= j + 4; ++n) {
                const std::uint32_t x = be32(bytes + 4 * n);
                if (x == kBlr || x == 0x4E800420u) return true;
                if (opcode(x) == 18 && (x & 1u) == 0) {
                    // A `b` is a tail call only if it leaves the code being
                    // read: absolute, or to before the restore or past `end`;
                    // a branch further into the function isn't an epilogue.
                    if ((x & 2u) != 0) return true;
                    std::int32_t offset = static_cast<std::int32_t>(x & 0x03FFFFFCu);
                    if (offset & 0x02000000) offset -= 0x04000000;
                    const std::int64_t target = static_cast<std::int64_t>(n) + offset / 4;
                    return target < static_cast<std::int64_t>(k) || target >= static_cast<std::int64_t>(end);
                }
                if ((x & 0xFC1FFFFFu) != 0x7C0803A6u && !restore_load(x)) return false;
            }
            return false;
        }
        const bool mtlr = (w & 0xFC1FFFFFu) == 0x7C0803A6u;
        if (!restore_load(w) && !mtlr) return false;
    }
    return false;
}

// Whether `first`, the first word a call reaches, starts the compiler's
// register save or restore helpers (_savegpr_N, _restgpr_N): a `stw`,
// `lwz`, `stfd` or `lfd` through r11.
static bool helper_word(std::uint32_t first) {
    const std::uint32_t op = opcode(first);
    return reg_a(first) == 11 && (op == 36 || op == 32 || op == 54 || op == 50);
}

bool RegTrack::calls(std::size_t k) const {
    const std::uint32_t insn = be32(bytes_ + 4 * k);
    if ((insn & 1u) == 0) return false;
    if (opcode(insn) == 18) {
        // `bl +4; mflr rD`: the code's own address taken, nothing called
        // (CodeRabbit CLI, rf6).
        if ((insn & 0x03FFFFFEu) == 4u && k + 1 < words_ && (be32(bytes_ + 4 * (k + 1)) & 0xFC1FFFFFu) == 0x7C0802A6u)
            return false;
        // `bl` right after `addi r11,r1,N`: a register save helper's call
        // only if its target is one, looked up wherever it is in the game's
        // code (another section included). The `addi` alone isn't proof, and
        // a target found nowhere is an ordinary call (CodeRabbit CLI). A `bl`
        // at the first word read has no `addi` before it: an ordinary call.
        const std::uint32_t prev = k > 0 && (insn & 2u) == 0 ? be32(bytes_ + 4 * (k - 1)) : 0;
        if (opcode(prev) == 14 && reg_d(prev) == 11 && reg_a(prev) == 1) {
            std::int32_t offset = static_cast<std::int32_t>(insn & 0x03FFFFFCu);
            if (offset & 0x02000000) offset -= 0x04000000;
            const std::int64_t index = static_cast<std::int64_t>(k) + offset / 4;
            std::uint32_t first = 0;
            if (index >= 0 && index < static_cast<std::int64_t>(words_)) {
                first = be32(bytes_ + 4 * static_cast<std::size_t>(index));
                if (helper_word(first)) return false;
            } else if (text_ != nullptr) {
                const std::uint32_t target = base_ + static_cast<std::uint32_t>(4 * k) + static_cast<std::uint32_t>(offset);
                for (const CodeRange& r : *text_) {
                    if (r.bytes == nullptr || target < r.address || target - r.address + 4 > r.size) continue;
                    if (helper_word(be32(r.bytes + (target - r.address)))) return false;
                    break;
                }
            }
        }
        return true;
    }
    if (opcode(insn) == 16) return true;
    const std::uint32_t xo = (insn >> 1) & 0x3FFu;
    return opcode(insn) == 19 && (xo == 16 || xo == 528);
}

// Whether the call at word `k` ends an error path: after it only the
// return value set (`li`, `mr`, a `nop`) before the function leaves (an
// epilogue's restores and return, or a plain `blr`). The scan reads straight
// through such a path, and the code a branch around it reaches never runs
// its call, so the call clobbers nothing there (CodeRabbit CLI, #616); a
// call followed by any other work is an ordinary one.
bool call_then_returns(const std::uint8_t* bytes, std::size_t k, std::size_t end) {
    constexpr std::size_t kWithin = 8;
    for (std::size_t j = k + 1; j < end && j <= k + kWithin; ++j) {
        if (epilogue_restore(bytes, j, end)) return true;
        const std::uint32_t w = be32(bytes + 4 * j);
        if (w == kBlr) return true;
        const bool li = opcode(w) == 14 && reg_a(w) == 0;
        const bool mr = opcode(w) == 31 && ((w >> 1) & 0x3FFu) == 444 && reg_d(w) == ((w >> 11) & 31u);
        if (!li && !mr && w != 0x60000000u) return false;
    }
    return false;
}

// Whether the word at `k` is a `li` or `mr` on a path that then only leaves
// the function (nothing but more of them before an epilogue or `blr`,
// call_then_returns from there): an early error return such as `li r3,-1;
// mr r5,r3; blr`. The scan reads straight through it into the code a branch
// around it reaches, where nothing it did happened: no register loses what
// it held, and none becomes a copy (CodeRabbit CLI).
static bool return_value_write(const std::uint8_t* bytes, std::size_t k, std::size_t end) {
    const std::uint32_t w = be32(bytes + 4 * k);
    const bool li = opcode(w) == 14 && reg_a(w) == 0;
    const bool mr = opcode(w) == 31 && ((w >> 1) & 0x3FFu) == 444 && reg_d(w) == ((w >> 11) & 31u);
    return (li || mr) && call_then_returns(bytes, k, end);
}

// `mask` (registers holding an argument, one bit each) after the word at
// `k` (before word `end`): a register it writes no longer holds it, unless
// the write is an `mr` from one that does, and a call leaves it only in the
// registers a callee keeps (r13-r31: CodeRabbit on #616). A register
// restored in an epilogue (epilogue_restore) is left as it was: the scan
// reads straight through an early return's epilogue into the code its
// branch around it reaches, where the register still holds the argument.
static std::uint32_t track_argument(const std::uint8_t* bytes, std::size_t k, std::size_t end, bool call,
                                    std::uint32_t mask) {
    const std::uint32_t insn = be32(bytes + 4 * k);
    if (epilogue_restore(bytes, k, end) || return_value_write(bytes, k, end)) return mask;
    if (call && !call_then_returns(bytes, k, end)) return mask & ~kVolatileRegisters;
    const bool mr = opcode(insn) == 31 && ((insn >> 1) & 0x3FFu) == 444 && (insn & 1u) == 0 &&
                    reg_d(insn) == ((insn >> 11) & 31u);
    const bool copies = mr && ((mask >> reg_d(insn)) & 1u);
    for (std::uint32_t reg = 0; reg < 32; ++reg)
        if (((mask >> reg) & 1u) && writes_register(insn, reg)) mask &= ~(1u << reg);
    if (copies) mask |= 1u << reg_a(insn);
    return mask;
}

// The table addresses being built (`lis` high halves and `addi`-completed
// addresses, per register) after `insn`: what it builds is read from the
// values before it, then every register it writes (a call: r0, r3-r12)
// forgets what it held (CodeRabbit on #616).
static void track_addresses(std::uint32_t insn, bool call_counts, std::uint32_t high[32], std::uint32_t address[32]) {
    const std::uint32_t d = reg_d(insn), a = reg_a(insn);
    std::uint32_t new_high = 0, new_address = 0;
    if (opcode(insn) == 15 && a == 0) new_high = (insn & 0xFFFFu) << 16;                  // lis
    if (opcode(insn) == 14 && a != 0 && high[a] != 0)                                     // addi rD,rA,lo
        new_address = high[a] + static_cast<std::uint32_t>(simm(insn));
    for (std::uint32_t g = 0; g < 32; ++g) {
        if ((call_counts && ((kVolatileRegisters >> g) & 1u)) || writes_register(insn, g)) {
            high[g] = 0;
            address[g] = 0;
        }
    }
    if (new_high != 0) high[d] = new_high;
    if (new_address != 0) address[d] = new_address;
}

RegTrack::RegTrack(const std::vector<CodeRange>& text, const CodeRange& range, std::size_t start, std::size_t end,
                   unsigned what)
    : RegTrack(range.bytes, range.size / 4, start, end, what) {
    text_ = &text;
    base_ = range.address;
}

RegTrack::RegTrack(const std::uint8_t* bytes, std::size_t words, std::size_t start, std::size_t end, unsigned what)
    : bytes_(bytes), words_(words), start_(start), end_(end), what_(what) {}

void RegTrack::follow(unsigned reg) {
    reg &= 31u;
    followed_ |= 1u << reg;
    copies_[reg] = 1u << reg;
}

bool RegTrack::clobbers(std::size_t k, unsigned reg) const {
    const std::uint32_t insn = be32(bytes_ + 4 * k);
    reg &= 31u;
    if (writes_register(insn, reg)) return true;
    return ((kVolatileRegisters >> reg) & 1u) && calls(k) && !call_then_returns(bytes_, k, end_);
}

void RegTrack::step(std::size_t k) {
    const bool call = calls(k);
    if (what_ & kArguments) {
        for (unsigned reg = 0; reg < 32; ++reg)
            if ((followed_ >> reg) & 1u) copies_[reg] = track_argument(bytes_, k, end_, call, copies_[reg]);
    }
    if ((what_ & kAddresses) && !epilogue_restore(bytes_, k, end_) && !return_value_write(bytes_, k, end_)) {
        track_addresses(be32(bytes_ + 4 * k), call && !call_then_returns(bytes_, k, end_), high_, address_);
    }
}

}  // namespace codescan
}  // namespace riftwii
