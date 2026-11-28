// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/symsearch.hpp"
#include "riftwii/byteorder.hpp"
#include "riftwii/checkedalloc.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

namespace riftwii {
namespace {

constexpr std::size_t kCallWindow = 24;      // instructions between the command load and its bl
constexpr std::size_t kArgumentLookback = 8; // the other arguments may be loaded before the command
constexpr std::size_t kBodyWindow = 64;      // instructions of a candidate searched for the IPC command
constexpr std::uint32_t kIpcIoctl = 6;       // IPC request commands (wiibrew IOS IPC)
constexpr std::uint32_t kIpcIoctlv = 7;

// DI commands the SDK's DVD driver sends through IOS_IoctlAsync.
constexpr std::uint32_t kDiCommands[] = {0x71, 0x70, 0x8A, 0x8D, 0x12, 0xE3, 0xE4, 0x79, 0x88, 0xD9, 0xA8, 0xE0};
constexpr std::uint32_t kOpenPartition = 0x8B;

std::uint32_t li(unsigned reg, std::uint32_t value) { return 0x38000000u | (reg << 21) | (value & 0xFFFF); }

bool is_bl(std::uint32_t insn) { return (insn & 0xFC000003u) == 0x48000001u; }

std::uint32_t bl_target(std::uint32_t address, std::uint32_t insn) {
    std::int32_t displacement = static_cast<std::int32_t>(insn & 0x03FFFFFCu);
    if (displacement & 0x02000000) displacement -= 0x04000000;
    return address + static_cast<std::uint32_t>(displacement);
}

// An entry of the view with no bytes behind it holds no code to read:
// neither helper looks into one (CodeRabbit CLI, fork #23).
bool inside(const std::vector<CodeRange>& text, std::uint32_t address) {
    for (const CodeRange& r : text) {
        if (r.bytes != nullptr && address >= r.address && address < r.address + r.size) return true;
    }
    return false;
}

std::uint32_t word_at(const std::vector<CodeRange>& text, std::uint32_t address) {
    for (const CodeRange& r : text) {
        if (r.bytes != nullptr && address >= r.address && address + 4 <= r.address + r.size)
            return be32(r.bytes + (address - r.address));
    }
    return 0;
}

// Calls `visit(target)` for the first bl within kCallWindow instructions
// after every occurrence of `insn` (at word index `site`), provided
// `accept(range, site, end)` holds.
template <typename Accept, typename Visit>
void for_each_call_after(const std::vector<CodeRange>& text, std::uint32_t insn, Accept accept, Visit visit) {
    for (const CodeRange& r : text) {
        const std::size_t words = r.size / 4;
        for (std::size_t i = 0; i < words; ++i) {
            if (be32(r.bytes + i * 4) != insn) continue;
            const std::size_t end = i + 1 + kCallWindow < words ? i + 1 + kCallWindow : words;
            if (!accept(r, i, end)) continue;
            for (std::size_t j = i + 1; j < end; ++j) {
                const std::uint32_t w = be32(r.bytes + j * 4);
                if (is_bl(w)) {
                    visit(bl_target(r.address + static_cast<std::uint32_t>(j * 4), w));
                    break;
                }
            }
        }
    }
}

bool window_contains(const CodeRange& r, std::size_t from, std::size_t to, std::uint32_t insn) {
    for (std::size_t k = from; k < to; ++k) {
        if (be32(r.bytes + k * 4) == insn) return true;
    }
    return false;
}

// A function that starts with a stack frame push (every SDK build) and,
// before its first blr, loads the IPC command number it puts in the
// request block: IOS_IoctlAsync stores 6, IOS_IoctlvAsync 7. The helpers
// the DVD driver calls around them do neither.
bool looks_like_ipc_function(const std::vector<CodeRange>& text, std::uint32_t address, std::uint32_t ipc_command) {
    if (!inside(text, address) || (word_at(text, address) & 0xFFFF0000u) != 0x94210000u) return false;
    for (std::size_t k = 1; k < kBodyWindow; ++k) {
        const std::uint32_t w = word_at(text, address + static_cast<std::uint32_t>(k * 4));
        if (w == 0x4E800020u) break;                                            // blr
        if ((w & 0xFC1FFFFFu) == (0x38000000u | ipc_command)) return true;     // li rX, command
    }
    return false;
}

}  // namespace

bool find_ipc_symbols(const std::vector<CodeRange>& text, IpcSymbols& out, std::string& error,
                      unsigned min_commands) {
    for (const CodeRange& r : text) {
        if (r.bytes == nullptr || (r.address & 3) != 0 || (r.size & 3) != 0) {
            error = "code ranges must be word aligned";
            return false;
        }
    }
    // target -> set of DI commands whose call sites branch to it
    std::map<std::uint32_t, std::set<std::uint32_t>> votes;
    for (const std::uint32_t cmd : kDiCommands) {
        for_each_call_after(
            text, li(4, cmd), [](const CodeRange&, std::size_t, std::size_t) { return true; },
            [&](std::uint32_t target) { votes[target].insert(cmd); });
    }
    std::uint32_t best = 0;
    std::size_t best_votes = 0;
    bool tie = false;
    for (const auto& [target, commands] : votes) {
        if (!commands.count(0x71) || !looks_like_ipc_function(text, target, kIpcIoctl)) continue;
        if (commands.size() > best_votes) {
            best = target;
            best_votes = commands.size();
            tie = false;
        } else if (commands.size() == best_votes) {
            tie = true;
        }
    }
    if (best_votes < min_commands || tie) {
        error = "IOS_IoctlAsync not found: no IPC-shaped function (stwu prologue, li 6) shared by " +
                std::to_string(min_commands) + " DI command call sites" + (tie ? " (tie)" : "");
        return false;
    }
    out.ioctl_async = best;
    out.ioctl_async_commands = static_cast<unsigned>(best_votes);

    out.ioctlv_async = 0;
    std::map<std::uint32_t, unsigned> vvotes;
    for_each_call_after(
        text, li(4, kOpenPartition),
        [](const CodeRange& r, std::size_t site, std::size_t end) {
            // The vector counts (3 in, 2 out) are loaded in whatever order
            // the compiler chose, before or after the command.
            const std::size_t from = site > kArgumentLookback ? site - kArgumentLookback : 0;
            return window_contains(r, from, end, li(5, 3)) && window_contains(r, from, end, li(6, 2));
        },
        [&](std::uint32_t target) { vvotes[target]++; });
    for (const auto& [target, n] : vvotes) {
        (void)n;
        if (target != best && looks_like_ipc_function(text, target, kIpcIoctlv)) {
            if (out.ioctlv_async != 0 && out.ioctlv_async != target) {
                out.ioctlv_async = 0;  // ambiguous: leave it unknown
                break;
            }
            out.ioctlv_async = target;
        }
    }
    error.clear();
    return true;
}

}  // namespace riftwii

// ---- the whole API ----------------------------------------------------------

