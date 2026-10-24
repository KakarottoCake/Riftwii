// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcexi.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <string>

#include "gc_card.h"

namespace riftwii {
namespace {

static_assert(kGcExiHooks == GC_EXI_HOOKS, "The EXI finder must match the runtime's hook table.");

// The game's code as words by address.
class Words {
public:
    explicit Words(const std::vector<CodeRange>& text) : text_(text) {}
    bool has(std::uint32_t a) const { return find(a) != nullptr; }
    std::uint32_t at(std::uint32_t a) const {
        const std::uint8_t* p = find(a);
        if (p == nullptr) return 0;
        return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
    }
    template <class F>
    void each(F f) const {
        for (const CodeRange& range : text_) {
            for (std::uint32_t offset = 0; offset + 4 <= range.size; offset += 4) {
                const std::uint32_t address = range.address + offset;
                f(address, at(address));
            }
        }
    }

private:
    const std::uint8_t* find(std::uint32_t a) const {
        for (const CodeRange& r : text_)
            if (a >= r.address && a + 4 <= r.address + r.size) return r.bytes + (a - r.address);
        return nullptr;
    }
    const std::vector<CodeRange>& text_;
};

std::uint32_t bl_target(std::uint32_t at, std::uint32_t w) {
    if ((w & 0xFC000003) != 0x48000001) return 0;
    std::int32_t d = static_cast<std::int32_t>(w & 0x03FFFFFC);
    if (d & 0x02000000) d -= 0x04000000;
    return at + d;
}

// Where any relative branch goes, linked or not, conditional or not (b, bl,
// bc, bcl): a tail call reaches a function as surely as a call does. 0 for
// anything else and for absolute branches.
std::uint32_t any_branch_target(std::uint32_t at, std::uint32_t w) {
    const unsigned op = w >> 26;
    if ((w & 2) != 0) return 0;  // AA: an absolute address, not code reached from here
    if (op == 18) {
        std::int32_t d = static_cast<std::int32_t>(w & 0x03FFFFFC);
        if (d & 0x02000000) d -= 0x04000000;
        return at + d;
    }
    if (op == 16) {
        std::int32_t d = static_cast<std::int32_t>(w & 0xFFFC);
        if (d & 0x8000) d -= 0x10000;
        return at + d;
    }
    return 0;
}

std::uint32_t function_start(const Words& code, std::uint32_t at) {
    for (std::uint32_t a = at - 4; code.has(a) && at - a < 0x400; a -= 4)
        if (code.at(a) == 0x4E800020) return a + 4;
    return 0;
}

// The SDK's EXI functions (gc_card.h's order), found through the EXI
// control blocks they all index (an array of 0x40-byte blocks, one per
// channel, its state word at 12) and what each does to that state word:
// select sets 4 and deselect clears it, lock sets 16 and unlock clears
// it, attach sets 8 (and probes). EXIImm keeps its buffer and length at 16 and 20;
// EXIDma calls the same interrupt clear as EXIImm without storing them;
// EXIGetID reads the probe times at 0x800030C0 and calls lock, select,
// EXIImm, then EXISync; EXIProbe and EXIProbeEx call the probe select
// uses and EXIGetID, EXIProbeEx returning -1 too.
//
// The 2001 SDK (Luigi's Mansion USA) differs: its blocks are 0x38 bytes,
// indexed with mulli by 0x38; its EXIGetID does not read the probe times
// (it is the one function calling lock, select and EXIImm); and it has no
// EXIProbeEx, its EXIProbe being the probe select calls. EXIProbeEx is then
// left 0, and the other ten are hooked.
//
// `index_word` is the instruction that turns a channel into its block's
// offset (one block size).

constexpr std::uint32_t kModernIndex = 0x54003032;  // slwi rX,rY,6: 0x40-byte blocks
constexpr std::uint32_t kOldIndex = 0x1C000038;     // mulli rX,rY,0x38: the 2001 SDK's

// Every table the index instruction is used with: lis/addi pairs near it at
// a function's start, and which function uses which table.
struct BlockTables {
    std::map<std::uint32_t, unsigned> users;        // table -> functions using it
    std::map<std::uint32_t, std::uint32_t> base_of;  // function -> its table
};

// Both block sizes' tables in one pass over the code: the pass is most of the
// finder's time (Splinter Cell: Double Agent's 5.6 MB of code), and a second
// one for the 2001 SDK's size doubled it.
struct BothTables {
    BlockTables modern;  // kModernIndex
    BlockTables old;     // kOldIndex
};

BothTables find_block_tables(const Words& code) {
    BothTables both;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        const std::uint32_t index = w & 0xFC00FFFF;
        if (index != kModernIndex && index != kOldIndex) return;
        BlockTables& tables = index == kModernIndex ? both.modern : both.old;
        const std::uint32_t f = function_start(code, a);
        if (f == 0 || a - f > 0x40) return;
        std::uint32_t hi[32] = {};
        bool known[32] = {};
        for (std::uint32_t b = f; b < f + 0x50; b += 4) {
            const std::uint32_t x = code.at(b);
            const unsigned op = x >> 26, d = (x >> 21) & 31, ra = (x >> 16) & 31;
            if (op == 15 && ra == 0) {
                hi[d] = (x & 0xFFFF) << 16;
                known[d] = true;
            } else if (op == 14 && ra != 0 && known[ra]) {
                const std::uint32_t value = hi[ra] + static_cast<std::uint32_t>(static_cast<std::int16_t>(x & 0xFFFF));
                if (value >= 0x80000000 && value < 0x81800000) {
                    tables.users[value]++;
                    tables.base_of[f] = value;
                    return;
                }
            }
        }
    });
    return both;
}

