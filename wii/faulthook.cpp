// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "faulthook.hpp"

#include <gccore.h>
#include <ogc/cache.h>

#include <cstdio>
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

bool overlaps(const MemoryPatch& p, std::uint32_t start, std::uint32_t bytes) {
    if (!p.has_offset || p.value.empty()) return false;
    return p.offset < static_cast<std::uint64_t>(start) + bytes && p.offset + p.value.size() > start;
}

}  // namespace

bool plan_fault_hook(const DolHeader& dol, std::uint32_t arena1_hi, std::uint32_t mem1_floor, std::uint32_t arena2_lo,
                     const std::vector<MemoryPatch>& patches, FaultHook& out, std::string& why) {
    out = FaultHook{};
    const rt_fault_header& h = header();
    if (riftwii_fault_bin_size < sizeof(rt_fault_header) || h.magic != RT_FAULT_MAGIC ||
        h.version != RT_FAULT_VERSION || h.size > riftwii_fault_bin_size ||
        h.context_offset + RT_FAULT_CONTEXT_BYTES > h.size) {
        why = "the embedded crash blob is damaged";
        return false;
    }

    // 1. The function: from the game as loaded.
    std::vector<CodeRange> text, all;
    for (std::size_t i = 0; i < kDolSections; ++i) {
        const DolSection& s = dol.sections[i];
        if (!s.used()) continue;
        const CodeRange r{s.address, reinterpret_cast<const std::uint8_t*>(s.address), s.size};
        all.push_back(r);
        if (i < kDolTextSections) text.push_back(r);
    }
    if (!find_unhandled_exception(text, all, out.function, why)) return false;

    // 2. Memory: the blob below the MEM1 arena's top, its state at the
    //    bottom of the MEM2 arena.
    out.code_bytes = align_up(h.size);
    out.code_base = (arena1_hi - out.code_bytes) & ~31u;
    out.state_bytes = align_up(sizeof(rt_fault_state));
    out.state_base = align_up(arena2_lo);
    const std::uint32_t arena2_end = *reinterpret_cast<volatile std::uint32_t*>(kMem2ArenaEndField);
    if (arena1_hi < out.code_bytes || out.code_base < mem1_floor || out.state_base + out.state_bytes > arena2_end) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "no room (MEM1 arena top 0x%08x, floor 0x%08x; MEM2 arena 0x%08x-0x%08x)",
                      arena1_hi, mem1_floor, arena2_lo, arena2_end);
        why = buf;
        return false;
    }
    for (const MemoryPatch& p : patches) {
        if (overlaps(p, out.code_base, out.code_bytes) || overlaps(p, out.state_base, out.state_bytes) ||
            overlaps(p, out.function, 4)) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "a pack's memory patch at 0x%08x uses the memory it needs",
                          static_cast<unsigned>(p.offset));
            why = buf;
            return false;
        }
    }
    out.new_arena1_hi = out.code_base;
    out.new_arena2_lo = out.state_base + out.state_bytes;

    // 3. The blob and its context; the hook comes last. The state needs
    //    nothing now (MEM2's bottom is still this loader's until the
    //    jump): the blob fills what it uses when the game crashes.
    std::memset(reinterpret_cast<void*>(out.code_base), 0, out.code_bytes);
    std::memcpy(reinterpret_cast<void*>(out.code_base), riftwii_fault_bin, h.size);
    rt_fault_context* ctx = reinterpret_cast<rt_fault_context*>(out.code_base + h.context_offset);
    if (ctx->magic != RT_FAULT_CONTEXT_MAGIC) {
        why = "the crash blob's context is not where its header says";
        return false;
    }
    ctx->state = out.state_base;
    ctx->stack_top = out.state_base + offsetof(rt_fault_state, stack) + RT_FAULT_STACK_BYTES - 16;
    ctx->ticks_per_second = *reinterpret_cast<volatile std::uint32_t*>(kBusClockField) / 4u;
    std::strncpy(ctx->version, RIFTWII_VERSION, sizeof(ctx->version) - 1);
    out.active = true;
    logf("Game crashes: blob %u bytes at 0x%08x, state %u bytes at 0x%08x; __OSUnhandledException 0x%08x\n", h.size,
         out.code_base, out.state_bytes, out.state_base, out.function);
    return true;
}

bool install_fault_hook(FaultHook& hook, std::string& why) {
    if (!hook.active) return false;
    const rt_fault_header& h = header();
    // Checked now, after the packs' patches and the cheats.
    const std::uint32_t first = *reinterpret_cast<const std::uint32_t*>(hook.function);
    std::string reason;
    if (!displaceable(first, kScratchRegister, reason)) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "__OSUnhandledException's first instruction (0x%08x) cannot be moved (%s)",
                      first, reason.c_str());
        why = buf;
        hook.active = false;
        return false;
    }
    const std::uint32_t replay[4] = {first, kNop, kNop, kNop};
    store_words(hook.code_base + h.replay, replay, 4);
    const auto resume = encode_absolute_jump(kScratchRegister, hook.function + 4);
    store_words(hook.code_base + h.resume, resume.data(), 4);
    sync_code(hook.code_base, hook.code_bytes);
    std::uint32_t branch = 0;
    if (!encode_branch(hook.function, hook.code_base + h.hook, branch)) {
        why = "__OSUnhandledException is out of a branch's reach";
        hook.active = false;
        return false;
    }
    store_words(hook.function, &branch, 1);
    sync_code(hook.function & ~31u, 32);
    logf("Game crashes: hooked; a crash is saved for the next start\n");
    return true;
}

}  // namespace riftwii::wii