namespace riftwii {
namespace {

constexpr std::uint32_t kApiWindowBytes = 0x2000;  // either side of IOS_IoctlAsync
constexpr std::uint32_t kApiMaxFunctionBytes = 0x400;
constexpr std::size_t kStoreWindow = 8;            // instructions from the li to the stw
constexpr std::uint32_t kBlockRelaunch = 0x28;     // request block field the reboot forms set

bool is_stwu_r1(std::uint32_t w) { return (w & 0xFFFF0000u) == 0x94210000u; }
bool is_li(std::uint32_t w) { return (w & 0xFC1F0000u) == 0x38000000u; }  // addi rX, 0, imm
unsigned rd_of(std::uint32_t w) { return (w >> 21) & 31u; }

// The immediates a function stores into a request block at `offset`:
// `li rX, imm` followed within a few instructions by `stw rX, offset(rY)`.
std::set<std::uint32_t> stored_immediates(const std::vector<CodeRange>& text, std::uint32_t start, std::uint32_t end,
                                          std::uint32_t offset) {
    std::set<std::uint32_t> found;
    for (std::uint32_t a = start; a + 4 <= end; a += 4) {
        const std::uint32_t w = word_at(text, a);
        if (!is_li(w)) continue;
        for (std::size_t k = 1; k <= kStoreWindow && a + k * 4 < end; ++k) {
            const std::uint32_t s = word_at(text, a + static_cast<std::uint32_t>(k * 4));
            // A store off r1 is to the function's own stack, never the
            // request block (an unoptimized function keeps its locals there).
            const unsigned base = (s >> 16) & 31u;
            if ((s & 0xFC000000u) == 0x90000000u && rd_of(s) == rd_of(w) && base != 1 && (s & 0xFFFFu) == offset) {
                found.insert(w & 0xFFFFu);
                break;
            }
            if ((s & 0xFC000000u) == 0x38000000u && rd_of(s) == rd_of(w)) break;  // the register moved on
        }
    }
    return found;
}

// Targets of the bl instructions in [start, end).
std::set<std::uint32_t> call_targets(const std::vector<CodeRange>& text, std::uint32_t start, std::uint32_t end) {
    std::set<std::uint32_t> targets;
    for (std::uint32_t a = start; a + 4 <= end; a += 4) {
        const std::uint32_t w = word_at(text, a);
        if (is_bl(w)) targets.insert(bl_target(a, w));
    }
    return targets;
}

// Whether the instruction may write general register `r`. Errs towards yes:
// a "yes" only makes the caller give up on that call.
bool may_write_gpr(std::uint32_t p, unsigned r) {
    const std::uint32_t op = p >> 26;
    const unsigned rd = rd_of(p);
    const unsigned ra = (p >> 16) & 31u;
    switch (op) {
        case 7: case 8: case 12: case 13: case 14: case 15:  // mulli, subfic, addic(.), addi, addis
            return rd == r;
        case 20: case 21: case 23:                            // rlwimi, rlwinm, rlwnm
        case 24: case 25: case 26: case 27: case 28: case 29:  // ori, oris, xori, xoris, andi., andis.
            return ra == r;
        case 32: case 34: case 40: case 42:  // lwz, lbz, lhz, lha
            return rd == r;
        case 33: case 35: case 41: case 43:  // their update forms also write rA
            return rd == r || ra == r;
        case 46:  // lmw rD: rD to r31
            return rd <= r;
        case 37: case 39: case 45: case 49: case 51: case 53: case 55:  // stores and FP loads with update
        case 57: case 61:                                               // psq_lu, psq_stu
            return ra == r;
        case 4: {
            // Paired singles: only psq_lux and psq_stux (6-bit extended
            // opcodes 38 and 39) write a GPR, their rA; the rest write FPRs
            // or CR.
            const std::uint32_t xo6 = (p >> 1) & 0x3Fu;
            return (xo6 == 38 || xo6 == 39) && ra == r;
        }
        case 31: {
            const std::uint32_t xo = (p >> 1) & 0x3FFu;
            switch (xo) {
                case 28: case 60: case 444: case 412: case 316: case 476: case 124: case 284:  // and..eqv, or (mr)
                case 24: case 536: case 792: case 824: case 26: case 922: case 954:  // shifts, cntlzw, exts
                    return ra == r;
                case 0: case 32:                                            // cmp, cmpl
                case 144: case 146: case 210: case 242: case 467:          // mtcrf, mtmsr, mtsr(in), mtspr
                case 151: case 215: case 407: case 662: case 918: case 150:  // stores
                case 663: case 727: case 983:                               // FP stores
                case 535: case 599:                                         // lfsx, lfdx (an FPR)
                case 54: case 86: case 246: case 278: case 470: case 982: case 1014:  // cache
                case 598: case 854:                                         // sync, eieio
                    return false;
                case 183: case 247: case 439: case 695: case 759:  // stores with update write rA
                case 567: case 631:  // lfsux, lfdux: rA too (their destination is an FPR)
                    return ra == r;
                case 55: case 119: case 311: case 375:  // loads with update write rD and rA
                    return rd == r || ra == r;
                case 533: case 597:  // lswx, lswi: from rD on, as many registers as the count
                    return true;
                default:
                    return rd == r;
            }
        }
        default:
            return false;
    }
}

// Any branch: b, bc, bclr and bcctr, linked or not. A call (bl, bctrl,
// blrl) may change r4, and code reached from a branch may come with
// another r4, so the scan for r4's value stops at one.
bool is_any_branch(std::uint32_t p) {
    const std::uint32_t op = p >> 26;
    if (op == 16 || op == 18) return true;
    const std::uint32_t xo = (p >> 1) & 0x3FFu;
    return op == 19 && (xo == 16 || xo == 528);
}

// Whether a direct branch (b or bc, linked or not) in [start, end) lands
// after `after` and at or before `upto`: code reached that way skips what
// comes before its landing.
bool branch_lands_between(const std::vector<CodeRange>& text, std::uint32_t start, std::uint32_t end,
                          std::uint32_t after, std::uint32_t upto) {
    for (std::uint32_t a = start; a + 4 <= end; a += 4) {
        const std::uint32_t w = word_at(text, a);
        const std::uint32_t op = w >> 26;
        if (op != 16 && op != 18) continue;
        const bool absolute = (w & 2u) != 0;
        std::int32_t displacement = 0;
        if (op == 18) {
            displacement = static_cast<std::int32_t>(w & 0x03FFFFFCu);
            if (displacement & 0x02000000) displacement -= 0x04000000;
        } else {
            displacement = static_cast<std::int16_t>(w & 0xFFFCu);
        }
        const std::uint32_t target = absolute ? static_cast<std::uint32_t>(displacement)
                                              : a + static_cast<std::uint32_t>(displacement);
        if (target > after && target <= upto) return true;
    }
    return false;
}

// The value `li r4, N` gives r4 for the call at `call` (looking back a few
// instructions, not past `start`); -1 when r4 is set some other way there,
// changed after the `li`, a branch comes between them or lands between
// them from elsewhere in [start, end) (another path, maybe another r4), or
// r4 is not set at all.
std::int64_t r4_immediate_before(const std::vector<CodeRange>& text, std::uint32_t start, std::uint32_t end,
                                 std::uint32_t call) {
    for (std::uint32_t back = 4; back <= kArgumentLookback * 4 && call >= start + back; back += 4) {
        const std::uint32_t at = call - back;
        const std::uint32_t p = word_at(text, at);
        if (is_li(p) && rd_of(p) == 4) {
            if (branch_lands_between(text, start, end, at, call)) return -1;
            return static_cast<std::int16_t>(p & 0xFFFFu);
        }
        if (is_any_branch(p) || may_write_gpr(p, 4)) return -1;
    }
    return -1;
}

// The IPC command (1 to 7) a function hands to `setup` as `li r4, N`, or
// 0 when it doesn't call it that way. Every call to `setup` in the
// function must hand it the same command, as the allocator path asks one
// stored command of a function.
std::uint32_t setup_command(const std::vector<CodeRange>& text, std::uint32_t start, std::uint32_t end,
                            std::uint32_t setup) {
    std::uint32_t command = 0;
    for (std::uint32_t a = start + 8; a + 4 <= end; a += 4) {
        const std::uint32_t w = word_at(text, a);
        if (!is_bl(w) || bl_target(a, w) != setup) continue;
        const std::int64_t cmd = r4_immediate_before(text, start, end, a);
        if (cmd < 1 || cmd > 7) return 0;
        const std::uint32_t this_command = static_cast<std::uint32_t>(cmd);
        if (command != 0 && command != this_command) return 0;
        command = this_command;
    }
    return command;
}

// How the function hands its block to `submit`: 1 when r4 (the callback)
// is a register copy (asynchronous), 0 when it is `li r4, 0`
// (synchronous), -1 when there is no such call.
int submit_style(const std::vector<CodeRange>& text, std::uint32_t start, std::uint32_t end, std::uint32_t submit) {
    for (std::uint32_t a = start + 8; a + 4 <= end; a += 4) {
        const std::uint32_t w = word_at(text, a);
        if (!is_bl(w) || bl_target(a, w) != submit) continue;
        for (std::uint32_t back = 4; back <= 8; back += 4) {
            const std::uint32_t p = word_at(text, a - back);
            if (rd_of(p) != 4) continue;
            if (is_li(p)) return (p & 0xFFFFu) == 0 ? 0 : 1;
            return 1;  // mr r4, rX or another move
        }
        return 1;
    }
    return -1;
}

}  // namespace

bool find_ipc_api(const std::vector<CodeRange>& text, const IpcSymbols& known, IpcApi& out, std::string& error) {
    if (known.ioctl_async == 0 || !inside(text, known.ioctl_async)) {
        error = "IOS_IoctlAsync must be known to find the rest of the IPC API";
        return false;
    }
    // The stretch of text around it, clipped to the range it lives in.
    const CodeRange* range = nullptr;
    for (const CodeRange& r : text) {
        if (known.ioctl_async >= r.address && known.ioctl_async < r.address + r.size) range = &r;
    }
    const std::uint32_t lo = known.ioctl_async - std::min(known.ioctl_async - range->address, kApiWindowBytes);
    const std::uint32_t hi = std::min(static_cast<std::uint32_t>(range->address + range->size), known.ioctl_async + kApiWindowBytes);

    // Function starts: stack-frame prologues.
    std::vector<std::uint32_t> starts;
    for (std::uint32_t a = lo; a + 4 <= hi; a += 4) {
        if (is_stwu_r1(word_at(text, a))) starts.push_back(a);
    }
    std::size_t known_index = starts.size();
    for (std::size_t i = 0; i < starts.size(); ++i) {
        if (starts[i] == known.ioctl_async) known_index = i;
    }
    if (known_index == starts.size()) {
        error = "IOS_IoctlAsync does not start with a stack frame push";
        return false;
    }
    // The two helpers every API function calls: the block allocator
    // (called with 64, 32: size and alignment) and the submit routine
    // (called with the block and the callback register, or 0).
    const std::uint32_t known_end = known_index + 1 < starts.size() ? starts[known_index + 1] : hi;
    std::uint32_t alloc = 0, submit = 0;
    for (std::uint32_t a = known.ioctl_async + 8; a + 4 <= known_end; a += 4) {
        const std::uint32_t w = word_at(text, a);
        if (!is_bl(w)) continue;
        const std::uint32_t p1 = word_at(text, a - 4), p2 = word_at(text, a - 8);
        if ((p1 == li(5, 32) && p2 == li(4, 64)) || (p1 == li(4, 64) && p2 == li(5, 32))) {
            alloc = bl_target(a, w);
        } else if ((p1 & 0xFC0007FFu) == 0x7C000378u && ((p1 >> 16) & 31u) == 4 && rd_of(p1) == ((p1 >> 11) & 31u)) {
            submit = bl_target(a, w);  // mr r4, rX (or rX, rX, rX) before the call
        }
    }
    // An SDK built without optimization (LEGO Star Wars: The Complete
    // Saga, grantendo/wiiu#61) doesn't allocate the block in the API
    // function: each one hands its command to a setup helper, `li r4, N`
    // then `bl setup`, and the helper allocates the block (taking no size)
    // and stores the command. The submit call is the same. That helper
    // is a call IOS_IoctlAsync makes with `li r4, 6`, and it counts only
    // when it starts with a stack frame and another API function in the
    // stretch (one that also calls submit, as below) calls it with another
    // command. Another call made with 6 that isn't the helper is passed
    // over; when two different functions both pass, which one is the
    // helper is unclear and the search gives up.
    const auto is_setup_helper = [&](std::uint32_t candidate) {
        if (!is_stwu_r1(word_at(text, candidate))) return false;
        std::set<std::uint32_t> commands;
        for (std::size_t i = 0; i < starts.size(); ++i) {
            const std::uint32_t next = i + 1 < starts.size() ? starts[i + 1] : hi;
            if (next - starts[i] > kApiMaxFunctionBytes) continue;
            if (!call_targets(text, starts[i], next).count(submit)) continue;
            const std::uint32_t cmd = setup_command(text, starts[i], next, candidate);
            if (cmd != 0) commands.insert(cmd);
        }
        return commands.size() >= 2;
    };
    std::uint32_t setup = 0;
    bool two_helpers = false;
    if (alloc == 0 && submit != 0) {
        for (std::uint32_t a = known.ioctl_async + 8; a + 4 <= known_end; a += 4) {
            const std::uint32_t w = word_at(text, a);
            if (!is_bl(w) || r4_immediate_before(text, known.ioctl_async, known_end, a) != kIpcIoctlCmd) continue;
            const std::uint32_t candidate = bl_target(a, w);
            if (candidate == setup || !inside(text, candidate) || !is_setup_helper(candidate)) continue;
            if (setup != 0) two_helpers = true;
            setup = candidate;
        }
    }
    if (two_helpers) {
        error = "IOS_IoctlAsync hands command 6 to two functions that both look like the setup helper";
        return false;
    }
    if ((alloc == 0 && setup == 0) || submit == 0) {
        error = "IOS_IoctlAsync does not call a block allocator (64, 32) and a submit routine the expected way";
        return false;
    }
    // Every function in the stretch that uses both helpers and stores
    // one IPC command (or hands it to the setup helper) is an API
    // function; the reboot forms of ioctlv (relaunch set in the block) are
    // left alone.
    for (std::size_t i = 0; i < starts.size(); ++i) {
        const std::uint32_t start = starts[i];
        const std::uint32_t next = i + 1 < starts.size() ? starts[i + 1] : hi;
        if (next - start > kApiMaxFunctionBytes) continue;
        const std::set<std::uint32_t> calls = call_targets(text, start, next);
        if (!calls.count(submit)) continue;
        std::uint32_t cmd = 0;
        if (alloc != 0) {
            if (!calls.count(alloc)) continue;
            const std::set<std::uint32_t> commands = stored_immediates(text, start, next, 0);
            if (commands.size() != 1) continue;
            cmd = *commands.begin();
        } else {
            cmd = setup_command(text, start, next, setup);
        }
        if (cmd < 1 || cmd > 7) continue;
        const std::set<std::uint32_t> relaunch = stored_immediates(text, start, next, kBlockRelaunch);
        bool reboots = false;
        for (const std::uint32_t v : relaunch) reboots = reboots || v != 0;
        if (reboots) continue;
        const int style = submit_style(text, start, next, submit);
        if (style < 0) continue;
        std::uint32_t& slot = style == 1 ? out.async[cmd] : out.sync[cmd];
        if (slot != 0) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "two %s functions store IPC command %u near IOS_IoctlAsync",
                          style == 1 ? "asynchronous" : "synchronous", static_cast<unsigned>(cmd));
            error = buf;
            return false;
        }
        slot = start;
    }
    if (out.async[kIpcIoctlCmd] != known.ioctl_async) {
        error = "the IPC API search does not classify IOS_IoctlAsync as the asynchronous ioctl";
        return false;
    }
    if (known.ioctlv_async != 0 && out.async[kIpcIoctlvCmd] != known.ioctlv_async) {
        error = "the IOS_IoctlvAsync found through the partition open disagrees with the API search";
        return false;
    }
    out.submit = submit;
    error.clear();
    return true;
}

}  // namespace riftwii