// The first instruction after the function ending at `end`, past the zero
// words and nops the linker pads with.
std::uint32_t next_function(const Words& code, std::uint32_t end) {
    std::uint32_t a = end;
    while (code.has(a) && (code.at(a) == 0 || code.at(a) == 0x60000000) && a - end < 0x40) a += 4;
    return code.has(a) ? a : 0;
}

// Why a listed game's missing EXIProbeEx was not accepted (for the log).
struct Refusal {
    GcExiFunctions::ListedRefusal reason = GcExiFunctions::ListedRefusal::None;
    std::uint32_t at = 0;
};

// The functions using one table, and whether all of them were placed. `out`
// keeps what was placed either way (for the log); a doubtful one stays 0.
bool resolve_exi_functions(const Words& code, std::uint32_t index_word, std::uint32_t ecb,
                           const BlockTables& tables, bool probe_ex_may_be_absent,
                           std::array<std::uint32_t, kGcExiHooks>& out, bool& probe_ex_absent,
                           Refusal& refusal) {
    out.fill(0);
    probe_ex_absent = false;
    refusal = Refusal{};
    struct Fn {
        std::uint32_t start = 0;
        std::uint32_t size = 0;
        bool sets[32] = {};
        bool clears[32] = {};
        bool stores_16_20 = false;
        bool minus_one = false;
        bool probe_times = false;
        bool dma_start = false;  // ori rX,rX,3: a DMA transfer's control word
        std::vector<std::uint32_t> calls;
    };
    std::vector<Fn> fns;
    for (auto& [f, b] : tables.base_of) {
        if (b != ecb) continue;
        Fn fn;
        fn.start = f;
        bool stores_buffer = false;
        bool stores_length = false;
        for (std::uint32_t a = f; code.has(a) && a < f + 0x800; a += 4) {
            const std::uint32_t x = code.at(a);
            const unsigned op = x >> 26;
            const bool self_ori = op == 24 && ((x >> 21) & 31) == ((x >> 16) & 31);
            if (self_ori && (x & 0xFFFF) < 32 && (x & 0xFFFF) != 0) {
                for (unsigned bit = 0; bit < 5; ++bit) {
                    if ((x & 0xFFFF) == (1u << bit)) fn.sets[bit] = true;  // ori rX,rX,1<<bit
                }
            }
            if (self_ori && (x & 0xFFFF) == 3) fn.dma_start = true;
            if (op == 21 && ((x >> 11) & 31) == 0) {  // rlwinm rA,rS,0,MB,ME clearing one bit
                const unsigned mb = (x >> 6) & 31, me = (x >> 1) & 31;
                if (mb == (me + 2) % 32) fn.clears[31 - ((me + 1) % 32)] = true;
            }
            if (op == 36 && (x & 0xFFFF) == 16) stores_buffer = true;
            if (op == 36 && (x & 0xFFFF) == 20) stores_length = true;
            if (x == 0x3860FFFF) fn.minus_one = true;  // li r3,-1
            if (op == 14 && (x & 0xFFFF) == 0x30C0) fn.probe_times = true;
            if (std::uint32_t t = bl_target(a, x)) fn.calls.push_back(t);
            if (x == 0x4E800020) {
                fn.size = a + 4 - f;
                break;
            }
        }
        fn.stores_16_20 = stores_buffer && stores_length;
        fns.push_back(fn);
    }
    auto only = [&](auto test) -> std::uint32_t {
        std::uint32_t found = 0;
        for (const Fn& fn : fns) {
            if (test(fn)) {
                if (found) return 0;  // two: doubtful
                found = fn.start;
            }
        }
        return found;
    };
    auto function_at = [&](std::uint32_t address) -> const Fn* {
        for (const Fn& fn : fns) {
            if (fn.start == address) return &fn;
        }
        return nullptr;
    };
    auto calls = [](const Fn& fn, std::uint32_t target) {
        return target != 0 && std::find(fn.calls.begin(), fn.calls.end(), target) != fn.calls.end();
    };
    out[GC_EXI_DESELECT] = only([](const Fn& fn) { return fn.clears[2]; });
    out[GC_EXI_LOCK] = only([](const Fn& fn) { return fn.sets[4]; });
    out[GC_EXI_IMM] = only([](const Fn& fn) { return fn.stores_16_20 && fn.sets[1]; });
    out[GC_EXI_GET_ID] = only([&](const Fn& fn) {
        return fn.probe_times && calls(fn, out[GC_EXI_LOCK]) && calls(fn, out[GC_EXI_IMM]);
    });
    if (out[GC_EXI_GET_ID] == 0) {  // the 2001 SDK's, which does not read the probe times
        out[GC_EXI_GET_ID] = only([&](const Fn& fn) {
            return calls(fn, out[GC_EXI_LOCK]) && calls(fn, out[GC_EXI_IMM]);
        });
    }
    const std::uint32_t get_id = out[GC_EXI_GET_ID];
    const Fn* get_id_function = function_at(get_id);
    // Pokemon Channel also links a select variant that sets the same
    // software state but does not assert a device's chip select. GetID
    // calls the bus-selecting function; the state bit alone is ambiguous.
    out[GC_EXI_SELECT] = only([&](const Fn& fn) {
        return fn.sets[2] && !fn.sets[4] && get_id_function != nullptr && calls(*get_id_function, fn.start);
    });
    // EXIGetID unlocks (and attaches) too: never taken for those.
    out[GC_EXI_UNLOCK] = only([&](const Fn& fn) { return fn.clears[4] && !fn.probe_times && fn.start != get_id; });
    // The probe select calls (in the control blocks' functions).
    std::uint32_t probe = 0;
    const Fn* select_function = function_at(out[GC_EXI_SELECT]);
    if (select_function != nullptr) {
        for (std::uint32_t target : select_function->calls) {
            if (function_at(target) != nullptr) {
                probe = target;
                break;
            }
        }
    }
    out[GC_EXI_ATTACH] = only(
        [&](const Fn& fn) { return fn.sets[3] && calls(fn, probe) && !fn.probe_times && fn.start != get_id; });
    out[GC_EXI_PROBE] = only([&](const Fn& fn) {
        return calls(fn, probe) && calls(fn, out[GC_EXI_GET_ID]) && !fn.minus_one && !fn.sets[3] && fn.size < 0x100;
    });
    out[GC_EXI_PROBE_EX] = only([&](const Fn& fn) {
        return calls(fn, probe) && calls(fn, out[GC_EXI_GET_ID]) && fn.minus_one && !fn.sets[3];
    });
    // The 2001 SDK: no EXIProbe around the probe, and no EXIProbeEx.
    const bool old_sdk = index_word == kOldIndex;
    const bool old_probe = old_sdk && out[GC_EXI_PROBE] == 0 && out[GC_EXI_PROBE_EX] == 0 && probe != 0;
    if (old_probe) out[GC_EXI_PROBE] = probe;
    // EXIDma: the other one that starts a transfer, with DMA and start
    // set in the control word.
    out[GC_EXI_DMA] = only([&](const Fn& fn) { return fn.dma_start && fn.start != out[GC_EXI_IMM]; });
    // EXISync: what EXIGetID calls right after its first EXIImm, taken only
    // when it is one of the control blocks' functions: anything else is
    // doubtful, and slot A is then left alone rather than an unrelated
    // function hooked.
    if (get_id_function != nullptr) {
        const auto& targets = get_id_function->calls;
        for (std::size_t i = 0; i + 1 < targets.size(); ++i) {
            if (targets[i] == out[GC_EXI_IMM]) {
                if (function_at(targets[i + 1]) != nullptr) out[GC_EXI_SYNC] = targets[i + 1];
                break;
            }
        }
    }
    // Whether every branch to the internal probe anywhere in the code (bl,
    // b, bc: a tail call reaches it as surely as a call) is one an EXIProbeEx
    // the finder missed cannot be. A function's start found by the nearest
    // blr before it can be half of a real EXIProbeEx with an early return, so
    // such a guess never vouches for a branch. A branch passes when it is
    //  (a) inside a placed function (or the probe itself), between its start
    //      and its first blr; or
    //  (b) in code bracketed by placed functions, from the end of the nearest
    //      one before it to the start of the nearest one after it, when that
    //      whole span has no branch to EXIGetID and no li r3,-1: no piece of
    //      an EXIProbeEx, whose GetID call or -1 would sit in the same span.
    //      The SDK's EXI start-up, which probes channels 0 and 1, sits there
    //      in Resident Evil 2 USA.
    // Anything else rejects: a branch from outside the EXI functions, a span
    // longer than kLongestSpan, a placed function of unknown size.
    constexpr std::uint32_t kLongestSpan = 0x1000;
    // The first branch to the probe that failed, for the log.
    std::uint32_t refused_branch = 0;
    auto only_exi_code_branches_to_probe = [&]() {
        // A 0 target is what any_branch_target gives a word that is not a
        // branch: never compared against.
        if (probe == 0 || get_id == 0) return false;
        std::vector<const Fn*> placed_functions;
        for (std::uint32_t address : out) {
            if (address == 0) continue;
            const Fn* fn = function_at(address);
            if (fn == nullptr || fn->size == 0) return false;
            placed_functions.push_back(fn);
        }
        const Fn* probe_function = function_at(probe);
        if (probe_function == nullptr || probe_function->size == 0) return false;
        placed_functions.push_back(probe_function);
        auto bracketed_span_is_clean = [&](std::uint32_t a) {
            std::uint32_t span_start = 0;
            std::uint32_t span_end = 0;
            for (const Fn* fn : placed_functions) {
                const std::uint32_t end = fn->start + fn->size;
                if (end <= a && end > span_start) span_start = end;
                if (fn->start > a && (span_end == 0 || fn->start < span_end)) span_end = fn->start;
            }
            if (span_start == 0 || span_end == 0 || span_end - span_start > kLongestSpan) return false;
            for (std::uint32_t s = span_start; s < span_end; s += 4) {
                if (!code.has(s)) return false;
                const std::uint32_t w = code.at(s);
                if (w == 0x3860FFFF) return false;  // li r3,-1
                const std::uint32_t target = any_branch_target(s, w);
                if (target != 0 && target == get_id) return false;
            }
            return true;
        };
        bool clear = true;
        code.each([&](std::uint32_t a, std::uint32_t w) {
            if (!clear) return;
            const std::uint32_t target = any_branch_target(a, w);
            if (target == 0 || target != probe) return;
            const bool inside_placed = std::any_of(placed_functions.begin(), placed_functions.end(),
                                                   [a](const Fn* fn) { return a >= fn->start && a < fn->start + fn->size; });
            if (!inside_placed && !bracketed_span_is_clean(a)) {
                clear = false;
                refused_branch = a;
            }
        });
        return clear;
    };
    // A 0x40-byte SDK without EXIProbeEx (Resident Evil 2 USA links one),
    // allowed only for the games listed by the caller and only when nothing
    // could be an EXIProbeEx the finder missed: EXIProbe is the one function
    // here calling the probe and EXIGetID (the -1 aside), the SDK's next
    // function after it is EXIAttach (where EXIProbeEx sits otherwise), and
    // nothing else branches to the internal probe in a way an EXIProbeEx
    // could (only_exi_code_branches_to_probe). Games with an EXIProbeEx found
    // never get here.
    bool modern_without_probe_ex = false;
    if (!old_sdk && probe_ex_may_be_absent && out[GC_EXI_PROBE_EX] == 0 && out[GC_EXI_PROBE] != 0 &&
        out[GC_EXI_ATTACH] != 0 && probe != 0) {
        const std::uint32_t probe_like = only([&](const Fn& fn) {
            return calls(fn, probe) && calls(fn, out[GC_EXI_GET_ID]) && !fn.sets[3] && fn.start != get_id;
        });
        const Fn* probe_function = function_at(out[GC_EXI_PROBE]);
        const std::uint32_t after_probe = probe_function != nullptr && probe_function->size != 0
                                              ? next_function(code, probe_function->start + probe_function->size)
                                              : 0;
        const bool attach_follows = after_probe != 0 && after_probe == out[GC_EXI_ATTACH];
        // The checks in turn, the first that refuses named for the log.
        using Reason = GcExiFunctions::ListedRefusal;
        Refusal why;
        if (probe_like != out[GC_EXI_PROBE]) {
            why.reason = Reason::ProbeLike;
        } else if (!attach_follows) {
            why = {Reason::AttachDoesNotFollow, after_probe};
        } else if (!only_exi_code_branches_to_probe()) {
            why = {Reason::ProbeCaller, refused_branch};
        } else {
            modern_without_probe_ex = true;
        }
        // Named only when the other ten were all placed: otherwise a missing
        // function is the reason, and the log says that.
        bool others_placed = true;
        for (unsigned i = 0; i < GC_EXI_HOOKS; ++i) {
            if (i != GC_EXI_PROBE_EX && out[i] == 0) others_placed = false;
        }
        if (others_placed) refusal = why;
    }
    for (unsigned i = 0; i < GC_EXI_HOOKS; ++i) {
        const bool may_be_absent = i == GC_EXI_PROBE_EX && (old_probe || modern_without_probe_ex);
        if (out[i] == 0 && !may_be_absent) return false;
    }
    probe_ex_absent = out[GC_EXI_PROBE_EX] == 0;
    return true;
}

