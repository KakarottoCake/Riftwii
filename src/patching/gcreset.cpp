// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcreset.hpp"

#include <algorithm>
#include <cstring>
#include <set>

namespace riftwii {
namespace {

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
        for (const CodeRange& r : text_)
            for (std::uint32_t i = 0; i + 4 <= r.size; i += 4) f(r.address + i, at(r.address + i));
    }

private:
    const std::uint8_t* find(std::uint32_t a) const {
        for (const CodeRange& r : text_)
            if (a >= r.address && a + 4 <= r.address + r.size) return r.bytes + (a - r.address);
        return nullptr;
    }
    const std::vector<CodeRange>& text_;
};

constexpr std::uint32_t kBlr = 0x4E800020;
constexpr std::uint32_t kMflrR0 = 0x7C0802A6;
constexpr std::uint32_t kOriPiBase = 0x61083000;    // ori r8,r8,0x3000
constexpr std::uint32_t kLiThree = 0x38800003;      // li r4,3
constexpr std::uint32_t kStoreThree = 0x90880024;   // stw r4,0x24(r8)
constexpr std::uint32_t kStoreCode = 0x90680024;    // stw r3,0x24(r8)
// How far back a function's first word is looked for, and how far into a
// function its argument's copy and the calls are.
constexpr std::uint32_t kFunctionReach = 0x400;
constexpr std::uint32_t kArgumentReach = 0x20;

unsigned opcode(std::uint32_t w) { return w >> 26; }

// The first word of the function `a` is in: its `mflr r0`, or the word
// after the `blr` before it, whichever is nearer; 0 if neither is near.
// The reset routine is hand-written and ends in a loop, not a `blr`, so
// the function after it is found by its `mflr r0`.
std::uint32_t function_start(const Words& code, std::uint32_t a) {
    for (std::uint32_t b = a; b + kFunctionReach > a && code.has(b); b -= 4) {
        if (code.at(b) == kMflrR0) return b;
        if (code.has(b - 4) && code.at(b - 4) == kBlr) return b;
    }
    return 0;
}

// Where a `bl` at `a` goes, 0 if the word is not one.
std::uint32_t bl_target(std::uint32_t a, std::uint32_t w) {
    if (opcode(w) != 18 || (w & 3) != 1) return 0;
    std::int32_t offset = static_cast<std::int32_t>(w & 0x03FFFFFC);
    if (offset & 0x02000000) offset -= 0x04000000;
    return a + static_cast<std::uint32_t>(offset);
}

// The functions with a `bl` to `target`.
std::set<std::uint32_t> callers_of(const Words& code, std::uint32_t target) {
    std::set<std::uint32_t> found;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if (bl_target(a, w) != target) return;
        if (const std::uint32_t start = function_start(code, a)) found.insert(start);
    });
    return found;
}

// `mr rN,r3` (or rN,r3,r3): N, or 0 when the word is not one.
unsigned copies_r3(std::uint32_t w) {
    const bool is_mr = opcode(w) == 31 && ((w >> 1) & 0x3FF) == 444 && ((w >> 21) & 31) == 3 && ((w >> 11) & 31) == 3;
    return is_mr ? (w >> 16) & 31 : 0;
}

// `cmpwi rN,value` (cmpi with any CR field).
bool compares(std::uint32_t w, unsigned reg, std::uint16_t value) {
    return opcode(w) == 11 && ((w >> 16) & 31) == reg && (w & 0xFFFF) == value && ((w >> 21) & 3) == 0;
}

// Whether the function at `start` keeps r3 and compares it with 2 and 1,
// as OSResetSystem does with its `reset`.
bool looks_like_reset_system(const Words& code, std::uint32_t start) {
    unsigned kept = 0;
    for (std::uint32_t a = start; a < start + kArgumentReach && code.has(a) && kept == 0; a += 4)
        kept = copies_r3(code.at(a));
    if (kept == 0) return false;
    bool with_two = false, with_one = false;
    for (std::uint32_t a = start; a < start + kFunctionReach && code.has(a); a += 4) {
        const std::uint32_t w = code.at(a);
        if (compares(w, kept, 2)) with_two = true;
        if (compares(w, kept, 1)) with_one = true;
        if (w == kBlr) break;
    }
    return with_two && with_one;
}

}  // namespace

GcResetFunctions find_gc_reset_functions(const std::vector<CodeRange>& text) {
    const Words code(text);
    std::set<std::uint32_t> routines;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if (w != kOriPiBase || code.at(a + 4) != kLiThree || code.at(a + 8) != kStoreThree ||
            code.at(a + 12) != kStoreCode)
            return;
        if (const std::uint32_t start = function_start(code, a)) routines.insert(start);
    });
    GcResetFunctions out;
    if (routines.size() != 1) return out;
    const std::uint32_t routine = *routines.begin();
    // Its callers, and theirs once more (__OSDoHotReset between).
    std::set<std::uint32_t> candidates = callers_of(code, routine);
    for (std::uint32_t between : std::set<std::uint32_t>(candidates))
        for (std::uint32_t caller : callers_of(code, between)) candidates.insert(caller);
    std::uint32_t reset_system = 0;
    unsigned matches = 0;
    for (std::uint32_t candidate : candidates) {
        if (!looks_like_reset_system(code, candidate)) continue;
        reset_system = candidate;
        ++matches;
    }
    if (matches != 1) return out;
    out.reset_system = reset_system;
    out.hardware_reset = routine;
    return out;
}