// --- The PAD functions ------------------------------------------------------

namespace riftwii {
namespace {

constexpr std::uint32_t kMflrR0 = 0x7C0802A6u;
constexpr std::uint32_t kBlr = 0x4E800020u;
constexpr std::uint32_t kFunctionLookback = 0x1000;  // bytes walked back to a function's start
constexpr unsigned kMinReadSites = 3;
constexpr std::size_t kMotorWindow = 40;             // instructions from the flag read to the command
constexpr std::uint32_t kIdleResetWindow = 0x1000;   // bytes from PADRead's start: PADRead and its status helper
constexpr std::uint32_t kLiR0Zero = 0x38000000u;     // li r0, 0
constexpr std::uint32_t kLiR3One = 0x38600001u;      // li r3, 1

bool is_stw_r0_sda(std::uint32_t w) { return (w & 0xFFFF0000u) == 0x900D0000u; }  // stw r0, d(r13)

// The SDK's screen-dimming resets: clear a small-data flag, return 1
// (compilers put the store and the return value in either order). The
// flag's offset from r13 for a function of that shape, else kNoIdleReset
// (offsets fit 16 bits).
constexpr std::int32_t kNoIdleReset = 0x10000;
std::int32_t idle_reset_flag(const std::vector<CodeRange>& text, std::uint32_t address) {
    // Word by word, so the four may come from neighbouring ranges (a
    // patch over part of the game's code, code_with_patches).
    for (std::uint32_t k = 0; k < 4; ++k) {
        if (!inside(text, address + 4 * k)) return kNoIdleReset;
    }
    const std::uint32_t second = word_at(text, address + 4);
    const std::uint32_t third = word_at(text, address + 8);
    const bool store_then_return = is_stw_r0_sda(second) && third == kLiR3One;
    const bool return_then_store = second == kLiR3One && is_stw_r0_sda(third);
    if (word_at(text, address) != kLiR0Zero || !(store_then_return || return_then_store) ||
        word_at(text, address + 12) != kBlr)
        return kNoIdleReset;
    return static_cast<std::int16_t>((store_then_return ? second : third) & 0xFFFFu);
}

// The idle reset PADRead (or the status helper after it) calls; 0 when no
// call in the window reaches one, or calls reach two different ones. The
// video library defines the controller ports' reset right after the Wii
// Remotes' one, for the flag 4 bytes above, in every game checked (Brawl,
// Mario Kart Wii, Tatsunoko vs. Capcom, Clash of Ninja Revolution 3, Kamen
// Rider): a call near PADRead to a lone function of that shape is
// something else.
std::uint32_t find_idle_reset(const std::vector<CodeRange>& text, std::uint32_t pad_read) {
    std::uint32_t found = 0;
    for (std::uint32_t at = pad_read; at < pad_read + kIdleResetWindow; at += 4) {
        if (!inside(text, at)) break;
        const std::uint32_t w = word_at(text, at);
        if (!is_bl(w)) continue;
        const std::uint32_t target = bl_target(at, w);
        const std::int32_t flag = idle_reset_flag(text, target);
        if (flag == kNoIdleReset || idle_reset_flag(text, target - 16) != flag + 4) continue;
        if (found != 0 && found != target) return 0;
        found = target;
    }
    return found;
}

// The start of the function containing `address`: the nearest `stwu r1`
// followed by `mflr r0` before it, 0 when a `blr` comes first (the site
// is not inside a framed function) or none is near.
std::uint32_t function_start(const std::vector<CodeRange>& text, std::uint32_t address) {
    for (std::uint32_t back = 0; back <= kFunctionLookback && address >= back; back += 4) {
        const std::uint32_t at = address - back;
        if (!inside(text, at)) return 0;
        const std::uint32_t w = word_at(text, at);
        if (back != 0 && w == kBlr) return 0;
        if ((w & 0xFFFF8000u) == 0x94218000u && word_at(text, at + 4) == kMflrR0) return at;
    }
    return 0;
}

// At word `i` of `r` (with four words after it), one of PADRead's "no
// controller in this port" paths: `stb rE,10(rS); mr r3,rS; li r4,0;
// li r5,10; bl memset`. True with the memset it calls.
bool pad_error_path(const CodeRange& r, std::size_t i, std::uint32_t& memset_target) {
    const std::uint32_t w = be32(r.bytes + i * 4);
    if ((w & 0xFC00FFFFu) != 0x9800000Au) return false;  // stb rE, 10(rS)
    const std::uint32_t s = (w >> 16) & 31u;
    const std::uint32_t mr = 0x7C000378u | (s << 21) | (3u << 16) | (s << 11);
    const std::uint32_t call = be32(r.bytes + (i + 4) * 4);
    if (be32(r.bytes + (i + 1) * 4) != mr || be32(r.bytes + (i + 2) * 4) != li(4, 0) ||
        be32(r.bytes + (i + 3) * 4) != li(5, 10) || !is_bl(call)) {
        return false;
    }
    memset_target = bl_target(r.address + static_cast<std::uint32_t>(i * 4) + 16, call);
    return true;
}

}  // namespace

bool is_pad_read(const std::vector<CodeRange>& text, std::uint32_t address) {
    unsigned sites = 0;
    std::uint32_t memset_target = 0;
    bool one_memset = true;
    for (const CodeRange& r : text) {
        const std::size_t words = r.size / 4;
        for (std::size_t i = 0; i + 4 < words; ++i) {
            std::uint32_t target = 0;
            if (!pad_error_path(r, i, target)) continue;
            if (function_start(text, r.address + static_cast<std::uint32_t>(i * 4)) != address) continue;
            if (sites != 0 && target != memset_target) one_memset = false;
            memset_target = target;
            ++sites;
        }
    }
    return sites >= kMinReadSites && one_memset;
}

namespace {

// One of the video library's screen-dimming resets at word `i` of `r`: a
// flag cleared by one store, and 1 returned. `li r0,0; stw r0,d(r13);
// li r3,1; blr` (the middle two either way round), or with the flag's address made by `lis rX` before or
// after the `li r0,0` (LEGO Star Wars III's other reset), `li r3,1` before
// or after its store.
// Read by address: the words may cross from one range into the next
// touching one (a patch over part of it with the same bytes splits it in
// code_with_patches' view; CodeRabbit on #610); a gap ends it.
bool one_store_reset(const std::vector<CodeRange>& text, std::uint32_t at) {
    std::uint32_t words_here[5] = {};
    std::size_t words = 0;
    for (; words < 5; ++words) {
        const std::uint32_t a = at + static_cast<std::uint32_t>(words * 4);
        bool found = false;
        for (const CodeRange& t : text) {
            if (t.bytes != nullptr && a >= t.address && a - t.address + 4 <= t.size && ((a - t.address) & 3u) == 0) {
                words_here[words] = be32(t.bytes + (a - t.address));
                found = true;
                break;
            }
        }
        if (!found) break;
    }
    const auto w = [&](std::size_t k) { return words_here[k]; };
    if (words < 4) return false;
    // The store and the return value in either order, as idle_reset_flag takes them.
    if (w(0) == kLiR0Zero && w(3) == kBlr &&
        ((is_stw_r0_sda(w(1)) && w(2) == kLiR3One) || (w(1) == kLiR3One && is_stw_r0_sda(w(2)))))
        return true;
    if (words < 5) return false;
    const auto lis_then_store = [&](std::uint32_t lis, std::uint32_t store) {
        const std::uint32_t rx = (lis >> 21) & 31u;
        return (lis & 0xFC1F0000u) == 0x3C000000u && (store >> 26) == 36u && ((store >> 21) & 31u) == 0u &&
               ((store >> 16) & 31u) == rx && rx != 0u;
    };
    if (w(0) == kLiR0Zero && lis_then_store(w(1), w(2)) && w(3) == kLiR3One && w(4) == kBlr) return true;
    if (lis_then_store(w(0), w(2)) && w(1) == kLiR0Zero && w(3) == kLiR3One && w(4) == kBlr) return true;
    // `li r3,1` before the store (CodeRabbit on #609), when the flag's
    // address isn't in r3 (the return value would overwrite it): `li r0,0`
    // and `lis rX` in either order, then `li r3,1`, the store and the return.
    const bool not_r3 = ((w(0) >> 21) & 31u) != 3u && ((w(1) >> 21) & 31u) != 3u;
    return w(2) == kLiR3One && w(4) == kBlr && not_r3 &&
           ((w(0) == kLiR0Zero && lis_then_store(w(1), w(3))) || (w(1) == kLiR0Zero && lis_then_store(w(0), w(3))));
}

}  // namespace

std::uint32_t find_wii_remote_idle_reset(const std::vector<CodeRange>& text, std::uint32_t wpad_probe) {
    if (wpad_probe == 0) return 0;
    // Every bl within kWpadReach of WPADProbe (the Wii Remote library's own
    // code) whose target is a one-store reset: the library calls the Wii
    // Remotes' one on each report (LEGO Star Wars III: 0x8080B700 from
    // 0x807C1578 and 0x807C35F8, WPADProbe at 0x807C3A10; Kamen Rider:
    // Dragon Knight: 0x8026C520 from 0x8026F988 and 0x80271864, WPADProbe
    // at 0x80271CB0). One such target, or none.
    constexpr std::uint32_t kWpadReach = 0x10000;
    std::uint32_t found = 0;
    for (const CodeRange& r : text) {
        const std::size_t words = r.size / 4;
        for (std::size_t i = 0; i < words; ++i) {
            const std::uint32_t at = r.address + static_cast<std::uint32_t>(i * 4);
            if (at + kWpadReach < wpad_probe || at > wpad_probe + kWpadReach) continue;
            const std::uint32_t w = be32(r.bytes + i * 4);
            if (!is_bl(w)) continue;
            const std::uint32_t target = bl_target(at, w);
            if (!one_store_reset(text, target)) continue;
            if (found != 0 && found != target) return 0;  // two: not guessed
            found = target;
        }
    }
    if (found == 0) return 0;
    // It must be the Wii Remote library's own: called from inside it (near
    // WPADProbe) at least twice, and from nowhere else, as in every one of
    // 32 dumped games that have it (two calls each, both there). A game
    // helper of the same shape that the window happens to reach is called
    // from the game's own code too (CodeRabbit on #609).
    unsigned near = 0;
    for (const CodeRange& r : text) {
        const std::size_t words = r.size / 4;
        for (std::size_t i = 0; i < words; ++i) {
            const std::uint32_t w = be32(r.bytes + i * 4);
            if (!is_bl(w)) continue;
            const std::uint32_t at = r.address + static_cast<std::uint32_t>(i * 4);
            if (bl_target(at, w) != found) continue;
            if (at + kWpadReach < wpad_probe || at > wpad_probe + kWpadReach) return 0;
            ++near;
        }
    }
    return near >= 2 ? found : 0;
}

bool find_pad_symbols(const std::vector<CodeRange>& text, PadSymbols& out, std::string& error) {
    out = PadSymbols{};
    std::map<std::uint32_t, std::set<std::uint32_t>> read_memsets;  // function -> memset targets
    std::map<std::uint32_t, unsigned> read_sites;
    std::set<std::uint32_t> motors;
    for (const CodeRange& r : text) {
        const std::size_t words = r.size / 4;
        for (std::size_t i = 0; i + 4 < words; ++i) {
            const std::uint32_t w = be32(r.bytes + i * 4);
            const std::uint32_t at = r.address + static_cast<std::uint32_t>(i * 4);
            std::uint32_t memset_target = 0;
            if (pad_error_path(r, i, memset_target)) {
                const std::uint32_t f = function_start(text, at);
                if (f != 0) {
                    read_memsets[f].insert(memset_target);
                    ++read_sites[f];
                }
            }
            if ((w & 0xFC1FFFFFu) == 0x3C008000u) {  // lis rX, 0x8000
                const std::uint32_t x = (w >> 21) & 31u;
                for (std::size_t k = 1; k <= 3 && i + k < words; ++k) {
                    if ((be32(r.bytes + (i + k) * 4) & 0xFC1FFFFFu) != (0x88000000u | (x << 16) | 0x30E3u)) continue;
                    bool command = false;
                    for (std::size_t j = i + k + 1; j < words && j <= i + k + kMotorWindow && !command; ++j) {
                        command = (be32(r.bytes + j * 4) & 0xFC00FFFFu) == 0x64000040u;  // oris rA, rB, 0x40
                    }
                    const std::uint32_t f = command ? function_start(text, at) : 0;
                    if (f != 0) motors.insert(f);
                    break;
                }
            }
        }
    }
    for (const auto& [f, sites] : read_sites) {
        if (sites < kMinReadSites || read_memsets[f].size() != 1) continue;
        if (out.read != 0) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "two PADRead candidates, 0x%08x and 0x%08x", out.read, f);
            error = buf;
            return false;
        }
        out.read = f;
        out.read_sites = sites;
    }
    if (out.read != 0) out.idle_reset = find_idle_reset(text, out.read);
    if (motors.size() > 1) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%u PADControlMotor candidates, the first 0x%08x",
                      static_cast<unsigned>(motors.size()), *motors.begin());
        error = buf;
        return false;
    }
    if (!motors.empty()) out.control_motor = *motors.begin();
    error.clear();
    return true;
}