unsigned placed(const std::array<std::uint32_t, kGcExiHooks>& hooks) {
    return static_cast<unsigned>(std::count_if(hooks.begin(), hooks.end(), [](std::uint32_t x) { return x != 0; }));
}

// One block size: every table with more than seven users is tried (a cheap
// filter, never the choice), and the result is taken only when exactly one
// table resolves every function. Unrelated game tables can have far more
// users of the same stride than the EXI blocks (Need for Speed: Underground
// 2 USA has one with 60 against the EXI blocks' 16), so the count cannot
// pick; and two tables that both resolve are doubtful.
struct LayoutResult {
    unsigned tables_tried = 0;
    unsigned resolving_tables = 0;
    // The first table that resolved every function, with its hooks.
    std::uint32_t first_blocks = 0;
    std::array<std::uint32_t, kGcExiHooks> first_hooks{};
    bool probe_ex_absent = false;
    // Of the tables that did not resolve, the one placing the most functions
    // (for the log only).
    std::uint32_t best_partial_blocks = 0;
    std::array<std::uint32_t, kGcExiHooks> best_partial{};
    Refusal best_partial_refusal;
};

LayoutResult find_exi_functions_by(const Words& code, std::uint32_t index_word, const BlockTables& tables,
                                   bool probe_ex_may_be_absent) {
    LayoutResult result;
    for (const auto& [table, users] : tables.users) {
        if (users <= 7) continue;
        ++result.tables_tried;
        std::array<std::uint32_t, kGcExiHooks> hooks{};
        bool absent = false;
        Refusal refusal;
        if (resolve_exi_functions(code, index_word, table, tables, probe_ex_may_be_absent, hooks, absent, refusal)) {
            if (++result.resolving_tables == 1) {
                result.first_blocks = table;
                result.first_hooks = hooks;
                result.probe_ex_absent = absent;
            }
        } else if (result.best_partial_blocks == 0 || placed(hooks) > placed(result.best_partial)) {
            result.best_partial_blocks = table;
            result.best_partial = hooks;
            result.best_partial_refusal = refusal;
        }
    }
    return result;
}