std::uint32_t find_gc_boot_dol(const std::vector<CodeRange>& text, const std::vector<CodeRange>& data) {
    static const char kNewApploaderDate[] = "2004/02/01";
    const std::size_t date_bytes = sizeof(kNewApploaderDate) - 1;
    std::uint32_t date = 0;
    for (const CodeRange& r : data) {
        for (std::size_t i = 0; date == 0 && i + date_bytes <= r.size; ++i)
            if (std::memcmp(r.bytes + i, kNewApploaderDate, date_bytes) == 0)
                date = r.address + static_cast<std::uint32_t>(i);
    }
    if (date == 0) return 0;
    // lis takes the upper half rounded for addi's signed lower half.
    const std::uint16_t low = static_cast<std::uint16_t>(date & 0xFFFF);
    const std::uint16_t high = static_cast<std::uint16_t>((date >> 16) + ((low & 0x8000) ? 1 : 0));
    const Words code(text);
    std::set<std::uint32_t> functions;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        const bool lis = opcode(w) == 15 && ((w >> 16) & 31) == 0 && (w & 0xFFFF) == high;
        if (!lis) return;
        const unsigned reg = (w >> 21) & 31;
        for (std::uint32_t b = a + 4; b <= a + 64 && code.has(b); b += 4) {
            const std::uint32_t next = code.at(b);
            const bool addi = opcode(next) == 14 && ((next >> 16) & 31) == reg && (next & 0xFFFF) == low;
            if (!addi) continue;
            if (const std::uint32_t f = function_start(code, a)) functions.insert(f);
            break;
        }
    });
    // The one of them that keeps its seventh argument.
    std::uint32_t found = 0;
    unsigned keeping = 0;
    for (std::uint32_t f : functions) {
        bool keeps_argv = false;
        for (std::uint32_t a = f; a < f + 24 * 4 && code.has(a) && !keeps_argv; a += 4) {
            const std::uint32_t w = code.at(a);
            keeps_argv = opcode(w) == 31 && ((w >> 21) & 31) == 9 && ((w >> 11) & 31) == 9 && ((w >> 1) & 1023) == 444;
        }
        if (keeps_argv) {
            found = f;
            ++keeping;
        }
    }
    return keeping == 1 ? found : 0;
}

bool gc_pack_exec_args(std::vector<std::uint8_t>& bi2, const std::vector<std::string>& args) {
    constexpr std::size_t kBi2Bytes = 0x2000;
    constexpr std::size_t kUpperHalf = 0x1000;
    if (bi2.size() != kBi2Bytes) return false;
    auto put32 = [&](std::size_t at, std::uint32_t v) {
        bi2[at] = static_cast<std::uint8_t>(v >> 24);
        bi2[at + 1] = static_cast<std::uint8_t>(v >> 16);
        bi2[at + 2] = static_cast<std::uint8_t>(v >> 8);
        bi2[at + 3] = static_cast<std::uint8_t>(v);
    };
    if (args.empty()) {
        put32(8, 0);
        return true;
    }
    std::vector<std::uint8_t> packed(kBi2Bytes - kUpperHalf, 0);
    // The strings from the top down, the last first, as the SDK does.
    std::size_t at = kBi2Bytes;
    std::vector<std::uint32_t> offsets(args.size());
    for (std::size_t i = args.size(); i-- > 0;) {
        const std::size_t need = args[i].size() + 1;
        if (at < kUpperHalf + need) return false;
        at -= need;
        offsets[i] = static_cast<std::uint32_t>(at);
    }
    // Then, word aligned below them, argc and the list ending in 0.
    std::size_t list = (at & ~std::size_t(3)) - 4 * (args.size() + 1);
    if (list < kUpperHalf + 4 || list > kBi2Bytes) return false;
    const std::size_t argc_at = list - 4;
    std::fill(bi2.begin() + kUpperHalf, bi2.end(), 0);
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::copy(args[i].begin(), args[i].end(), bi2.begin() + offsets[i]);
        put32(list + 4 * i, offsets[i]);
    }
    put32(list + 4 * args.size(), 0);
    put32(argc_at, static_cast<std::uint32_t>(args.size()));
    put32(8, static_cast<std::uint32_t>(argc_at));
    return true;
}

GcRestartPlan plan_gc_restart(const std::vector<GcRestartPiece>& pieces, std::uint32_t window,
                              std::uint32_t window_bytes, std::uint32_t max_regions) {
    constexpr std::uint32_t kMem1 = 0x80000000;
    constexpr std::uint32_t kMem1End = 0x81800000;
    constexpr std::uint32_t kRegionBytes = 12;
    GcRestartPlan none;
    if (pieces.size() > max_regions) return none;
    const std::uint64_t window_end = std::uint64_t(window) + window_bytes;
    std::uint64_t next = (std::uint64_t(window) + std::uint64_t(max_regions) * kRegionBytes + 31) & ~std::uint64_t(31);
    GcRestartPlan plan;
    plan.table = window;
    for (const GcRestartPiece& piece : pieces) {
        const std::uint64_t piece_end = std::uint64_t(piece.address) + piece.length;
        if (piece.length == 0 || piece.address < kMem1 || piece_end > kMem1End) return none;
        GcRestartRegion region;
        region.address = piece.address;
        region.length = piece.length;
        if (!piece.zero) {
            if (next + piece.length > window_end) return none;
            region.source = static_cast<std::uint32_t>(next);
            next = (next + piece.length + 31) & ~std::uint64_t(31);
        }
        plan.regions.push_back(region);
    }
    plan.end = static_cast<std::uint32_t>(next);
    return plan;
}

}  // namespace riftwii
