// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/hbmsearch.hpp"
#include "riftwii/byteorder.hpp"

#include "codescan.hpp"

#include <algorithm>
#include <cstring>

namespace riftwii {
using namespace codescan;
namespace {




// The word at `address` in any of `ranges`.
bool word_at(const std::vector<CodeRange>& ranges, std::uint32_t address, std::uint32_t& out) {
    for (const CodeRange& r : ranges) {
        if (r.bytes == nullptr || address < r.address || address - r.address + 4 > r.size) continue;
        out = be32(r.bytes + (address - r.address));
        return true;
    }
    return false;
}

// The function holding the word at `address`: its `stwu r1` going back, or
// the first word after the `blr` that ends the function before it (a leaf
// with no frame), past padding. 0 when neither comes within `limit` words.
std::uint32_t function_start(const std::vector<CodeRange>& text, std::uint32_t address, unsigned limit) {
    for (unsigned back = 0; back <= limit && back * 4 <= address; ++back) {
        std::uint32_t insn = 0;
        if (!word_at(text, address - back * 4, insn)) return 0;
        if (is_stwu_r1(insn)) return address - back * 4;
        if (back == 0 || insn != kBlr) continue;
        std::uint32_t first = address - back * 4 + 4;
        std::uint32_t next = 0;
        while (first < address && word_at(text, first, next) && (next == 0 || next == kNop)) first += 4;
        return first;
    }
    return 0;
}

// Calls `visit(address, insn)` for every word from `start` to its function's
// `blr` (at most `limit` words); stops early when `visit` returns false.
template <typename Visit>
void each_word(const std::vector<CodeRange>& text, std::uint32_t start, unsigned limit, Visit visit) {
    for (unsigned i = 0; i < limit; ++i) {
        std::uint32_t insn = 0;
        if (!word_at(text, start + i * 4, insn)) return;
        if (!visit(start + i * 4, insn)) return;
        if (insn == kBlr) return;
    }
}

// The KPAD status's fields the ring walker stores: the device type and the
// controller's error, as every SDK seen since 2007 lays them out.
constexpr std::int32_t kDevTypeOffset = 0x5C;
constexpr std::int32_t kWpadErrOffset = 0x5D;
// The 2006 KPAD (Excite Truck, Marvel Ultimate Alliance): a 136-byte
// status with the device type as a word at 0x5C, and a byte at 0x85 (the
// error, copied from the WPAD status's) its read stores.
constexpr std::int32_t kStatus2006Bytes = 136;
constexpr std::int32_t kWpadErr2006Offset = 0x85;
constexpr std::int32_t kMinStatusBytes = 0x80;
constexpr std::int32_t kMaxStatusBytes = 0x200;
constexpr unsigned kMinBackSteps = 3;
constexpr unsigned kErrorCopyWithin = 24;  // words from the read's start

// The function right after the one at `start` (up to `limit` words long,
// to its first `blr`), past any padding; 0 when there is none.
std::uint32_t next_function(const std::vector<CodeRange>& text, std::uint32_t start, unsigned limit) {
    std::uint32_t end = 0;
    each_word(text, start, limit, [&](std::uint32_t at, std::uint32_t w) {
        if (w == kBlr) end = at;
        return true;
    });
    if (end == 0) return 0;
    std::uint32_t next = end + 4, insn = 0;
    while (word_at(text, next, insn) && (insn == 0 || insn == kNop) && next < end + 64) next += 4;
    return word_at(text, next, insn) ? next : 0;
}

// Whether the wrapper at `at` keeps r3 (`mr rN,r3`) before its first call:
// it takes an argument, which HBMGetSelectBtnNum doesn't (Dr. Mario Online
// Rx has HBMSetAdjustFlag(BOOL) right after HBMDraw).
bool keeps_argument(const std::vector<CodeRange>& text, std::uint32_t at) {
    bool keeps = false;
    each_word(text, at, 16, [&](std::uint32_t, std::uint32_t w) {
        if (is_call(w)) return false;
        const bool mr_from_r3 = opcode(w) == 31 && ((w >> 1) & 0x3FFu) == 444u && reg_d(w) == 3 && ((w >> 11) & 31u) == 3u;
        keeps = keeps || mr_from_r3;
        return true;
    });
    return keeps;
}

// HBMGetSelectBtnNum, given where it should start: a short wrapper (a
// `stwu`, a call within 16 words, ended within 40, taking no argument;
// 2009 on), or a short leaf that answers -1 while the menu runs (`li
// r3,-1` within 12 words; the 2007 builds: Brawl, Mario Kart Wii, Cruis'n
// USA, and the C++ menu's inner one).
bool looks_like_select(const std::vector<CodeRange>& text, std::uint32_t at) {
    constexpr std::uint32_t kLiR3MinusOne = 0x3860FFFFu;
    std::uint32_t first = 0;
    if (at == 0 || !word_at(text, at, first)) return false;
    bool calls = false, ended = false, minus_one = false;
    unsigned words = 0;
    each_word(text, at, 40, [&](std::uint32_t where, std::uint32_t w) {
        ++words;
        calls = calls || (is_call(w) && where < at + 64);
        minus_one = minus_one || (w == kLiR3MinusOne && where < at + 48);
        if (w == kBlr) ended = true;
        return true;
    });
    if (!ended) return false;
    if (is_stwu_r1(first)) return calls && !keeps_argument(text, at);
    return minus_one && words <= 12;
}

// HBMCalc's answer, where it returns the button the menu ended on (the C++
// HOME Menu, 2008 on: Dr. Mario Online Rx): the leaf it calls last, right
// before its epilogue (`lwz` from the frame, `mtlr r0`, `addi r1`), whose
// r3 it passes back untouched. 0 when the last call isn't such a leaf.
std::uint32_t calc_answer(const std::vector<CodeRange>& text, std::uint32_t calc) {
    std::uint32_t last_call = 0, last_call_insn = 0;
    bool epilogue_only = false;
    each_word(text, calc, 64, [&](std::uint32_t at, std::uint32_t w) {
        if (is_call(w)) {
            last_call = at;
            last_call_insn = w;
            epilogue_only = true;
            return true;
        }
        const bool restores = opcode(w) == 32 && reg_a(w) == 1 && reg_d(w) != 3;  // lwz rN,d(r1)
        const bool frame_end = w == kMtlrR0 || (opcode(w) == 14 && reg_d(w) == 1 && reg_a(w) == 1);
        if (!(restores || frame_end || w == kBlr)) epilogue_only = false;
        return true;
    });
    if (last_call == 0 || !epilogue_only) return 0;
    std::int32_t offset = static_cast<std::int32_t>(last_call_insn & 0x03FFFFFCu);
    if (offset & 0x02000000) offset -= 0x04000000;
    const std::uint32_t leaf = last_call + static_cast<std::uint32_t>(offset);
    std::uint32_t first = 0;
    if (!word_at(text, leaf, first) || is_stwu_r1(first)) return 0;
    return looks_like_select(text, leaf) ? leaf : 0;
}

// The 2006 KPAD's read, where no later one is found: the one function that
// walks a ring of 136-byte statuses backwards (`mulli rD,rA,136` and at
// least three `addi rD,rA,-136`), copies the status's device type as a word
// (`stw` at 0x5C) and stores its error (`stb` at 0x85). Excite Truck
// (0x80047c0c) and Marvel Ultimate Alliance (0x8039e3d4). Three arguments,
// no error's address.
KpadRead find_kpad_read_2006(const std::vector<CodeRange>& text) {
    KpadRead found;
    unsigned matches = 0;
    for (const CodeRange& r : text) {
        if (r.bytes == nullptr) continue;
        const std::size_t words = r.size / 4;
        std::size_t i = 0;
        while (i < words) {
            while (i < words && !is_stwu_r1(be32(r.bytes + 4 * i))) ++i;
            if (i >= words) break;
            const std::size_t start = i++;
            while (i < words && !is_stwu_r1(be32(r.bytes + 4 * i))) ++i;
            bool multiplied = false, dev_type = false, error = false;
            unsigned back_steps = 0;
            const std::size_t end = function_end(r.bytes, start, i);
            for (std::size_t k = start; k < end; ++k) {
                const std::uint32_t insn = be32(r.bytes + 4 * k);
                const std::int32_t value = simm(insn);
                if (opcode(insn) == 7 && value == kStatus2006Bytes) multiplied = true;     // mulli
                if (opcode(insn) == 14 && value == -kStatus2006Bytes) ++back_steps;        // addi
                if (opcode(insn) == 36 && value == kDevTypeOffset) dev_type = true;        // stw
                if (opcode(insn) == 38 && value == kWpadErr2006Offset) error = true;       // stb
            }
            if (!multiplied || back_steps < kMinBackSteps || !dev_type || !error) continue;
            found.address = r.address + static_cast<std::uint32_t>(4 * start);
            found.status_bytes = static_cast<std::uint32_t>(kStatus2006Bytes);
            ++matches;
        }
    }
    return matches == 1 ? found : KpadRead{};
}

}  // namespace

KpadRead find_kpad_read(const std::vector<CodeRange>& text) {
    KpadRead found;
    unsigned matches = 0;
    for (const CodeRange& r : text) {
        if (r.bytes == nullptr) continue;
        const std::size_t words = r.size / 4;
        std::size_t i = 0;
        // One function at a time, from one `stwu r1` to the next.
        while (i < words) {
            while (i < words && !is_stwu_r1(be32(r.bytes + 4 * i))) ++i;
            if (i >= words) break;
            const std::size_t start = i++;
            while (i < words && !is_stwu_r1(be32(r.bytes + 4 * i))) ++i;
            // The sizes multiplied and stepped back by, and the two stores.
            std::int32_t sizes_multiplied[8] = {};
            unsigned multiplied = 0;
            unsigned back_steps[8] = {};
            bool dev_type = false, wpad_err = false;
            for (std::size_t k = start; k < i; ++k) {
                const std::uint32_t insn = be32(r.bytes + 4 * k);
                const std::int32_t value = simm(insn);
                if (opcode(insn) == 7 && value >= kMinStatusBytes && value <= kMaxStatusBytes && multiplied < 8) {
                    sizes_multiplied[multiplied++] = value;  // mulli
                } else if (opcode(insn) == 38) {           // stb
                    dev_type = dev_type || value == kDevTypeOffset;
                    wpad_err = wpad_err || value == kWpadErrOffset;
                }
            }
            if (!dev_type || !wpad_err || multiplied == 0) continue;
            for (std::size_t k = start; k < i; ++k) {
                const std::uint32_t insn = be32(r.bytes + 4 * k);
                if (opcode(insn) != 14) continue;  // addi rD,rA,-S
                for (unsigned m = 0; m < multiplied; ++m)
                    if (simm(insn) == -sizes_multiplied[m]) ++back_steps[m];
            }
            std::int32_t size = 0;
            for (unsigned m = 0; m < multiplied; ++m)
                if (back_steps[m] >= kMinBackSteps) size = sizes_multiplied[m];
            if (size == 0) continue;
            found.address = r.address + static_cast<std::uint32_t>(4 * start);
            found.status_bytes = static_cast<std::uint32_t>(size);
            ++matches;
        }
    }
    if (matches == 0) return find_kpad_read_2006(text);
    if (matches != 1) return KpadRead{};
    // The newer read keeps its fourth argument, the error's address, for
    // later: `mr rN,r6` among its first words, next to its copies of r3 to
    // r5 (Naruto, Kamen Rider, Ranger Cross). The 2007 read takes three
    // (Brawl, Mario Kart Wii), and r6 is whatever its caller left there.
    each_word(text, found.address, kErrorCopyWithin, [&](std::uint32_t, std::uint32_t w) {
        const bool or_form = opcode(w) == 31 && ((w >> 1) & 0x3FFu) == 444 && (w & 1u) == 0;  // or rA,rS,rB
        if (or_form && reg_d(w) == 6 && ((w >> 11) & 31u) == 6 && reg_a(w) != 6) found.takes_error = true;
        return !found.takes_error;
    });
    return found;
}

std::uint32_t find_wpad_get_info(const std::vector<CodeRange>& text) {
    constexpr std::uint32_t kIndexChannel = 0x5460103Au;  // slwi r0,r3,2
    constexpr std::int32_t kStatusReport = 0x15;
    constexpr std::int32_t kBusy = -2;
    constexpr std::int32_t kCommandBytes = 48;
    std::uint32_t found = 0;
    unsigned matches = 0;
    for (const CodeRange& r : text) {
        if (r.bytes == nullptr) continue;
        const std::size_t words = r.size / 4;
        std::size_t i = 0;
        while (i < words) {
            while (i < words && !is_stwu_r1(be32(r.bytes + 4 * i))) ++i;
            if (i >= words) break;
            const std::size_t start = i++;
            while (i < words && !is_stwu_r1(be32(r.bytes + 4 * i))) ++i;
            bool index = false, report = false, busy = false, command = false;
            int info_reg = -1, callback_reg = -1;
            // The copies stored to the stack after they were made (the
            // request's own fields): a prologue saving the same registers
            // before the copies doesn't count (CodeRabbit on #616).
            bool info_kept = false, callback_kept = false;
            const std::size_t end = function_end(r.bytes, start, i);
            for (std::size_t k = start; k < end; ++k) {
                const std::uint32_t insn = be32(r.bytes + 4 * k);
                const std::int32_t value = simm(insn);
                index = index || insn == kIndexChannel;
                const bool li = opcode(insn) == 14 && reg_a(insn) == 0;
                report = report || (li && value == kStatusReport);
                busy = busy || (li && value == kBusy);
                command = command || (opcode(insn) == 7 && value == kCommandBytes);  // mulli
                const bool mr = opcode(insn) == 31 && ((insn >> 1) & 0x3FFu) == 444 && (insn & 1u) == 0 &&
                                ((insn >> 21) & 31u) == ((insn >> 11) & 31u);
                const bool copy4 = mr && ((insn >> 21) & 31u) == 4, copy5 = mr && ((insn >> 21) & 31u) == 5;
                // A copy written over after it was made (`li r27,0`) no
                // longer holds the argument (CodeRabbit CLI, #616).
                // A store made before that still counts, and an early
                // return's restore (epilogue_restore) a branch goes around
                // doesn't end the copy (CodeRabbit CLI).
                const bool restore = epilogue_restore(r.bytes, k, end);
                if (!copy4 && !restore && info_reg >= 0 && writes_register(insn, static_cast<std::uint32_t>(info_reg)))
                    info_reg = -1;
                if (!copy5 && !restore && callback_reg >= 0 &&
                    writes_register(insn, static_cast<std::uint32_t>(callback_reg)))
                    callback_reg = -1;
                if (copy4) {
                    info_reg = static_cast<int>(reg_a(insn));
                    info_kept = false;
                }
                if (copy5) {
                    callback_reg = static_cast<int>(reg_a(insn));
                    callback_kept = false;
                }
                if (opcode(insn) == 36 && reg_a(insn) == 1) {  // stw rS,d(r1)
                    if (static_cast<int>(reg_d(insn)) == info_reg) info_kept = true;
                    if (static_cast<int>(reg_d(insn)) == callback_reg) callback_kept = true;
                }
            }
            const bool keeps = info_kept && callback_kept;
            if (!(index && report && busy && command && keeps)) continue;
            found = r.address + static_cast<std::uint32_t>(4 * start);
            ++matches;
        }
    }
    return matches == 1 ? found : 0;
}

WpadConnect2006 find_wpad_connect_2006(const std::vector<CodeRange>& text) {
    constexpr std::int32_t kConnectCallback = 2020;  // in the Oct 3 2006 WPAD's control block
    constexpr std::size_t kMaxWords = 40;
    WpadConnect2006 found;
    unsigned matches = 0;
    // The setter ends at its return: leaf code after it, before the next
    // framed function, isn't counted (function_end).
    each_framed_function(text, kMaxWords, [&](const CodeRange& r, std::size_t start, std::size_t end) {
        RegTrack regs(text, r, start, end, RegTrack::kArguments | RegTrack::kAddresses);
        regs.follow(4);  // the new callback, the second argument
        std::uint32_t table = 0;
        std::uint32_t swapped = 0;  // the table whose block's callback was loaded and stored
        bool two = false;           // another table swapped too: not guessed
        int block = -1;
        bool loads = false, stores = false;
        for (std::size_t k = start; k < end; ++k) {
            const std::uint32_t insn = be32(r.bytes + 4 * k);
            const std::uint32_t d = reg_d(insn), a = reg_a(insn);
            const std::uint32_t r4_here = regs.copies(4);  // as this instruction finds them
            const bool lwzx = opcode(insn) == 31 && ((insn >> 1) & 0x3FFu) == 23;
            const std::uint32_t base = lwzx ? regs.address(a) : 0;  // the table, as it finds it
            regs.step(k);
            if (lwzx && base != 0) {
                // A new lookup: a load and a store through the block it
                // gives count afresh, even from the same table
                // (CodeRabbit on #616).
                loads = stores = false;
                table = base;
                block = static_cast<int>(d);
                continue;
            }
            if (block >= 0 && static_cast<int>(a) == block && simm(insn) == kConnectCallback) {
                loads = loads || opcode(insn) == 32;  // lwz
                // stw of the new callback, not another value (a cleared
                // slot): CodeRabbit CLI, #616.
                stores = stores || (opcode(insn) == 36 && ((r4_here >> d) & 1u));
            }
            // The block's register written with anything else (`lwz
            // r4,8(r1)`), or clobbered by a call (CodeRabbit CLI): what
            // goes through it after isn't the block.
            if (block >= 0 && regs.clobbers(k, static_cast<unsigned>(block)) &&
                !epilogue_restore(r.bytes, k, end)) {
                block = -1;
                loads = stores = false;
            }
            // The first table with both keeps them: a later lookup in
            // another table doesn't take its place, but a second swap
            // through another table makes the function ambiguous
            // (CodeRabbit CLI, #616).
            if (loads && stores) {
                if (swapped == 0) swapped = table;
                else if (table != swapped) two = true;
            }
        }
        if (swapped == 0 || two) return;
        found.table = swapped;
        found.offset = static_cast<std::uint32_t>(kConnectCallback);
        ++matches;
    });
    return matches == 1 ? found : WpadConnect2006{};
}

namespace {

bool is_li(std::uint32_t insn, std::int32_t value) { return opcode(insn) == 14 && reg_a(insn) == 0 && simm(insn) == value; }

// `cmpwi rA,value` in cr0.
bool is_cmpwi(std::uint32_t insn, std::int32_t value) {
    return opcode(insn) == 11 && reg_d(insn) == 0 && simm(insn) == value;
}

// `slwi rX,rS,2` (`rlwinm rX,rS,2,0,29`) for an rS in `channel` (r3, the
// channel, and its copies, one bit each): the channel as an index into a
// table of words.
bool indexes_channel(std::uint32_t insn, std::uint32_t channel) {
    return (insn & ~(31u << 16) & ~(31u << 21)) == 0x5400103Au && ((channel >> reg_d(insn)) & 1u);
}

// WPAD's per-channel callback setters: the (table, offset) pair of each
// short framed function that takes a channel's control block from a table
// (`lwzx`) and swaps one word of it past its buffers, loading the old
// callback and storing its second argument (r4, or a copy) in its place.
std::vector<std::pair<std::uint32_t, std::int32_t>> wpad_setters(const std::vector<CodeRange>& text) {
    constexpr std::size_t kMaxSetterWords = 48;
    constexpr std::int32_t kBlockFields = 0x400;  // the callbacks sit past the control block's buffers
    std::vector<std::pair<std::uint32_t, std::int32_t>> setters;
    each_framed_function(text, kMaxSetterWords, [&](const CodeRange& r, std::size_t start, std::size_t end) {
        RegTrack regs(text, r, start, end, RegTrack::kArguments | RegTrack::kAddresses);
        regs.follow(4);  // the new callback, the second argument
        std::uint32_t table = 0;
        const std::size_t first_of_function = setters.size();
        int block = -1;
        // The block's fields loaded so far (another field read between the
        // old callback's load and the store doesn't hide it: CodeRabbit
        // CLI, #616).
        std::vector<std::int32_t> loaded;
        for (std::size_t k = start; k < end; ++k) {
            const std::uint32_t insn = be32(r.bytes + 4 * k);
            const std::uint32_t d = reg_d(insn), a = reg_a(insn);
            const std::uint32_t r4_here = regs.copies(4);  // as this instruction finds them
            const bool lwzx = opcode(insn) == 31 && ((insn >> 1) & 0x3FFu) == 23;
            const std::uint32_t base = lwzx ? regs.address(a) : 0;  // the table, as it finds it
            regs.step(k);
            if (lwzx && base != 0) {
                table = base;
                block = static_cast<int>(d);
                loaded.clear();  // a load from another block isn't this one's
                continue;
            }
            // The block register written with anything else (a reload from
            // the stack, another value): a load and a store through it after
            // this aren't the table's block (CodeRabbit CLI, #15). A load into
            // it from the block itself ends it too.
            if (block >= 0 && regs.clobbers(k, static_cast<unsigned>(block))) {
                // An epilogue's restore (epilogue_restore: off r1, then the
                // frame undone and a return or tail call) doesn't end it: an
                // early error return branched around leaves the block as it
                // was for the swap after it.
                if (!epilogue_restore(r.bytes, k, end)) {
                    block = -1;
                    loaded.clear();
                }
                continue;
            }
            if (block < 0 || static_cast<int>(a) != block || simm(insn) < kBlockFields) continue;
            if (opcode(insn) == 32) {  // lwz: the old callback
                if (std::find(loaded.begin(), loaded.end(), simm(insn)) == loaded.end()) loaded.push_back(simm(insn));
                continue;
            }
            if (opcode(insn) != 36 || std::find(loaded.begin(), loaded.end(), simm(insn)) == loaded.end() ||
                !((r4_here >> d) & 1u))
                continue;
            // stw: the new one, the second argument. Once per function, though
            // more than one of its paths swaps the same slot.
            const std::pair<std::uint32_t, std::int32_t> setter(table, simm(insn));
            if (std::find(setters.begin() + static_cast<std::ptrdiff_t>(first_of_function), setters.end(), setter) ==
                setters.end())
                setters.push_back(setter);
        }
    });
    return setters;
}

}  // namespace

std::uint32_t find_wpad_get_info_later(const std::vector<CodeRange>& text, std::uint32_t block_table) {
    constexpr std::size_t kMaxWords = 48;
    constexpr std::uint32_t kCmpwiR3Busy = 0x2C03FFFEu;  // cmpwi r3,-2
    if (block_table == 0) return 0;
    std::uint32_t found = 0;
    bool two = false;
    each_framed_function(text, kMaxWords, [&](const CodeRange& r, std::size_t start, std::size_t end) {
        RegTrack regs(text, r, start, end, RegTrack::kAddresses);
        bool table = false, several = false;
        std::uint32_t target = 0;
        for (std::size_t k = start; k < end; ++k) {
            const std::uint32_t insn = be32(r.bytes + 4 * k);
            const bool bl = (insn & 0xFC000003u) == 0x48000001u;
            // The table's address built here: an `addi` from a `lis` half,
            // as the registers were before it (RegTrack).
            regs.step(k);
            if (opcode(insn) == 14 && regs.address(reg_d(insn)) == block_table) table = true;
            if (bl && k + 1 < end && be32(r.bytes + 4 * (k + 1)) == kCmpwiR3Busy) {
                std::uint32_t offset = insn & 0x03FFFFFCu;  // bl: its target
                if (offset & 0x02000000u) offset |= 0xFC000000u;
                const std::uint32_t here = r.address + static_cast<std::uint32_t>(4 * k) + offset;
                // Two different busy-checked calls in one function: not
                // guessed (CodeRabbit CLI, #616); the same one twice is fine.
                if (target != 0 && target != here) several = true;
                target = here;
            }
        }
        if (!table || target == 0) return;
        // The target is in the game's code, or it isn't a function to hook
        // (CodeRabbit CLI, #616).
        bool in_code = false;
        for (const CodeRange& c : text)
            in_code = in_code || (c.bytes != nullptr && target >= c.address && target - c.address + 4 <= c.size);
        if (!in_code) return;
        // Only a function that makes the table counts, and its own
        // ambiguity with it (CodeRabbit CLI, #616).
        if (several || (found != 0 && found != target)) two = true;
        found = target;
    });
    return two ? 0 : found;
}

WpadConnect2006 find_wpad_connect_later(const std::vector<CodeRange>& text) {
    constexpr std::uint32_t kBctrl = 0x4E800421u;
    constexpr unsigned kCallWindow = 8;  // words from the slot's load to its call
    const std::vector<std::pair<std::uint32_t, std::int32_t>> setters = wpad_setters(text);
    // The slot offsets WPAD calls with "no controller" (the call's load
    // doesn't say which table's block it is: that is the setters').
    std::vector<std::int32_t> told_gone;
    for (const CodeRange& r : text) {
        if (r.bytes == nullptr) continue;
        const std::size_t words = r.size / 4;
        for (std::size_t i = 0; i < words; ++i) {
            const std::uint32_t insn = be32(r.bytes + 4 * i);
            if (opcode(insn) != 32 || reg_d(insn) != 12) continue;  // lwz r12,O(rB)
            const auto setter = std::find_if(setters.begin(), setters.end(),
                                             [&](const auto& s) { return s.second == simm(insn); });
            if (setter == setters.end()) continue;
            // What each word on the way does to r4 (the callback's argument:
            // `li r4,-1` is "no controller", any other write isn't), to the
            // registers holding the slot (r12, loaded here, and its `mr`
            // copies) and to CTR (`mtctr`), and where a conditional branch
            // goes; then every path from the load to the call is followed,
            // and the call is the slot's "no controller" only if on each path
            // r4 is -1 and CTR came from a register holding the slot then
            // (CodeRabbit on #616, CodeRabbit CLI: a branch around any of
            // those writes).
            struct Word {
                std::int8_t r4 = 0;           // 0: untouched, 1: -1, 2: anything else
                std::int8_t ctr = -1;         // mtctr's source register, -1: none
                bool ctr_other = false;       // CTR changed another way (bdnz)
                std::uint32_t writes = 0;     // the registers it writes
                std::int8_t copy_from = -1;   // `mr rA,rS`: rS (rA holds what rS held)
                std::int64_t to = -1;         // a conditional branch's target word, -1: none
            };
            std::vector<Word> way;
            for (std::size_t k = i + 1; k < words && k <= i + kCallWindow; ++k) {
                const std::uint32_t next = be32(r.bytes + 4 * k);
                if (next == kBctrl) {
                    struct Path {
                        std::size_t at;
                        bool gone, ctr;
                        std::uint32_t slot;  // the registers holding the slot
                    };
                    bool every = true;
                    std::vector<Path> paths{{0, false, false, 1u << 12}};
                    while (!paths.empty() && every) {
                        Path q = paths.back();
                        paths.pop_back();
                        for (; q.at < way.size(); ++q.at) {
                            const Word& w = way[q.at];
                            if (w.r4 != 0) q.gone = w.r4 == 1;
                            if (w.ctr >= 0) q.ctr = ((q.slot >> w.ctr) & 1u) != 0;
                            if (w.ctr_other) q.ctr = false;
                            const bool copies_slot = w.copy_from >= 0 && ((q.slot >> w.copy_from) & 1u);
                            q.slot &= ~w.writes;
                            if (copies_slot) q.slot |= w.writes;
                            if (w.to < 0) continue;
                            const std::int64_t to = w.to - static_cast<std::int64_t>(i + 1);
                            // Back, or onto the words before: not followed.
                            if (to <= static_cast<std::int64_t>(q.at)) every = false;
                            // To the call or before it; past it, that path
                            // doesn't call (the `beq` past it when the slot
                            // is empty).
                            else if (to <= static_cast<std::int64_t>(way.size()))
                                paths.push_back({static_cast<std::size_t>(to), q.gone, q.ctr, q.slot});
                        }
                        every = every && q.gone && q.ctr;
                    }
                    // The call goes through CTR: only a call whose CTR came
                    // from the loaded slot is the slot's (CodeRabbit CLI, #15).
                    if (every && std::find(told_gone.begin(), told_gone.end(), setter->second) == told_gone.end())
                        told_gone.push_back(setter->second);
                    break;
                }
                // A jump or return, or r12 loaded again: not this slot's call
                // (the `beq` past it when the slot is empty is fine).
                if (opcode(next) == 18 || next == kBlr ||
                    ((opcode(next) == 32 || opcode(next) == 14) && reg_d(next) == 12))
                    break;
                Word w;
                if ((next & 0xFC1FFFFFu) == 0x7C0903A6u) w.ctr = static_cast<std::int8_t>(reg_d(next));  // mtctr rS
                for (std::uint32_t g = 0; g < 32; ++g)
                    if (writes_register(next, g)) w.writes |= 1u << g;
                // `mr rA,rS` (`or rA,rS,rS`).
                if (opcode(next) == 31 && ((next >> 1) & 0x3FFu) == 444 && (next & 1u) == 0 &&
                    ((next >> 21) & 31u) == ((next >> 11) & 31u))
                    w.copy_from = static_cast<std::int8_t>((next >> 21) & 31u);
                if ((w.writes >> 4) & 1u) w.r4 = is_li(next, -1) ? 1 : 2;
                if (opcode(next) == 16) {  // bc
                    if ((((next >> 21) & 31u) & 4u) == 0) w.ctr_other = true;  // bdnz and kin count CTR down
                    if ((next & 3u) == 0) {  // relative, no link
                        std::int32_t offset = static_cast<std::int32_t>(next & 0xFFFCu);
                        if (offset & 0x8000) offset -= 0x10000;
                        w.to = static_cast<std::int64_t>(k) + offset / 4;
                    }
                }
                way.push_back(w);
            }
        }
    }
    if (told_gone.size() != 1) return WpadConnect2006{};
    // One table with a setter for that offset; two: not guessed.
    std::uint32_t table = 0;
    for (const auto& setter : setters) {
        if (setter.second != told_gone[0]) continue;
        if (table != 0 && table != setter.first) return WpadConnect2006{};
        table = setter.first;
    }
    WpadConnect2006 found;
    found.table = table;
    found.offset = static_cast<std::uint32_t>(told_gone[0]);
    return found;
}

WpadConnect2006 find_wpad_extension(const std::vector<CodeRange>& text, const WpadConnect2006& connect) {
    if (connect.table == 0 || connect.offset < 4) return WpadConnect2006{};
    const std::pair<std::uint32_t, std::int32_t> slot(connect.table, static_cast<std::int32_t>(connect.offset) - 4);
    const std::vector<std::pair<std::uint32_t, std::int32_t>> setters = wpad_setters(text);
    if (std::count(setters.begin(), setters.end(), slot) != 1) return WpadConnect2006{};
    WpadConnect2006 found;
    found.table = slot.first;
    found.offset = static_cast<std::uint32_t>(slot.second);
    return found;
}

std::uint32_t find_wpad_read(const std::vector<CodeRange>& text) {
    constexpr std::size_t kMaxWords = 192;
    constexpr std::int32_t kCoreStatusBytes = 42;  // sizeof(WPADStatus)
    constexpr std::int32_t kErrorAt = 41;          // WPADStatus.err
    std::uint32_t found = 0;
    unsigned matches = 0;
    each_framed_function(text, kMaxWords, [&](const CodeRange& r, std::size_t start, std::size_t end) {
        bool index = false, copy = false, corrupt = false, busy = false, error = false;
        RegTrack regs(text, r, start, end, RegTrack::kArguments);
        regs.follow(3);  // the channel
        regs.follow(4);  // the status
        for (std::size_t k = start; k < end; ++k) {
            const std::uint32_t insn = be32(r.bytes + 4 * k);
            // The checks see the registers as this instruction finds them.
            const std::uint32_t from_r3 = regs.copies(3), from_r4 = regs.copies(4);
            index = index || indexes_channel(insn, from_r3);
            copy = copy || (is_li(insn, kCoreStatusBytes) && reg_d(insn) == 5);
            corrupt = corrupt || is_cmpwi(insn, -7);
            busy = busy || is_cmpwi(insn, -2);
            error = error || (opcode(insn) == 38 && simm(insn) == kErrorAt && ((from_r4 >> reg_a(insn)) & 1u));
            regs.step(k);
        }
        if (!(index && copy && corrupt && busy && error)) return;
        found = r.address + static_cast<std::uint32_t>(4 * start);
        ++matches;
    });
    return matches == 1 ? found : 0;
}

std::uint32_t find_wpad_set_format(const std::vector<CodeRange>& text) {
    constexpr std::size_t kMaxWords = 96;
    constexpr std::int32_t kBlockFields = 0x400;
    std::uint32_t found = 0;
    unsigned matches = 0;
    each_framed_function(text, kMaxWords, [&](const CodeRange& r, std::size_t start, std::size_t end) {
        bool none = false, busy = false, compares = false, stores = false;
        // r4 (the format) and its copies, one bit each; the checks count
        // its copies only, as this setter keeps the format in a copy.
        RegTrack regs(text, r, start, end, RegTrack::kArguments);
        regs.follow(4);
        for (std::size_t k = start; k < end; ++k) {
            const std::uint32_t insn = be32(r.bytes + 4 * k);
            const std::uint32_t copies = regs.copies(4) & ~(1u << 4);  // as this instruction finds them
            regs.step(k);
            // "No controller": returned as such, or the probe's result kept
            // and compared with it (The Complete Saga's 2007 WPAD).
            none = none || is_li(insn, -1) || is_cmpwi(insn, -1);
            busy = busy || is_li(insn, -2);
            const bool cmplw = opcode(insn) == 31 && ((insn >> 1) & 0x3FFu) == 32 && ((insn >> 21) & 1u) == 0;
            compares = compares || (cmplw && (((copies >> reg_a(insn)) & 1u) || ((copies >> ((insn >> 11) & 31u)) & 1u)));
            stores = stores || (opcode(insn) == 36 && simm(insn) >= kBlockFields && ((copies >> reg_d(insn)) & 1u));
        }
        if (!(none && busy && compares && stores)) return;
        found = r.address + static_cast<std::uint32_t>(4 * start);
        ++matches;
    });
    return matches == 1 ? found : 0;
}

std::uint32_t find_wpad_probe(const std::vector<CodeRange>& text) {
    constexpr std::uint32_t kNoDevice = 253;     // WPAD's "not known yet"
    constexpr std::uint32_t kIndexChannel = 0x5460103Au;  // slwi r0,r3,2
    std::uint32_t found = 0;
    unsigned matches = 0;
    std::vector<std::uint32_t> checked;  // a function with several compares is looked at once
    for (const CodeRange& r : text) {
        if (r.bytes == nullptr) continue;
        for (std::size_t k = 0; k * 4 + 4 <= r.size; ++k) {
            const std::uint32_t insn = be32(r.bytes + 4 * k);
            if (opcode(insn) != 10 || (insn & 0xFFFFu) != kNoDevice) continue;  // cmplwi rX,253
            const std::uint32_t start = function_start(text, r.address + static_cast<std::uint32_t>(4 * k), 64);
            if (start == 0 || std::find(checked.begin(), checked.end(), start) != checked.end()) continue;
            checked.push_back(start);
            bool index = false, lwzx = false, store_type = false, minus_one = false, minus_two = false;
            std::int32_t type_offsets[8] = {};
            unsigned type_loads = 0;
            bool repeated_type = false;
            each_word(text, start, 80, [&](std::uint32_t, std::uint32_t w) {
                index = index || w == kIndexChannel;
                lwzx = lwzx || (opcode(w) == 31 && ((w >> 1) & 0x3FFu) == 23);
                if (opcode(w) == 34 && reg_d(w) == 0) {  // lbz r0,D(rA): the device type
                    for (unsigned t = 0; t < type_loads; ++t) repeated_type = repeated_type || type_offsets[t] == simm(w);
                    if (type_loads < 8) type_offsets[type_loads++] = simm(w);
                }
                store_type = store_type || (opcode(w) == 36 && reg_d(w) == 0 && (w & 0xFFFFu) == 0);  // stw r0,0(rA)
                minus_one = minus_one || (opcode(w) == 14 && reg_a(w) == 0 && (w & 0xFFFFu) == 0xFFFFu);
                minus_two = minus_two || (opcode(w) == 14 && reg_a(w) == 0 && (w & 0xFFFFu) == 0xFFFEu);
                return true;
            });
            if (!index || !lwzx || !store_type || !minus_one || !minus_two || !repeated_type) continue;
            found = start;
            ++matches;
        }
    }
    return matches == 1 ? found : 0;
}

KpadUnified find_kpad_unified(const std::vector<CodeRange>& text) {
    constexpr std::uint32_t kCmplwiR5By16 = 0x28050010u;
    // The later KPAD's (Donkey Kong Country Returns'): its ring can be longer
    // than 16, so the count is checked against the ring's length plus 16
    // (`addi r0,r3,16; cmplw r5,r0`).
    constexpr std::uint32_t kAddiR0R3By16 = 0x38030010u;
    constexpr std::uint32_t kCmplwR5R0 = 0x7C050040u;
    KpadUnified found;
    unsigned matches = 0;
    std::vector<std::uint32_t> checked;
    for (const CodeRange& r : text) {
        if (r.bytes == nullptr) continue;
        for (std::size_t k = 0; k * 4 + 4 <= r.size; ++k) {
            const bool later = k * 4 + 8 <= r.size && be32(r.bytes + 4 * k) == kAddiR0R3By16 &&
                               be32(r.bytes + 4 * (k + 1)) == kCmplwR5R0;
            if (be32(r.bytes + 4 * k) != kCmplwiR5By16 && !later) continue;
            const std::uint32_t start = function_start(text, r.address + static_cast<std::uint32_t>(4 * k), 24);
            if (start == 0 || std::find(checked.begin(), checked.end(), start) != checked.end()) continue;
            checked.push_back(start);
            bool channel_block = false, limit = false;
            std::int32_t multiples[8] = {}, copies[8] = {};
            unsigned n_multiples = 0, n_copies = 0;
            std::uint32_t before = 0;  // the word before this one
            each_word(text, start, 80, [&](std::uint32_t, std::uint32_t w) {
                if (opcode(w) == 7) {  // mulli
                    if (reg_a(w) == 3 && simm(w) >= 256) channel_block = true;
                    else if (n_multiples < 8) multiples[n_multiples++] = simm(w);
                }
                if (opcode(w) == 14 && reg_a(w) == 0) {  // li
                    if (simm(w) == 16) limit = true;
                    if (reg_d(w) == 5 && n_copies < 8) copies[n_copies++] = simm(w);
                }
                // The later KPAD's limit: the pair, not an `addi r0,r3,16`
                // alone (CodeRabbit CLI, #616).
                if (w == kCmplwR5R0 && before == kAddiR0R3By16) limit = true;
                before = w;
                return true;
            });
            std::uint32_t size = 0;
            for (unsigned m = 0; m < n_multiples; ++m)
                for (unsigned c = 0; c < n_copies; ++c)
                    if (multiples[m] == copies[c] && multiples[m] >= 40 && multiples[m] <= 96)
                        size = static_cast<std::uint32_t>(multiples[m]);
            if (!channel_block || !limit || size == 0) continue;
            found.address = start;
            found.status_bytes = size;
            ++matches;
        }
    }
    return matches == 1 ? found : KpadUnified{};
}

KpadConnect find_kpad_connect(const std::vector<CodeRange>& text, std::uint32_t kpad_read) {
    // The KPAD read's channel stride: its first `mulli rD,r3,K` with K at
    // least 256 (the per-channel block, not a status).
    std::uint32_t stride = 0;
    for (unsigned i = 0; i < 40 && stride == 0 && kpad_read != 0; ++i) {
        std::uint32_t w = 0;
        if (!word_at(text, kpad_read + 4 * i, w)) break;
        if (opcode(w) == 7 && reg_a(w) == 3 && simm(w) >= 256) stride = static_cast<std::uint32_t>(simm(w));
    }
    if (stride == 0) return {};
    KpadConnect found;
    unsigned matches = 0;
    for (const CodeRange& r : text) {
        if (r.bytes == nullptr) continue;
        for (std::size_t k = 0; k * 4 + 24 <= r.size; ++k) {
            const auto at = [&](std::size_t n) { return be32(r.bytes + 4 * (k + n)); };
            const std::uint32_t m = at(0), hi = at(1), lo = at(2), add = at(3), load = at(4), store = at(5);
            if (opcode(m) != 7 || reg_d(m) != 0 || simm(m) != static_cast<std::int32_t>(stride)) continue;
            const std::uint32_t base = reg_d(hi);
            const bool lis = opcode(hi) == 15 && reg_a(hi) == 0;
            const bool addi = opcode(lo) == 14 && reg_d(lo) == base && reg_a(lo) == base;
            // add rB,rB,r0
            const bool sum = opcode(add) == 31 && ((add >> 1) & 0x1FFu) == 266 && reg_d(add) == base &&
                             reg_a(add) == base && ((add >> 11) & 31u) == 0;
            const bool swap = opcode(load) == 32 && opcode(store) == 36 && reg_a(load) == base &&
                              reg_a(store) == base && (load & 0xFFFFu) == (store & 0xFFFFu) && simm(load) > 0;
            if (!lis || !addi || !sum || !swap) continue;
            found.address = function_start(text, r.address + static_cast<std::uint32_t>(4 * k), 24);
            found.block = (static_cast<std::uint32_t>(hi & 0xFFFFu) << 16) + static_cast<std::uint32_t>(simm(lo));
            found.stride = stride;
            found.offset = static_cast<std::uint32_t>(simm(load));
            ++matches;
        }
    }
    return matches == 1 && found.address != 0 ? found : KpadConnect{};
}

std::uint32_t find_small_data_base(const std::vector<CodeRange>& sections, std::uint32_t entry) {
    std::uint32_t high = 0;
    bool have_high = false;
    for (unsigned i = 0; i < 256; ++i) {
        std::uint32_t insn = 0;
        if (!word_at(sections, entry + i * 4, insn)) return 0;
        if (opcode(insn) == 15 && reg_d(insn) == 13 && reg_a(insn) == 0) {  // lis r13,hi
            high = (insn & 0xFFFFu) << 16;
            have_high = true;
        } else if (have_high && opcode(insn) == 24 && reg_d(insn) == 13 && reg_a(insn) == 13) {  // ori r13,r13,lo
            return high | (insn & 0xFFFFu);
        } else if (have_high && opcode(insn) == 14 && reg_d(insn) == 13 && reg_a(insn) == 13) {  // addi r13,r13,lo
            return high + static_cast<std::uint32_t>(simm(insn));
        }
    }
    return 0;
}

HbmFunctions find_hbm_functions(const std::vector<CodeRange>& text, const std::vector<CodeRange>& sections,
                                std::uint32_t small_data_base) {
    static const char kBanner[] = "<< RVL_SDK - HBM";
    const std::size_t banner_length = sizeof(kBanner) - 1;
    // 1. The banner, in any section.
    std::uint32_t banner = 0;
    unsigned banners = 0;
    for (const CodeRange& r : sections) {
        if (r.bytes == nullptr || r.size < banner_length) continue;
        for (std::size_t k = 0; k + banner_length <= r.size; ++k) {
            if (std::memcmp(r.bytes + k, kBanner, banner_length) != 0) continue;
            banner = r.address + static_cast<std::uint32_t>(k);
            ++banners;
        }
    }
    if (banners != 1) return HbmFunctions{};
    // 2. The word pointing at it (__HBMVersion), outside the code.
    std::uint32_t pointer = 0;
    unsigned pointers = 0;
    for (const CodeRange& r : sections) {
        if (r.bytes == nullptr) continue;
        bool is_text = false;
        for (const CodeRange& t : text) is_text = is_text || t.address == r.address;
        if (is_text) continue;
        for (std::size_t k = 0; k * 4 + 4 <= r.size; ++k) {
            if (be32(r.bytes + 4 * k) != banner) continue;
            pointer = r.address + static_cast<std::uint32_t>(4 * k);
            ++pointers;
        }
    }
    if (pointers != 1) return HbmFunctions{};
    // 3. The function that loads that word: `lwz rD,d(r13)` with the
    // small-data base, or `lis rX,ha` and `lwz rD,lo(rX)` a few words on.
    const std::uint32_t high_adjusted = ((pointer + 0x8000u) >> 16) & 0xFFFFu;
    const std::uint32_t low = pointer & 0xFFFFu;
    std::uint32_t init = 0;
    unsigned loaders = 0;
    for (const CodeRange& r : text) {
        if (r.bytes == nullptr) continue;
        const std::size_t words = r.size / 4;
        for (std::size_t k = 0; k < words; ++k) {
            const std::uint32_t insn = be32(r.bytes + 4 * k);
            if (opcode(insn) != 32) continue;  // lwz
            bool loads_pointer = false;
            if (reg_a(insn) == 13 && small_data_base != 0) {
                loads_pointer = small_data_base + static_cast<std::uint32_t>(simm(insn)) == pointer;
            } else if (reg_a(insn) != 0 && (insn & 0xFFFFu) == low) {
                for (std::size_t back = 1; back <= 6 && back <= k && !loads_pointer; ++back) {
                    const std::uint32_t prior = be32(r.bytes + 4 * (k - back));
                    loads_pointer = opcode(prior) == 15 && reg_a(prior) == 0 && reg_d(prior) == reg_a(insn) &&
                                    (prior & 0xFFFFu) == high_adjusted;
                }
            }
            if (!loads_pointer) continue;
            const std::uint32_t start = function_start(text, r.address + static_cast<std::uint32_t>(4 * k), 32);
            if (start == 0) continue;
            bool calls = false;
            each_word(text, start, 48, [&](std::uint32_t, std::uint32_t w) {
                calls = calls || is_call(w);
                return true;
            });
            if (!calls || start == init) continue;
            init = start;
            ++loaders;
        }
    }
    if (loaders != 1) return HbmFunctions{};
    // 4. HBMCalc: the next function, past HBMInit's `blr` and any padding.
    std::uint32_t end = 0;
    each_word(text, init, 64, [&](std::uint32_t at, std::uint32_t w) {
        if (w == kBlr) end = at;
        return true;
    });
    if (end == 0) return HbmFunctions{};
    std::uint32_t calc = end + 4;
    std::uint32_t insn = 0;
    while (word_at(text, calc, insn) && (insn == 0 || insn == kNop) && calc < end + 64) calc += 4;
    if (!word_at(text, calc, insn) || !is_stwu_r1(insn)) return HbmFunctions{};
    bool calls = false;
    each_word(text, calc, 32, [&](std::uint32_t, std::uint32_t w) {
        calls = calls || is_call(w);
        return true;
    });
    if (!calls) return HbmFunctions{};
    // The button the menu ends on: what HBMCalc returns (2008 on), else
    // HBMGetSelectBtnNum, the function after HBMDraw (the one after HBMCalc).
    std::uint32_t select = calc_answer(text, calc);
    const std::uint32_t draw = next_function(text, calc, 64);
    std::uint32_t draw_first = 0;
    if (select == 0 && draw != 0 && word_at(text, draw, draw_first) && is_stwu_r1(draw_first)) {
        const std::uint32_t candidate = next_function(text, draw, 256);
        if (looks_like_select(text, candidate)) select = candidate;
    }
    return HbmFunctions{init, calc, select};
}

}  // namespace riftwii
