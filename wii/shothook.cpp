// SPDX-License-Identifier: GPL-3.0-or-later
#include "shothook.hpp"

#include <gccore.h>
#include <ogc/cache.h>

#include <cstdio>
#include <cstring>

#include "log.hpp"
#include "riftwii/hook.hpp"
#include "riftwii/symsearch.hpp"
#include "riftwii_shot_bin.h"
#include "shot_hook.h"

namespace riftwii::wii {
namespace {

constexpr std::uint32_t kMem2ArenaEndField = 0x80003128;
constexpr std::uint32_t kBusClockField = 0x800000F8;  // the time base runs at a quarter of it
constexpr unsigned kScratchRegister = 12;             // shot_entry.S's jump back
constexpr std::uint32_t kNop = 0x60000000;

std::uint32_t align_up(std::uint32_t v) { return (v + 31) & ~31u; }

const rt_shot_header& header() { return *reinterpret_cast<const rt_shot_header*>(riftwii_shot_bin); }

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

// A plain `b` (not bl, not absolute): another blob's hook.
bool is_plain_branch(std::uint32_t word, std::uint32_t at, std::uint32_t& target) {
    if ((word & 0xFC000003u) != 0x48000000u) return false;
    std::int32_t delta = static_cast<std::int32_t>(word & 0x03FFFFFCu);
    if (delta & 0x02000000) delta -= 0x04000000;
    target = at + static_cast<std::uint32_t>(delta);
    return true;
}

}  // namespace

bool plan_shot_hook(const DolHeader& dol, std::uint32_t arena1_hi, std::uint32_t mem1_floor,
                    std::uint32_t arena2_lo, const ShotIpc& known, std::uint32_t pad_read, bool demo, bool direct,
                    const std::vector<MemoryPatch>& patches, ShotHook& out, std::string& why) {
    out = ShotHook{};
    out.demo = demo;
    const rt_shot_header& h = header();
    if (riftwii_shot_bin_size < sizeof(rt_shot_header) || h.magic != RT_SHOT_MAGIC || h.version != RT_SHOT_VERSION ||
        h.size > riftwii_shot_bin_size || h.context_offset + RT_SHOT_CONTEXT_BYTES > h.size) {
        why = "the embedded screenshot blob is damaged";
        return false;
    }

    // 1. The game's IOS calls: the NAND ones the blob makes, and the
    //    IOS_IoctlvAsync its Bluetooth reads go through.
    std::vector<CodeRange> text;
    for (std::size_t i = 0; i < kDolTextSections; ++i) {
        const DolSection& s = dol.sections[i];
        if (s.used()) text.push_back({s.address, reinterpret_cast<const std::uint8_t*>(s.address), s.size});
    }
    ShotIpc ipc = known;
    if (!ipc.open_async || !ipc.close_async || !ipc.write_async || !ipc.ioctl_async || !ipc.ioctlv_async) {
        IpcSymbols symbols;
        IpcApi api;
        std::string error;
        if (!find_ipc_symbols(text, symbols, error)) {
            why = "the game's IOS calls were not found: " + error;
            return false;
        }
        if (!find_ipc_api(text, symbols, api, error)) {
            why = "the game's IOS calls were not found: " + error;
            return false;
        }
        if (!ipc.open_async) ipc.open_async = api.async[kIpcOpen];
        if (!ipc.close_async) ipc.close_async = api.async[kIpcClose];
        if (!ipc.write_async) ipc.write_async = api.async[kIpcWrite];
        if (!ipc.ioctl_async) ipc.ioctl_async = api.async[kIpcIoctlCmd] ? api.async[kIpcIoctlCmd] : symbols.ioctl_async;
        if (!ipc.ioctlv_async) ipc.ioctlv_async = api.async[kIpcIoctlvCmd] ? api.async[kIpcIoctlvCmd] : symbols.ioctlv_async;
    }
    if (!ipc.open_async || !ipc.close_async || !ipc.write_async || !ipc.ioctl_async) {
        why = "the game has no asynchronous IOS open, close, write or ioctl to save with";
        return false;
    }
    out.ioctlv = ipc.ioctlv_async;
    if (pad_read == 0) {
        PadSymbols pad;
        std::string error;
        if (find_pad_symbols(text, pad, error)) pad_read = pad.read;
    }
    out.pad_read = pad_read;
    if (out.ioctlv == 0 && out.pad_read == 0) {
        why = "neither the game's IOS_IoctlvAsync nor a PADRead was found";
        return false;
    }

    // 2. Memory: the blob below the MEM1 arena's top, the state and the
    //    frame at the bottom of the MEM2 arena.
    out.code_bytes = align_up(h.size);
    out.code_base = (arena1_hi - out.code_bytes) & ~31u;
    const std::uint32_t state_bytes = align_up(sizeof(rt_shot_state));
    out.state_bytes = state_bytes + (direct ? 0u : align_up(RTSHOT_FRAME_BYTES));
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
        if (overlaps(p, out.code_base, out.code_bytes) || overlaps(p, out.state_base, out.state_bytes)) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "a pack's memory patch at 0x%08x uses the memory it needs",
                          static_cast<unsigned>(p.offset));
            why = buf;
            return false;
        }
    }
    out.new_arena1_hi = out.code_base;
    out.new_arena2_lo = out.state_base + out.state_bytes;

    // 3. The blob and its context; the hooks come last. The state is
    //    zeroed by the blob itself at its first call: MEM2's bottom is
    //    still this loader's until the jump.
    std::memset(reinterpret_cast<void*>(out.code_base), 0, out.code_bytes);
    std::memcpy(reinterpret_cast<void*>(out.code_base), riftwii_shot_bin, h.size);
    rt_shot_context* ctx = reinterpret_cast<rt_shot_context*>(out.code_base + h.context_offset);
    if (ctx->magic != RT_SHOT_CONTEXT_MAGIC) {
        why = "the screenshot blob's context is not where its header says";
        return false;
    }
    ctx->state = out.state_base;
    ctx->frame = direct ? 0u : out.state_base + state_bytes;
    ctx->open_async = ipc.open_async;
    ctx->close_async = ipc.close_async;
    ctx->write_async = ipc.write_async;
    ctx->ioctl_async = ipc.ioctl_async;
    ctx->complete_bt = out.code_base + h.complete_bt;
    ctx->complete_nand = out.code_base + h.complete_nand;
    ctx->flags = (demo ? RT_SHOT_FLAG_DEMO : 0u) | (direct ? RT_SHOT_FLAG_DIRECT : 0u);
    ctx->ticks_per_second = *reinterpret_cast<volatile std::uint32_t*>(kBusClockField) / 4u;
    ctx->number = 1;
    out.active = true;
    logf("Screenshots: blob %u bytes at 0x%08x, state%s %u bytes at 0x%08x; IOS_IoctlvAsync 0x%08x, "
         "PADRead 0x%08x%s\n",
         h.size, out.code_base, direct ? " (direct: no frame copy)" : " and frame", out.state_bytes, out.state_base,
         out.ioctlv, out.pad_read, demo ? " (demo)" : "");
    return true;
}