bool code_with_patches(const std::vector<CodeRange>& text, const std::vector<CodeRange>& patched,
                       std::vector<CodeRange>& out) {
    out.clear();
    // The game's ranges in address order (a DOL has a few text sections).
    std::vector<std::size_t> order;
    if (!try_resize(order, text.size())) return false;
    for (std::size_t k = 0; k < order.size(); ++k) {
        std::size_t at = k;
        while (at > 0 && text[order[at - 1]].address > text[k].address) {
            order[at] = order[at - 1];
            --at;
        }
        order[at] = k;
    }
    // Each patch splits at most one game range in two, so the pieces and
    // then the whole view fit in these (their sizes come from a pack).
    std::vector<CodeRange> pieces;
    if (!try_reserve(pieces, text.size() + patched.size()) || !try_reserve(out, text.size() + 2 * patched.size()))
        return false;
    for (const std::size_t k : order) {
        const CodeRange& r = text[k];
        // The parts of `r` between the patches that land on it.
        std::uint64_t cursor = r.address;
        const std::uint64_t end = std::uint64_t{r.address} + r.size;
        for (const CodeRange& p : patched) {
            const std::uint64_t p_end = std::uint64_t{p.address} + p.size;
            if (p_end <= cursor || p.address >= end) continue;
            if (p.address > cursor) {
                pieces.push_back(CodeRange{static_cast<std::uint32_t>(cursor), r.bytes + (cursor - r.address),
                                           static_cast<std::size_t>(p.address - cursor)});
            }
            cursor = std::max(cursor, p_end);
        }
        if (cursor < end) {
            pieces.push_back(CodeRange{static_cast<std::uint32_t>(cursor), r.bytes + (cursor - r.address),
                                       static_cast<std::size_t>(end - cursor)});
        }
    }
    // Both lists are in address order: merged in one pass.
    std::size_t a = 0, b = 0;
    while (a < pieces.size() || b < patched.size()) {
        const bool take_piece = b == patched.size() || (a < pieces.size() && pieces[a].address < patched[b].address);
        out.push_back(take_piece ? pieces[a++] : patched[b++]);
    }
    return true;
}