// Games whose SDK links no EXIProbeEx with the 0x40-byte blocks. Their EXI
// library carries no version string to tell them by (Resident Evil 2 USA's
// OS strings are the same as Super Mario Sunshine's, which has EXIProbeEx),
// so they are listed by ID. Other regions of these games join the list only
// once their code has been checked in tools/gc_exi_scan.cpp.
bool links_no_probe_ex(const std::string& game_id) {
    static const char* const kGames[] = {
        "GHAE08",  // Resident Evil 2 (USA), #318
    };
    for (const char* id : kGames) {
        if (game_id.compare(0, 6, id) == 0) return true;  // a shorter ID never compares equal
    }
    return false;
}

}  // namespace

const char* gc_exi_function_name(unsigned index) {
    static const char* const kNames[kGcExiHooks] = {"EXIImm",    "EXIDma",     "EXISync",  "EXISelect",
                                                    "EXIDeselect", "EXILock",  "EXIUnlock", "EXIProbe",
                                                    "EXIProbeEx", "EXIAttach", "EXIGetID"};
    return index < kGcExiHooks ? kNames[index] : "?";
}

GcExiFunctions find_gc_exi_functions(const std::vector<CodeRange>& text, const std::string& game_id) {
    const Words code(text);
    // Both layouts, each on its own: one table across the two must resolve
    // every function, and a second one anywhere makes the choice doubtful.
    const BothTables tables = find_block_tables(code);
    const LayoutResult modern = find_exi_functions_by(code, kModernIndex, tables.modern, links_no_probe_ex(game_id));
    const LayoutResult old_sdk = find_exi_functions_by(code, kOldIndex, tables.old, false);
    GcExiFunctions found;
    found.tables_tried = modern.tables_tried + old_sdk.tables_tried;
    found.resolving_tables = modern.resolving_tables + old_sdk.resolving_tables;
    const LayoutResult& first_resolving = modern.resolving_tables != 0 ? modern : old_sdk;
    if (found.resolving_tables == 1) {
        found.blocks = first_resolving.first_blocks;
        found.hooks = first_resolving.first_hooks;
        found.probe_ex_absent = first_resolving.probe_ex_absent;
        found.old_sdk = &first_resolving == &old_sdk;
        return found;
    }
    if (found.resolving_tables > 1) {
        // The log names the first table that resolved, never a decoy's
        // partial set: both full sets were there, the choice was the doubt.
        found.partial = first_resolving.first_hooks;
        found.partial_blocks = first_resolving.first_blocks;
        return found;
    }
    const bool old_placed_more = old_sdk.best_partial_blocks != 0 &&
                                 (modern.best_partial_blocks == 0 ||
                                  placed(old_sdk.best_partial) > placed(modern.best_partial));
    const LayoutResult& best = old_placed_more ? old_sdk : modern;
    found.partial = best.best_partial;
    found.partial_blocks = best.best_partial_blocks;
    found.listed_refusal = best.best_partial_refusal.reason;
    found.listed_refusal_at = best.best_partial_refusal.at;
    return found;
}