bool install_shot_hook(ShotHook& hook, std::string& why) {
    if (!hook.active) return false;
    const rt_shot_header& h = header();
    struct Site {
        std::uint32_t function, hook, replay, resume;
        const char* name;
    } sites[2] = {{hook.ioctlv, h.hook_ioctlv, h.replay_ioctlv, h.continue_ioctlv, "IOS_IoctlvAsync"},
                  {hook.pad_read, h.hook_pad, h.replay_pad, h.continue_pad, "PADRead"}};
    // Checked now, after the packs' patches, the cheats and the adapter.
    unsigned ready = 0;
    for (Site& s : sites) {
        if (s.function == 0) continue;
        const std::uint32_t first = *reinterpret_cast<const std::uint32_t*>(s.function);
        std::uint32_t replay[4] = {first, kNop, kNop, kNop};
        std::uint32_t target = 0;
        std::string reason;
        if (is_plain_branch(first, s.function, target)) {
            // Another hook already: the replay takes the same branch.
            if (!encode_branch(hook.code_base + s.replay, target, replay[0])) {
                logf("Screenshots: %s's hook at 0x%08x is out of reach; not watched\n", s.name, target);
                s.function = 0;
                continue;
            }
            logf("Screenshots: %s already hooked (to 0x%08x); chained\n", s.name, target);
        } else if (!displaceable(first, kScratchRegister, reason)) {
            logf("Screenshots: %s's first instruction (0x%08x) cannot be moved (%s); not watched\n", s.name, first,
                 reason.c_str());
            s.function = 0;
            continue;
        }
        store_words(hook.code_base + s.replay, replay, 4);
        const auto resume = encode_absolute_jump(kScratchRegister, s.function + 4);
        store_words(hook.code_base + s.resume, resume.data(), 4);
        ++ready;
    }
    if (ready == 0) {
        why = "nothing could be hooked";
        hook.active = false;
        return false;
    }
    sync_code(hook.code_base, hook.code_bytes);
    unsigned hooked = 0;
    for (const Site& s : sites) {
        if (s.function == 0) continue;
        std::uint32_t branch = 0;
        if (!encode_branch(s.function, hook.code_base + s.hook, branch)) {
            logf("Screenshots: %s is out of a branch's reach\n", s.name);
            continue;
        }
        store_words(s.function, &branch, 1);
        sync_code(s.function & ~31u, 32);
        ++hooked;
    }
    if (hooked == 0) {
        why = "no branch could reach the blob";
        hook.active = false;
        return false;
    }
    logf("Screenshots: hooked%s%s\n", sites[0].function ? " IOS_IoctlvAsync (Wii Remote: hold 1, press HOME)" : "",
         sites[1].function ? " PADRead (GameCube: hold L and R, press Down)" : "");
    return true;
}

}  // namespace riftwii::wii