bool find_pad_symbols_with_patches(const std::vector<CodeRange>& text, const std::vector<CodeRange>& patched,
                                   PadSymbols& out, bool& from_patch, std::string& error) {
    from_patch = false;
    PadSymbols pack;
    if (!patched.empty()) {
        std::string pack_error;
        // Only an unsure PADRead in the patches is an error: their
        // PADControlMotor isn't used (below), so two of those there don't
        // keep the game's own PADRead from being searched.
        if (!find_pad_symbols(patched, pack, pack_error) && pack_error.find("PADRead") != std::string::npos) {
            out = PadSymbols{};
            error = "in a pack's memory patches: " + pack_error;
            return false;
        }
    }
    if (pack.read == 0) return find_pad_symbols(text, out, error);
    PadSymbols game;
    std::string game_error;
    const bool game_sure = find_pad_symbols(text, game, game_error);
    out = PadSymbols{};
    out.read = pack.read;
    out.read_sites = pack.read_sites;
    std::vector<CodeRange> view;
    if (!code_with_patches(text, patched, view)) {
        out = PadSymbols{};
        error = "no memory to read the game's code with a pack's memory patches";
        return false;
    }
    out.idle_reset = find_idle_reset(view, pack.read);
    // The game's own PADControlMotor, as before packs could bring PADRead
    // this way: Settings > GameCube rumble Off writes to it before the
    // patches go in, which would land on bytes a patch hasn't written yet.
    out.control_motor = game_sure ? game.control_motor : 0;
    from_patch = true;
    error.clear();
    return true;
}

namespace {

// Every address where `text` (with its terminating NUL) starts.
std::vector<std::uint32_t> find_strings(const std::vector<CodeRange>& ranges, const char* text) {
    std::vector<std::uint32_t> out;
    const std::string needle(text, std::char_traits<char>::length(text) + 1);
    for (const CodeRange& r : ranges) {
        const std::uint8_t* begin = r.bytes;
        const std::uint8_t* end = r.bytes + r.size;
        for (const std::uint8_t* p = begin;;) {
            p = std::search(p, end, needle.begin(), needle.end(),
                            [](std::uint8_t a, char b) { return a == static_cast<std::uint8_t>(b); });
            if (p == end) break;
            out.push_back(r.address + static_cast<std::uint32_t>(p - begin));
            ++p;
        }
    }
    return out;
}

// Whether the string at `at` starts on its own: at the start of its range,
// or after a NUL (not inside a longer string that ends the same way).
bool string_starts_after_nul(const std::vector<CodeRange>& ranges, std::uint32_t at) {
    for (const CodeRange& r : ranges) {
        if (at < r.address || at >= r.address + r.size) continue;
        return at == r.address || r.bytes[at - r.address - 1] == 0;
    }
    return false;
}

constexpr std::size_t kPairWindow = 16;     // instructions between a lis and its addi/ori
constexpr std::size_t kPoolWindow = 400;    // instructions a pool base register is followed for
constexpr std::uint32_t kPoolReach = 0x8000;  // bytes past a pool's base its strings may be

// The functions whose code builds one of `targets`' addresses: `lis` and
// `addi`/`ori` directly, or `addi` from a string pool's base register.
std::set<std::uint32_t> functions_using(const std::vector<CodeRange>& text, const std::vector<std::uint32_t>& targets) {
    const auto hit = [&](std::uint32_t value) {
        return std::find(targets.begin(), targets.end(), value) != targets.end();
    };
    std::set<std::uint32_t> functions;
    for (const CodeRange& r : text) {
        const std::size_t words = r.size / 4;
        for (std::size_t i = 0; i < words; ++i) {
            const std::uint32_t lis = be32(r.bytes + i * 4);
            if ((lis & 0xFC1F0000u) != 0x3C000000u) continue;  // lis rA, H (addis rA, 0, H)
            const std::uint32_t a = (lis >> 21) & 31u;
            const std::uint32_t high = (lis & 0xFFFFu) << 16;
            for (std::size_t j = i + 1; j < words && j <= i + kPairWindow; ++j) {
                const std::uint32_t w = be32(r.bytes + j * 4);
                const std::uint32_t op = w >> 26;
                const std::uint32_t src = op == 24 ? (w >> 21) & 31u : (w >> 16) & 31u;
                const std::uint32_t dst = op == 24 ? (w >> 16) & 31u : (w >> 21) & 31u;
                if ((op != 14 && op != 24) || src != a) continue;  // addi rB, rA, L / ori rB, rA, L
                const std::uint32_t low = op == 14 ? static_cast<std::uint32_t>(static_cast<std::int16_t>(w & 0xFFFFu))
                                                   : (w & 0xFFFFu);
                const std::uint32_t base = high + low;
                const std::uint32_t at = r.address + static_cast<std::uint32_t>(j * 4);
                if (hit(base)) {
                    if (const std::uint32_t f = function_start(text, at)) functions.insert(f);
                } else if (std::any_of(targets.begin(), targets.end(),
                                       [base](std::uint32_t t) { return t > base && t - base < kPoolReach; })) {
                    // A string pool's base: its strings are `addi rX, rB, offset`.
                    for (std::size_t k = j + 1; k < words && k <= j + kPoolWindow; ++k) {
                        const std::uint32_t u = be32(r.bytes + k * 4);
                        if (u == kBlr) break;
                        if ((u >> 26) != 14 || ((u >> 16) & 31u) != dst) continue;
                        const std::uint32_t value = base + static_cast<std::uint32_t>(static_cast<std::int16_t>(u & 0xFFFFu));
                        if (!hit(value)) continue;
                        if (const std::uint32_t f =
                                function_start(text, r.address + static_cast<std::uint32_t>(k * 4)))
                            functions.insert(f);
                    }
                }
                break;  // the first use of rA after the lis
            }
        }
    }
    return functions;
}

}  // namespace

bool find_unhandled_exception(const std::vector<CodeRange>& text, const std::vector<CodeRange>& data,
                              std::uint32_t& out, std::string& error) {
    out = 0;
    std::vector<std::uint32_t> targets = find_strings(data, "Unhandled Exception %d");
    const std::vector<std::uint32_t> more = find_strings(data, "Non-recoverable Exception %d");
    targets.insert(targets.end(), more.begin(), more.end());
    if (targets.empty()) {
        error = "the game has no \"Unhandled Exception\" string";
        return false;
    }
    const std::set<std::uint32_t> functions = functions_using(text, targets);
    if (functions.size() != 1) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%u functions use the \"Unhandled Exception\" string",
                      static_cast<unsigned>(functions.size()));
        error = buf;
        return false;
    }
    out = *functions.begin();
    error.clear();
    return true;
}

