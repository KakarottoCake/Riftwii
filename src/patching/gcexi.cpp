// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcexi.hpp"

#include <algorithm>
#include <map>

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
// uses and EXIGetID, EXIProbeEx returning -1 too. Returns the control
// blocks' address, 0 when anything is missing or doubtful.
//
// The 2001 SDK (Luigi's Mansion USA) differs: its blocks are 0x38 bytes,
// indexed with mulli by 0x38; its EXIGetID does not read the probe times
// (it is the one function calling lock, select and EXIImm); and it has no
// EXIProbeEx, its EXIProbe being the probe select calls. EXIProbeEx is then
// left 0, and the other ten are hooked.
//
// One block size: `index_word` is the instruction that turns a channel
// into its block's offset.
std::uint32_t find_exi_functions_by(const Words& code, std::uint32_t index_word,
                                   std::array<std::uint32_t, kGcExiHooks>& out) {
    out.fill(0);
    // The control blocks: lis/addi pairs near the index instruction at a
    // function's start.
    std::map<std::uint32_t, unsigned> bases;
    std::map<std::uint32_t, std::uint32_t> base_of;  // function -> its base
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if ((w & 0xFC00FFFF) != index_word) return;
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
                    bases[value]++;
                    base_of[f] = value;
                    return;
                }
            }
        }
    });
    std::uint32_t ecb = 0;
    unsigned most = 7;
    for (auto& [b, n] : bases)
        if (n > most) {
            most = n;
            ecb = b;
        }
    if (ecb == 0) return 0;
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
    for (auto& [f, b] : base_of) {
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
    const bool old_sdk = index_word == 0x1C000038;  // the 0x38-byte blocks
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
    for (unsigned i = 0; i < GC_EXI_HOOKS; ++i) {
        if (out[i] == 0 && !(i == GC_EXI_PROBE_EX && old_probe)) return 0;
    }
    return ecb;
}

}  // namespace

GcExiFunctions find_gc_exi_functions(const std::vector<CodeRange>& text) {
    const Words code(text);
    GcExiFunctions found;
    // Try both layouts independently: unrelated game tables may have more
    // users of one stride than the EXI blocks have.
    found.blocks = find_exi_functions_by(code, 0x54003032, found.hooks);
    if (found.blocks == 0) found.blocks = find_exi_functions_by(code, 0x1C000038, found.hooks);
    if (found.blocks == 0) return {};
    return found;
}

}  // namespace riftwii