GcExiDecision decide_gc_exi_hooks(const GcExiFunctions& found, const std::vector<CodeRange>& text) {
    GcExiDecision decision;
    if (found.blocks == 0) {
        if (found.resolving_tables > 1) {
            decision.verdict = GcExiVerdict::SeveralTables;
        } else if (found.tables_tried == 0) {
            decision.verdict = GcExiVerdict::NoTables;
        } else if (found.listed_refusal != GcExiFunctions::ListedRefusal::None) {
            decision.verdict = GcExiVerdict::ListedRefused;
        } else {
            decision.verdict = GcExiVerdict::NotAllFound;
        }
        return decision;
    }
    for (unsigned i = 0; i < kGcExiHooks; ++i) {
        const bool may_be_absent = i == GC_EXI_PROBE_EX && found.probe_ex_absent;
        if (found.hooks[i] == 0 && !may_be_absent) {
            decision.verdict = GcExiVerdict::Incomplete;
            return decision;
        }
    }
    // Every hooked function at least kGcExiHookSpan from every other: two
    // roles on one address would replay the first hook's branch (the word
    // the second one copies) and loop, and one 4 bytes on would be jumped
    // into by the first one's replay.
    for (unsigned i = 0; i < kGcExiHooks; ++i) {
        for (unsigned j = i + 1; j < kGcExiHooks; ++j) {
            const std::uint32_t a = found.hooks[i];
            const std::uint32_t b = found.hooks[j];
            if (a == 0 || b == 0) continue;
            const std::uint32_t apart = a > b ? a - b : b - a;
            if (apart < kGcExiHookSpan) {
                decision.verdict = GcExiVerdict::Overlapping;
                decision.overlap_first = i;
                decision.overlap_second = j;
                return decision;
            }
        }
    }
    // The hook replays the first instruction elsewhere, so it must not be a
    // relative branch (b or bc), which would go somewhere else from there.
    const Words code(text);
    bool any_branch = false;
    for (unsigned i = 0; i < kGcExiHooks; ++i) {
        const std::uint32_t at = found.hooks[i];
        if (at == 0) continue;
        const unsigned op = code.at(at) >> 26;
        decision.branching[i] = op == 18 || op == 16;
        any_branch = any_branch || decision.branching[i];
    }
    decision.verdict = any_branch ? GcExiVerdict::StartsWithBranch : GcExiVerdict::Hook;
    return decision;
}

}  // namespace riftwii