bool find_os_return_to_menu(const std::vector<CodeRange>& text, const std::vector<CodeRange>& data,
                            std::uint32_t& out, std::string& error) {
    out = 0;
    // Only a string of its own, after a NUL: __OSReturnToMenu's text ends
    // with the same bytes, two in.
    std::vector<std::uint32_t> targets;
    for (const std::uint32_t at : find_strings(data, kOsReturnToMenuText)) {
        if (string_starts_after_nul(data, at)) targets.push_back(at);
    }
    if (targets.empty()) {
        error = "the game has no OSReturnToMenu text";
        return false;
    }
    const std::set<std::uint32_t> functions = functions_using(text, targets);
    if (functions.size() != 1) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%u functions use OSReturnToMenu's text", static_cast<unsigned>(functions.size()));
        error = buf;
        return false;
    }
    out = *functions.begin();
    error.clear();
    return true;
}

}  // namespace riftwii

// --- PADControlMotor of a GameCube game's SDK ---------------------------------

namespace riftwii {
namespace {

constexpr std::uint32_t kKeepLow2BitsMask = 0xFC00FFFFu;  // rlwinm rA, rS, 0, 30, 31, any registers
constexpr std::uint32_t kKeepLow2Bits = 0x540007BEu;
constexpr std::uint32_t kOrisMotorMask = 0xFC00FFFFu;     // oris rA, rS, 0x40, any registers
constexpr std::uint32_t kOrisMotor = 0x64000040u;
constexpr std::uint32_t kMotorFunctionBytes = 0x100;      // the function is about 0x80 bytes long

// Whether `start` begins as a function does, `mflr r0` or `stwu r1,-N(r1)`
// within its first three words: the word after a `blr` can also be the
// rest of a function with an early return, where a hook would land in the
// middle.
bool looks_like_function_start(const std::vector<CodeRange>& text, std::uint32_t start) {
    // PADControlMotor calls two functions, so it sets up a frame: a stwu
    // r1,-N(r1) among its first three words (after mflr r0 and its stw in
    // CodeWarrior's code). A mflr r0 alone is no start: GCC's code takes the
    // link register after an early return's epilogue, mid-function.
    for (std::uint32_t at = start; at < start + 12; at += 4) {
        if (!inside(text, at)) return false;
        if ((word_at(text, at) & 0xFFFF8000u) == 0x94218000u) return true;  // stwu r1,-N(r1)
    }
    return false;
}

// The start of a function GCC built (Sonic Riders Tournament Edition),
// whose early return's epilogue and blr sit in its middle: the nearest
// stwu r1,-N(r1) before `address` that follows a blr or a b. 0 for none.
std::uint32_t gcc_frame_start(const std::vector<CodeRange>& text, std::uint32_t address) {
    for (std::uint32_t back = 4; back <= kMotorFunctionBytes && address >= back; back += 4) {
        const std::uint32_t at = address - back;
        if (!inside(text, at) || !inside(text, at - 4)) return 0;
        if ((word_at(text, at) & 0xFFFF8000u) != 0x94218000u) continue;
        const std::uint32_t before = word_at(text, at - 4);
        return before == kBlr || (before & 0xFC000003u) == 0x48000000u ? at : 0;
    }
    return 0;
}

// Whether any of the function's first four words is a relative branch
// (opcode 16 or 18): the replay slot runs those four words from elsewhere,
// where the branch would land on the wrong address.
bool first_words_branch(const std::vector<CodeRange>& text, std::uint32_t start) {
    for (std::uint32_t at = start; at < start + 16; at += 4) {
        if (!inside(text, at)) return true;
        const std::uint32_t opcode = word_at(text, at) >> 26;
        if (opcode == 16 || opcode == 18) return true;
    }
    return false;
}

// The word after the previous `blr` before `address`, 0 when none is near.
std::uint32_t after_previous_blr(const std::vector<CodeRange>& text, std::uint32_t address) {
    for (std::uint32_t back = 4; back <= kMotorFunctionBytes && address >= back; back += 4) {
        const std::uint32_t at = address - back;
        if (!inside(text, at)) return 0;
        if (word_at(text, at) == kBlr) return at + 4;
    }
    return 0;
}

}  // namespace

bool find_gc_pad_motor(const std::vector<CodeRange>& text, std::uint32_t& out, std::string& error) {
    std::set<std::uint32_t> functions;
    std::string skipped;  // why a match was not taken, said when nothing else is found
    out = 0;
    for (const CodeRange& range : text) {
        const std::size_t words = range.size / 4;
        for (std::size_t i = 0; i < words; ++i) {
            const std::uint32_t at = range.address + static_cast<std::uint32_t>(i * 4);
            if ((be32(range.bytes + i * 4) & kKeepLow2BitsMask) != kKeepLow2Bits) continue;
            // The OR with 0x40 follows within a few words...
            std::uint32_t oris_at = 0;
            for (std::uint32_t step = 4; step <= 16 && oris_at == 0; step += 4) {
                if (inside(text, at + step) && (word_at(text, at + step) & kOrisMotorMask) == kOrisMotor)
                    oris_at = at + step;
            }
            if (oris_at == 0) continue;
            // ...then SISetCommand and SITransferCommands, called one after the other.
            bool two_calls = false;
            for (std::uint32_t step = 4; step <= 12 && !two_calls; step += 4) {
                const std::uint32_t call = oris_at + step;
                two_calls = inside(text, call + 4) && is_bl(word_at(text, call)) && is_bl(word_at(text, call + 4));
            }
            if (!two_calls) continue;
            std::uint32_t start = after_previous_blr(text, at);
            if (start == 0) continue;
            if (!looks_like_function_start(text, start))
                if (const std::uint32_t gcc = gcc_frame_start(text, at)) start = gcc;
            char buf[96];
            if (!looks_like_function_start(text, start)) {
                std::snprintf(buf, sizeof(buf), "0x%08x does not start like a function", start);
                skipped = buf;
            } else if (first_words_branch(text, start)) {
                std::snprintf(buf, sizeof(buf), "0x%08x has a branch in its first four words", start);
                skipped = buf;
            } else {
                functions.insert(start);
            }
        }
    }
    if (functions.size() > 1) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%u GameCube PADControlMotor candidates, the first 0x%08x",
                      static_cast<unsigned>(functions.size()), *functions.begin());
        error = buf;
        return false;
    }
    if (!functions.empty()) out = *functions.begin();
    // A match that was skipped is reported like the other refusals.
    error = functions.empty() ? skipped : std::string();
    return error.empty();
}

namespace {

// The SIGetStatus body as instruction fields, with registers taken from
// the candidate. This follows the register data flow, not game addresses.
std::uint32_t si_status_type_cache(const std::vector<CodeRange>& text, std::uint32_t at) {
    std::uint32_t words[16];
    for (unsigned i = 0; i < 16; ++i) words[i] = word_at(text, at + 4 * i);
    const auto rd = [](std::uint32_t w) { return (w >> 21) & 31u; };
    const auto ra = [](std::uint32_t w) { return (w >> 16) & 31u; };
    const unsigned hardware = rd(words[0]);
    const unsigned shift = rd(words[1]);
    const unsigned channel = ra(words[1]);
    const unsigned status = rd(words[2]);
    const unsigned scratch = ra(words[5]);
    const unsigned high = rd(words[7]);
    const unsigned index = ra(words[8]);
    const unsigned base = rd(words[9]);
    const unsigned entry = rd(words[10]);
    const unsigned type = rd(words[11]);
    const unsigned busy = ra(words[12]);
    // D-form memory operands with rA=0 mean absolute addressing.
    if (hardware == 0 || high == 0 || entry == 0) return 0;
    if (hardware == channel || shift == channel || status == channel || scratch == channel || high == channel ||
        shift == hardware || shift == status || index == high || base == index || type == entry || busy == entry) return 0;
    const std::uint32_t io_high = words[0] & 0xFFFFu;
    if (io_high != 0xCC00u && io_high != 0xCD00u) return 0;
    const std::uint32_t expected[] = {
        0x3C000000u | (hardware << 21) | io_high,              // lis hardware,CC00/CD00
        0x20000003u | (shift << 21) | (channel << 16),         // subfic shift,channel,3
        0x80006438u | (status << 21) | (hardware << 16),       // lwz status,SISR(hardware)
        0x54001838u | (shift << 21) | (shift << 16),          // slwi shift,shift,3
        0x7C000430u | (status << 21) | (status << 16) | (shift << 11),
        0x54000739u | (status << 21) | (scratch << 16),       // NOREP (bit 3)
        0x41820028u,                                         // beq past the cache update
        0x3C000000u | (high << 21) | (words[7] & 0xFFFFu),
        0x5400103Au | (channel << 21) | (index << 16),        // channel * sizeof(u32)
        0x38000000u | (base << 21) | (high << 16) | (words[9] & 0xFFFFu),
        0x7C000214u | (entry << 21) | (base << 16) | (index << 11),
        0x80000000u | (type << 21) | (entry << 16),
        0x54000631u | (type << 21) | (busy << 16),            // BUSY (bit 7)
        0x4082000Cu,                                         // bne past the store
        li(type, 8),
        0x90000000u | (type << 21) | (entry << 16),
    };
    for (unsigned i = 0; i < 16; ++i)
        if (words[i] != expected[i]) return 0;
    const std::uint32_t cache = ((words[7] & 0xFFFFu) << 16) +
                                static_cast<std::int16_t>(words[9] & 0xFFFFu);
    if ((cache & 3u) != 0 || cache < 0x80003100u || cache > 0x81800000u - 16u) return 0;
    return cache;
}

}  // namespace

