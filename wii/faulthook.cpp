// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "faulthook.hpp"

#include <gccore.h>
#include <ogc/cache.h>

#include <malloc.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "fault_hook.h"
#include "log.hpp"
#include "riftwii/hook.hpp"
#include "riftwii/symsearch.hpp"
#include "riftwii_fault_bin.h"

namespace riftwii::wii {
namespace {

constexpr std::uint32_t kMem2ArenaEndField = 0x80003128;
constexpr std::uint32_t kBusClockField = 0x800000F8;  // the time base runs at a quarter of it
constexpr unsigned kScratchRegister = 12;             // fault_entry.S's jump back
constexpr std::uint32_t kNop = 0x60000000;

std::uint32_t align_up(std::uint32_t v) { return (v + 31) & ~31u; }

const rt_fault_header& header() { return *reinterpret_cast<const rt_fault_header*>(riftwii_fault_bin); }

void store_words(std::uint32_t address, const std::uint32_t* words, std::size_t count) {
    volatile std::uint32_t* p = reinterpret_cast<volatile std::uint32_t*>(address);
    for (std::size_t i = 0; i < count; ++i) p[i] = words[i];
}

void sync_code(std::uint32_t address, std::uint32_t bytes) {
    DCFlushRange(reinterpret_cast<void*>(address), bytes);
    ICInvalidateRange(reinterpret_cast<void*>(address), bytes);
}

// A plain `b` (not bl, not absolute): another blob's hook.
bool is_plain_branch(std::uint32_t word, std::uint32_t at, std::uint32_t& target) {
    if ((word & 0xFC000003u) != 0x48000000u) return false;
    std::int32_t delta = static_cast<std::int32_t>(word & 0x03FFFFFCu);
    if (delta & 0x02000000) delta -= 0x04000000;
    target = at + static_cast<std::uint32_t>(delta);
    return true;
}

bool overlaps(const MemoryPatch& p, std::uint32_t start, std::uint32_t bytes) {
    if (!p.has_offset || p.value.empty()) return false;
    return p.offset < static_cast<std::uint64_t>(start) + bytes && p.offset + p.value.size() > start;
}

}  // namespace

bool plan_fault_hook(const DolHeader& dol, std::uint32_t arena1_hi, std::uint32_t mem1_floor, std::uint32_t arena2_lo,
                     std::uint32_t mem1_veneers, bool answer_bca, const std::vector<MemoryPatch>& patches,
                     FaultHook& out, std::string& why) {
    if (out.stage) std::free(out.stage);
    out = FaultHook{};
    const rt_fault_header& h = header();
    if (riftwii_fault_bin_size < sizeof(rt_fault_header) || h.magic != RT_FAULT_MAGIC ||
        h.version != RT_FAULT_VERSION || h.size > riftwii_fault_bin_size ||
        h.context_offset + RT_FAULT_CONTEXT_BYTES > h.size) {
        why = "the embedded crash blob is damaged";
        return false;
    }

    // 1. The functions: from the game as loaded. Either one is enough.
    std::vector<CodeRange> text, all;
    for (std::size_t i = 0; i < kDolSections; ++i) {
        const DolSection& s = dol.sections[i];
        if (!s.used()) continue;
        const CodeRange r{s.address, reinterpret_cast<const std::uint8_t*>(s.address), s.size};
        all.push_back(r);
        if (i < kDolTextSections) text.push_back(r);
    }
    std::string crash_why, bca_why;
    if (!find_unhandled_exception(text, all, out.function, crash_why)) {
        out.function = 0;
        logf("Game crashes: not recorded: %s\n", crash_why.c_str());
    }
    if (answer_bca) {
        IpcSymbols symbols;
        if (find_ipc_symbols(text, symbols, bca_why)) out.ioctl = symbols.ioctl_async;
        else logf("BCA: not answered: the game's IOS_IoctlAsync was not found: %s\n", bca_why.c_str());
    }
    if (out.function == 0 && out.ioctl == 0) {
        why = "nothing to hook";
        return false;
    }

    // 2. Memory: the blob below the MEM1 arena's top, its state at the
    //    bottom of the MEM2 arena.
    out.code_bytes = align_up(h.size);
    out.state_bytes = align_up(sizeof(rt_fault_state));
    const bool in_mem2 = mem1_veneers != 0;
    if (in_mem2) {
        out.code_base = align_up(arena2_lo);
        out.state_base = out.code_base + out.code_bytes;
    } else {
        out.code_base = (arena1_hi - out.code_bytes) & ~31u;
        out.state_base = align_up(arena2_lo);
    }
    const std::uint32_t arena2_end = *reinterpret_cast<volatile std::uint32_t*>(kMem2ArenaEndField);
    if ((!in_mem2 && (arena1_hi < out.code_bytes || out.code_base < mem1_floor)) ||
        out.state_base + out.state_bytes > arena2_end) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "no room (MEM1 arena top 0x%08x, floor 0x%08x; MEM2 arena 0x%08x-0x%08x)",
                      arena1_hi, mem1_floor, arena2_lo, arena2_end);
        why = buf;
        return false;
    }
    for (const MemoryPatch& p : patches) {
        if (overlaps(p, out.code_base, out.code_bytes) || overlaps(p, out.state_base, out.state_bytes) ||
            (out.function != 0 && overlaps(p, out.function, 4)) || (out.ioctl != 0 && overlaps(p, out.ioctl, 4))) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "a pack's memory patch at 0x%08x uses the memory it needs",
                          static_cast<unsigned>(p.offset));
            why = buf;
            return false;
        }
    }
    out.new_arena1_hi = in_mem2 ? arena1_hi : out.code_base;
    out.new_arena2_lo = out.state_base + out.state_bytes;

    // 3. The blob and its context; the hook comes last. The state needs
    //    nothing now (MEM2's bottom is still this loader's until the
    //    jump): the blob fills what it uses when the game crashes. A blob
    //    for MEM2 is built in a staging copy until then.
    std::uint8_t* bytes = reinterpret_cast<std::uint8_t*>(out.code_base);
    if (in_mem2) {
        out.stage = static_cast<std::uint8_t*>(memalign(32, out.code_bytes));
        if (!out.stage) {
            why = "no memory to stage the blob for MEM2";
            return false;
        }
        out.veneers = mem1_veneers;
        bytes = out.stage;
    }
    std::memset(bytes, 0, out.code_bytes);
    std::memcpy(bytes, riftwii_fault_bin, h.size);
    rt_fault_context* ctx = reinterpret_cast<rt_fault_context*>(bytes + h.context_offset);
    if (ctx->magic != RT_FAULT_CONTEXT_MAGIC) {
        why = "the crash blob's context is not where its header says";
        return false;
    }
    ctx->state = out.state_base;
    ctx->stack_top = out.state_base + offsetof(rt_fault_state, stack) + RT_FAULT_STACK_BYTES - 16;
    ctx->ticks_per_second = *reinterpret_cast<volatile std::uint32_t*>(kBusClockField) / 4u;
    std::strncpy(ctx->version, RIFTWII_VERSION, sizeof(ctx->version) - 1);
    ctx->bca_sink = (out.code_base + h.sink + 31u) & ~31u;
    ctx->flags = out.ioctl != 0 ? RT_FAULT_FLAG_BCA : 0u;
    out.active = true;
    logf("Game crashes: blob %u bytes at 0x%08x%s, state %u bytes at 0x%08x; __OSUnhandledException 0x%08x%s\n",
         h.size, out.code_base, in_mem2 ? " (MEM2: a code build uses all of MEM1)" : "", out.state_bytes,
         out.state_base, out.function, out.ioctl != 0 ? "; it also answers the BCA read" : "");
    return true;
}