std::uint32_t find_gc_si_type_cache(const std::vector<CodeRange>& text, std::uint32_t pad_read) {
    if (pad_read == 0 || (pad_read & 3u) != 0) return 0;
    std::set<std::uint32_t> caches;
    // Only PADRead's direct callees qualify. Stop at the function's end;
    // an unrelated SDK copy elsewhere must not select a different table.
    for (std::uint32_t offset = 0; offset < 0x600; offset += 4) {
        const std::uint32_t call = pad_read + offset;
        if (!inside(text, call)) return 0;
        const std::uint32_t word = word_at(text, call);
        if (word == 0x4E800020u) return caches.size() == 1 ? *caches.begin() : 0;
        if (!is_bl(word)) continue;
        const std::uint32_t target = bl_target(call, word);
        for (std::uint32_t step = 0; step < 0x40; step += 4) {
            if (!inside(text, target + step) || word_at(text, target + step) == 0x4E800020u) break;
            const std::uint32_t cache = si_status_type_cache(text, target + step);
            if (cache != 0) caches.insert(cache);
        }
    }
    return 0;
}

namespace {

constexpr std::uint32_t kMtlrR0 = 0x7C0803A6u;

bool is_save_lr(std::uint32_t w) { return (w & 0xFFFF0000u) == 0x90010000u; }  // stw r0,d(r1)
bool is_load_lr(std::uint32_t w) { return (w & 0xFFFF0000u) == 0x80010000u; }  // lwz r0,d(r1)
bool is_pop_r1(std::uint32_t w) { return (w & 0xFFFF0000u) == 0x38210000u; }   // addi r1,r1,N

// A small function's three-word prologue in either order MWCC writes it:
// mflr; stw r0,4(r1); stwu (older SDKs, Double Dash's) or stwu; mflr; stw.
bool small_prologue(const std::uint32_t* w) {
    return (w[0] == kMflrR0 && is_save_lr(w[1]) && is_stwu_r1(w[2])) ||
           (is_stwu_r1(w[0]) && w[1] == kMflrR0 && is_save_lr(w[2]));
}

// Its four-word epilogue: lwz r0, then mtlr and the frame's pop in either
// order, then blr.
bool small_epilogue(const std::uint32_t* w) {
    if (!is_load_lr(w[0]) || w[3] != kBlr) return false;
    return (w[1] == kMtlrR0 && is_pop_r1(w[2])) || (is_pop_r1(w[1]) && w[2] == kMtlrR0);
}

// SIDecodeType's mark: it gives a WaveBird's type, lis rD,0x8B10, among
// its many returns (so the search does not stop at a blr).
bool loads_wavebird_type(const std::vector<CodeRange>& text, std::uint32_t fn) {
    for (std::uint32_t step = 0; step < 0x200; step += 4) {
        if (!inside(text, fn + step)) return false;
        if ((word_at(text, fn + step) & 0xFC1FFFFFu) == 0x3C008B10u) return true;
    }
    return false;
}

// The functions PADRead calls directly, up to its blr.
std::set<std::uint32_t> pad_read_callees(const std::vector<CodeRange>& text, std::uint32_t pad_read) {
    std::set<std::uint32_t> callees;
    for (std::uint32_t offset = 0; offset < 0x600; offset += 4) {
        const std::uint32_t call = pad_read + offset;
        if (!inside(text, call)) return {};
        const std::uint32_t word = word_at(text, call);
        if (word == kBlr) return callees;
        if (is_bl(word)) callees.insert(bl_target(call, word));
    }
    return {};
}

}  // namespace

namespace {

// SIProbe's shape at each word of `scan`'s ranges, its nine words read by
// `words_at(range, i, w)`; `view` is the code its calls are followed in.
template <typename WordsAt>
std::uint32_t si_probe_scan(const std::vector<CodeRange>& scan, const std::vector<CodeRange>& view,
                            std::uint32_t pad_read, WordsAt words_at) {
    if (pad_read == 0 || (pad_read & 3u) != 0) return 0;
    const std::set<std::uint32_t> callees = pad_read_callees(view, pad_read);
    if (callees.empty()) return 0;
    std::set<std::uint32_t> found;
    for (const CodeRange& range : scan) {
        const std::size_t words = range.size / 4;
        for (std::size_t i = 0; i + 9 <= words; ++i) {
            std::uint32_t w[9];
            if (!words_at(range, i, w)) continue;
            if (!small_prologue(w) || !is_bl(w[3]) || !is_bl(w[4]) || !small_epilogue(w + 5)) continue;
            const std::uint32_t at = range.address + static_cast<std::uint32_t>(i * 4);
            const std::uint32_t get_type = bl_target(at + 12, w[3]);
            const std::uint32_t decode = bl_target(at + 16, w[4]);
            // SIGetType is one of PADRead's own calls (its rumble check);
            // SIDecodeType is told apart by the WaveBird's type.
            if (get_type == decode || callees.count(get_type) == 0) continue;
            if (!loads_wavebird_type(view, decode)) continue;
            found.insert(at);
        }
    }
    return found.size() == 1 ? *found.begin() : 0;
}

}  // namespace

std::uint32_t find_gc_si_probe(const std::vector<CodeRange>& text, std::uint32_t pad_read) {
    return si_probe_scan(text, text, pad_read, [](const CodeRange& range, std::size_t i, std::uint32_t* w) {
        for (unsigned k = 0; k < 9; ++k) w[k] = be32(range.bytes + (i + k) * 4);
        return true;
    });
}

std::uint32_t find_gc_si_probe_with_patches(const std::vector<CodeRange>& text, const std::vector<CodeRange>& patched,
                                            std::uint32_t pad_read) {
    // The game's code scanned whole, each word read as the patches leave
    // it, so a patch inside SIProbe (even one writing the same bytes) can't
    // split it out of sight; a patch that changes it is seen.
    std::vector<CodeRange> view;
    if (!code_with_patches(text, patched, view)) return 0;
    // The scan moves forward a word at a time, so the view's range is kept
    // and stepped, not looked for again at each word (thousands of patch
    // pieces over megabytes of code).
    std::size_t at_range = 0;
    const auto word_in_view = [&view, &at_range](std::uint32_t at, std::uint32_t& w) {
        while (at_range > 0 && at < view[at_range].address) --at_range;
        while (at_range < view.size() && std::uint64_t{at} + 4 > std::uint64_t{view[at_range].address} + view[at_range].size)
            ++at_range;
        if (at_range == view.size() || at < view[at_range].address) {
            if (at_range == view.size() && at_range > 0) --at_range;
            return false;
        }
        w = be32(view[at_range].bytes + (at - view[at_range].address));
        return true;
    };
    return si_probe_scan(text, view, pad_read, [&word_in_view](const CodeRange& range, std::size_t i, std::uint32_t* w) {
        for (unsigned k = 0; k < 9; ++k) {
            if (!word_in_view(range.address + static_cast<std::uint32_t>((i + k) * 4), w[k])) return false;
        }
        return true;
    });
}

namespace {

constexpr std::uint32_t kOsContextBytes = 0x2C8;  // the SDK's OSContext
constexpr std::size_t kSamplingWords = 24;        // the callback is called within this many words
constexpr std::size_t kAddressPairWords = 8;      // from a lis to the addi or ori that completes the address

bool is_lwz_sda(std::uint32_t w) { return (w & 0xFC1F0000u) == 0x800D0000u; }  // lwz rD, d(r13)
bool is_addi_r3_r1(std::uint32_t w) { return (w & 0xFFFF0000u) == 0x38610000u; }  // addi r3, r1, d

// Whether `target` is built somewhere in the code with lis and then addi
// (the high half adjusted for the signed low half) or ori, into any
// register: the address taken, as a function pointer's is.
bool address_built(const std::vector<CodeRange>& text, std::uint32_t target) {
    const std::uint32_t low = target & 0xFFFFu;
    const std::uint32_t high = target >> 16;
    const std::uint32_t high_adjusted = ((target + 0x8000u) >> 16) & 0xFFFFu;
    for (const CodeRange& range : text) {
        const std::size_t words = range.size / 4;
        for (std::size_t i = 0; i < words; ++i) {
            const std::uint32_t lis = be32(range.bytes + i * 4);
            if ((lis & 0xFC1F0000u) != 0x3C000000u) continue;  // lis rX, imm (addis rX, 0, imm)
            const std::uint32_t reg = (lis >> 21) & 31u;
            const std::uint32_t imm = lis & 0xFFFFu;
            for (std::size_t k = i + 1; k < words && k <= i + kAddressPairWords; ++k) {
                const std::uint32_t w = be32(range.bytes + k * 4);
                if ((w & 0xFFFFu) != low) continue;
                const std::uint32_t op = w >> 26;
                const bool addi = op == 14 && ((w >> 16) & 31u) == reg && imm == high_adjusted;
                const bool ori = op == 24 && ((w >> 21) & 31u) == reg && imm == high;
                if (addi || ori) return true;
            }
        }
    }
    return false;
}

// Whether the function at `w` (from its frame setup or mflr r0) has
// SamplingHandler's shape (find_gc_pad_sampling).
bool sampling_shape(std::uint32_t address, const std::uint32_t* w, std::size_t count) {
    std::uint32_t frame_bytes = 0;
    for (std::size_t i = 0; i < 4 && i < count; ++i) {
        const bool stwu_r1 = (w[i] & 0xFFFF0000u) == 0x94210000u;
        const std::int32_t frame = -static_cast<std::int16_t>(w[i] & 0xFFFFu);
        if (stwu_r1 && frame >= static_cast<std::int32_t>(kOsContextBytes)) frame_bytes = frame;
    }
    if (frame_bytes == 0) return false;
    for (std::size_t i = 0; i + 13 < count; ++i) {
        if (w[i] == 0x4E800020u) return false;  // do not continue into the next function
        const std::uint32_t reg = (w[i] >> 21) & 31u;
        // Both comparisons test a pointer for zero. The newer compiler
        // (ProStreet's Wii SDK) uses cmpwi and puts stwu before mflr.
        const bool tested = w[i + 1] == (0x28000000u | (reg << 16)) ||
                            w[i + 1] == (0x2C000000u | (reg << 16));
        if (!is_lwz_sda(w[i]) || !tested || w[i + 2] != 0x41820030u) continue;
        const std::uint32_t context = w[i + 3];
        const std::uint32_t context_offset = context & 0xFFFFu;
        if (!is_addi_r3_r1(context) || context_offset < 8 || context_offset > frame_bytes - kOsContextBytes ||
            (context_offset & 7u) != 0) continue;
        if (w[i + 5] != context || w[i + 10] != context) continue;
        const std::uint32_t callback = w[i] & 0xFFFFu;
        const bool reload = w[i + 7] == (0x818D0000u | callback);  // lwz r12,d(r13)
        const bool through_lr = w[i + 8] == 0x7D8803A6u && w[i + 9] == 0x4E800021u;
        const bool through_ctr = w[i + 8] == 0x7D8903A6u && w[i + 9] == 0x4E800421u;
        if (!reload || (!through_lr && !through_ctr)) continue;
        // The same clear/set pair surrounds the callback. Requiring the
        // restoration as well avoids mistaking another large interrupt
        // handler for PAD's when accepting more compiler layouts.
        if (!is_bl(w[i + 4]) || !is_bl(w[i + 6]) || !is_bl(w[i + 11]) || !is_bl(w[i + 13])) continue;
        const auto call_target = [&](std::size_t index) {
            return bl_target(address + static_cast<std::uint32_t>(4 * index), w[index]);
        };
        const std::uint32_t clear = call_target(i + 4);
        const std::uint32_t set = call_target(i + 6);
        if (clear == set || clear != call_target(i + 11) || set != call_target(i + 13)) continue;
        // Its second argument is the interrupted OSContext. The last set
        // must get that saved argument back, from a nonvolatile register
        // or (Everything or Nothing's compiler) a slot below the temporary
        // context, rather than another arbitrary pointer.
        const std::uint32_t restore = w[i + 12];
        for (unsigned saved = 14; saved < 32; ++saved) {
            const std::uint32_t move_to_r3 = 0x7C030378u | (saved << 21) | (saved << 11);
            const std::uint32_t add_to_r3 = 0x38600000u | (saved << 16);
            if (restore != move_to_r3 && restore != add_to_r3) continue;
            for (std::size_t at = 0; at < i; ++at) {
                const std::uint32_t move_from_r4 = 0x7C802378u | (saved << 16);
                const std::uint32_t add_from_r4 = 0x38040000u | (saved << 21);
                if (w[at] == move_from_r4 || w[at] == add_from_r4) return true;
            }
        }
        if ((restore & 0xFFFF0000u) == 0x80610000u) {  // lwz r3,d(r1)
            const std::uint32_t saved_offset = restore & 0xFFFFu;
            if (saved_offset < 8 || saved_offset + 4 > context_offset) continue;
            for (std::size_t at = 0; at < i; ++at)
                if (w[at] == (0x90810000u | saved_offset)) return true;  // stw r4,d(r1)
        }
    }
    return false;
}

}  // namespace

std::uint32_t find_gc_pad_sampling(const std::vector<CodeRange>& text) {
    std::set<std::uint32_t> found;
    for (const CodeRange& range : text) {
        const std::size_t words = range.size / 4;
        for (std::size_t i = 0; i + 18 <= words; ++i) {
            const std::uint32_t first = be32(range.bytes + i * 4);
            const bool frame_first = (first & 0xFFFF0000u) == 0x94210000u &&
                                     be32(range.bytes + (i + 1) * 4) == kMflrR0;
            if (first != kMflrR0 && !frame_first) continue;
            // A frame-first function must be called before its stwu, not
            // at the mflr one word into it.
            if (!frame_first && i != 0 && (be32(range.bytes + (i - 1) * 4) & 0xFFFF0000u) == 0x94210000u) continue;
            std::uint32_t w[kSamplingWords];
            const std::size_t count = std::min(kSamplingWords, words - i);
            for (std::size_t k = 0; k < count; ++k) w[k] = be32(range.bytes + (i + k) * 4);
            const std::uint32_t at = range.address + static_cast<std::uint32_t>(i * 4);
            if (!sampling_shape(at, w, count)) continue;
            if (address_built(text, at)) found.insert(at);
        }
    }
    return found.size() == 1 ? *found.begin() : 0;
}

std::int32_t find_gc_pad_enabled_offset(const std::vector<CodeRange>& text, std::uint32_t pad_read) {
    if (pad_read == 0 || (pad_read & 3u) != 0) return 0;
    std::set<std::int32_t> offsets;
    for (std::uint32_t step = 0; step < 0x600; step += 4) {
        const std::uint32_t at = pad_read + step;
        const std::uint32_t load = word_at(text, at);
        if (!inside(text, at)) return 0;
        if (load == 0x4E800020u) return offsets.size() == 1 ? *offsets.begin() : 0;
        if ((load & 0xFC1F0000u) != 0x800D0000u) continue;  // lwz enabled,d(r13)
        const unsigned enabled = (load >> 21) & 31u;
        const std::uint32_t test = word_at(text, at + 4);
        const unsigned mask = (test >> 11) & 31u;
        const std::uint32_t error = word_at(text, at + 12);
        const unsigned error_reg = (error >> 21) & 31u;
        const std::uint32_t store = word_at(text, at + 16);
        const unsigned status = (store >> 16) & 31u;
        if (status == 0 || status == error_reg || enabled == mask) continue;
        const std::uint32_t expected[] = {
            load,
            0x7C000039u | (enabled << 21) | (enabled << 16) | (mask << 11),
            0x40820020u,  // bne past the disabled-port block
            li(error_reg, 0xFFFFu),
            0x9800000Au | (error_reg << 21) | (status << 16),
            0x38600000u | (status << 16),  // addi r3,status,0
            li(4, 0),
            li(5, 10),
        };
        bool matches = true;
        for (unsigned i = 0; i < 8; ++i) {
            if (word_at(text, at + 4 * i) != expected[i]) matches = false;
        }
        if (!matches || !is_bl(word_at(text, at + 32))) continue;
        // Verify the mask's source within this PADRead, rather than taking
        // an arbitrary small-data word tested on the no-controller path.
        bool channel_mask = false;
        for (std::uint32_t source = pad_read; source < at; source += 4) {
            const std::uint32_t shift = word_at(text, source);
            if ((shift & 0xFC1F07FFu) != (0x7C000430u | (mask << 16))) continue;
            const unsigned high = (shift >> 21) & 31u;
            for (std::uint32_t init = pad_read; init < source; init += 4) {
                if (word_at(text, init) == (0x3C008000u | (high << 21))) channel_mask = true;
            }
        }
        const std::int32_t offset = static_cast<std::int16_t>(load & 0xFFFFu);
        if (channel_mask && offset != 0 && (offset & 3) == 0) offsets.insert(offset);
    }
    return 0;
}


}  // namespace riftwii