bool install_fault_hook(FaultHook& hook, std::string& why) {
    if (!hook.active) return false;
    const rt_fault_header& h = header();
    // Where a word of the blob is written now: its place, or the staging
    // copy until the jump.
    const auto now = [&](std::uint32_t address) {
        return hook.stage ? reinterpret_cast<std::uint32_t>(hook.stage) + (address - hook.code_base) : address;
    };
    struct Site {
        std::uint32_t& function;
        std::uint32_t hook, replay, resume;
        const char* name;
    } sites[2] = {{hook.function, h.hook, h.replay, h.resume, "__OSUnhandledException"},
                  {hook.ioctl, h.hook_ioctl, h.replay_ioctl, h.resume_ioctl, "IOS_IoctlAsync"}};
    // Checked now, after the packs' patches, the cheats and the other
    // blobs' hooks.
    for (Site& s : sites) {
        if (s.function == 0) continue;
        const std::uint32_t first = *reinterpret_cast<const std::uint32_t*>(s.function);
        std::uint32_t replay[4] = {first, kNop, kNop, kNop};
        std::uint32_t target = 0;
        std::string reason;
        if (is_plain_branch(first, s.function, target)) {
            // Another hook already: the replay takes the same branch, or
            // jumps there through r12 from MEM2 (free here, as for the
            // jump back).
            if (!encode_branch(hook.code_base + s.replay, target, replay[0])) {
                const auto jump = encode_absolute_jump(kScratchRegister, target);
                std::copy(jump.begin(), jump.end(), replay);
            }
        } else if (!displaceable(first, kScratchRegister, reason)) {
            logf("Game crashes: %s's first instruction (0x%08x) cannot be moved (%s); not hooked\n", s.name, first,
                 reason.c_str());
            s.function = 0;
            continue;
        }
        store_words(now(hook.code_base + s.replay), replay, 4);
        const auto resume = encode_absolute_jump(kScratchRegister, s.function + 4);
        store_words(now(hook.code_base + s.resume), resume.data(), 4);
    }
    if (!hook.stage) sync_code(hook.code_base, hook.code_bytes);  // else at the jump
    unsigned hooked = 0, veneers = 0;
    for (Site& s : sites) {
        if (s.function == 0) continue;
        std::uint32_t target = hook.code_base + s.hook;
        if (hook.veneers != 0) {
            const std::uint32_t veneer = hook.veneers + veneers++ * 16;
            const auto jump = encode_absolute_jump(kScratchRegister, target);
            store_words(veneer, jump.data(), 4);
            sync_code(veneer, 16);
            target = veneer;
        }
        std::uint32_t branch = 0;
        if (!encode_branch(s.function, target, branch)) {
            logf("Game crashes: %s is out of a branch's reach; not hooked\n", s.name);
            s.function = 0;
            continue;
        }
        store_words(s.function, &branch, 1);
        sync_code(s.function & ~31u, 32);
        ++hooked;
    }
    if (hooked == 0) {
        why = "nothing could be hooked";
        hook.active = false;
        return false;
    }
    logf("Game crashes: hooked%s%s\n", hook.function ? "; a crash is saved for the next start" : "",
         hook.ioctl ? "; the BCA read is answered as a retail disc's" : "");
    return true;
}

void place_fault_hook(FaultHook& hook) {
    if (!hook.stage) return;
    if (hook.active) {
        std::memcpy(reinterpret_cast<void*>(hook.code_base), hook.stage, hook.code_bytes);
        sync_code(hook.code_base, hook.code_bytes);
    }
    std::free(hook.stage);
    hook.stage = nullptr;
}

}  // namespace riftwii::wii
