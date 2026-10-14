// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "wpadhook.hpp"

#include <gccore.h>
#include <ogc/cache.h>

#include <cstdio>
#include <cstring>

#include "loadersettings.hpp"
#include "log.hpp"
#include "riftwii/hook.hpp"
#include "riftwii/returnto.hpp"
#include "riftwii/symsearch.hpp"
#include "riftwii/wpadlog.hpp"
#include "riftwii_wlog_bin.h"
#include "wlog/wlog_hook.h"

namespace riftwii::wii {
namespace {

constexpr std::uint32_t kMem2ArenaEndField = 0x80003128;
// The Wii's bus clock; the time base runs at a quarter of it. A constant: boot.cpp
// writes it to 0x800000F8 only at the handover, after this is planned (read
// there, a launch from the RiftWii channel got 0).
constexpr std::uint32_t kBusClock = 0x0E7BE2C0;
constexpr std::uint32_t kPrintStubFirst = 0x9421FF90;  // stwu r1,-0x70(r1)

bool g_wiiu_pro_seen = false;

std::uint32_t align_up(std::uint32_t v) { return (v + 31) & ~31u; }

const rt_wlog_header& header() { return *reinterpret_cast<const rt_wlog_header*>(riftwii_wlog_bin); }

std::uint32_t load_word(std::uint32_t address) { return *reinterpret_cast<const volatile std::uint32_t*>(address); }

void store_word(std::uint32_t address, std::uint32_t word) {
    *reinterpret_cast<volatile std::uint32_t*>(address) = word;
}

void sync_code(std::uint32_t address, std::uint32_t bytes) {
    DCFlushRange(reinterpret_cast<void*>(address), bytes);
    ICInvalidateRange(reinterpret_cast<void*>(address), bytes);
}

bool overlaps(const MemoryPatch& p, std::uint32_t start, std::uint32_t bytes) {
    if (!p.has_offset || p.value.empty()) return false;
    return p.offset < static_cast<std::uint64_t>(start) + bytes && p.offset + p.value.size() > start;
}

// "li rZ,0": what __OSLaunchMenu's exit site holds until something else
// patches it.
bool is_li_zero(std::uint32_t word) { return (word & 0xFC1FFFFFu) == 0x38000000u; }

}  // namespace

void note_wiiu_pro_seen() { g_wiiu_pro_seen = true; }

bool wpad_log_wanted(std::string& why) {
    const auto it = Settings().other.find("wpad_log");
    const std::string mode = it == Settings().other.end() ? "auto" : it->second;
    if (mode == "on") {
        why = "wpad_log = on";
        return true;
    }
    if (mode == "off") {
        why = "wpad_log = off";
        return false;
    }
    why = g_wiiu_pro_seen ? "a Wii U Pro Controller was connected in the menu"
                          : "no Wii U Pro Controller was connected in the menu";
    return g_wiiu_pro_seen;
}

bool plan_wpad_log(const DolHeader& dol, std::uint32_t arena1_hi, std::uint32_t mem1_floor, std::uint32_t arena2_lo,
                   const std::uint32_t known_ios[4], const std::vector<MemoryPatch>& patches, WpadLogHook& out,
                   std::string& why) {
    out = WpadLogHook{};
    const rt_wlog_header& h = header();
    if (riftwii_wlog_bin_size < sizeof(rt_wlog_header) || h.magic != RT_WLOG_MAGIC ||
        h.version != RT_WLOG_VERSION || h.size > riftwii_wlog_bin_size ||
        h.context_offset + RT_WLOG_CONTEXT_BYTES > h.size) {
        why = "the embedded Wii Remote log blob is damaged";
        return false;
    }

    // 1. Where to hook: the print functions and the HOME Menu's exit.
    std::vector<CodeSpan> spans;
    std::vector<CodeRange> text;
    for (std::size_t i = 0; i < kDolTextSections; ++i) {
        const DolSection& s = dol.sections[i];
        if (!s.used()) continue;
        const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(s.address);
        spans.push_back(CodeSpan{const_cast<std::uint8_t*>(bytes), s.size, s.address});
        text.push_back(CodeRange{s.address, bytes, s.size});
    }
    out.stubs = find_print_stubs(spans);
    if (out.stubs.empty()) {
        why = "the game has no empty print functions";
        return false;
    }
    out.exit_site = find_launch_menu_exit(spans);
    if (out.exit_site == 0) {
        why = "__OSLaunchMenu was not found (the log could never be saved)";
        return false;
    }

    // 2. The game's synchronous IOS functions, for the save.
    std::uint32_t ios[4] = {known_ios[0], known_ios[1], known_ios[2], known_ios[3]};
    if (ios[0] == 0 || ios[1] == 0 || ios[2] == 0 || ios[3] == 0) {
        std::string error;
        IpcSymbols symbols;
        IpcApi api;
        if (!find_ipc_symbols(text, symbols, error) || !find_ipc_api(text, symbols, api, error)) {
            why = "the game's IOS functions were not found: " + error;
            return false;
        }
        ios[0] = api.sync[kIpcOpen];
        ios[1] = api.sync[kIpcClose];
        ios[2] = api.sync[kIpcWrite];
        ios[3] = api.sync[kIpcIoctlCmd];
    }
    if (ios[0] == 0 || ios[1] == 0 || ios[2] == 0 || ios[3] == 0) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "the game lacks a synchronous IOS function (open 0x%08x, close 0x%08x, "
                      "write 0x%08x, ioctl 0x%08x)", ios[0], ios[1], ios[2], ios[3]);
        why = buf;
        return false;
    }

    // 3. Memory: the blob below the MEM1 arena's top, the log at the
    //    bottom of the MEM2 arena.
    out.code_bytes = align_up(h.size);
    out.state_bytes = align_up(sizeof(rt_wlog_state));
    out.code_base = (arena1_hi - out.code_bytes) & ~31u;
    out.state_base = align_up(arena2_lo);
    const std::uint32_t arena2_end = load_word(kMem2ArenaEndField);
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

    // 4. The blob and its context; the hooks come last. The log needs
    //    nothing now (MEM2's bottom is still this loader's until the
    //    jump): the blob writes its header at its first use.
    std::memset(reinterpret_cast<void*>(out.code_base), 0, out.code_bytes);
    std::memcpy(reinterpret_cast<void*>(out.code_base), riftwii_wlog_bin, h.size);
    rt_wlog_context* ctx = reinterpret_cast<rt_wlog_context*>(out.code_base + h.context_offset);
    if (ctx->magic != RT_WLOG_CONTEXT_MAGIC) {
        why = "the Wii Remote log blob's context is not where its header says";
        return false;
    }
    ctx->state = out.state_base;
    ctx->ios_open = ios[0];
    ctx->ios_close = ios[1];
    ctx->ios_write = ios[2];
    ctx->ios_ioctl = ios[3];
    ctx->saved = 0;
    ctx->result = 0;
    ctx->ticks_per_second = kBusClock / 4u;
    ctx->stubs = static_cast<std::uint32_t>(out.stubs.size());
    out.active = true;
    logf("Wii Remote log: blob %u bytes at 0x%08x, log %u bytes at 0x%08x; %u print function(s), "
         "HOME Menu exit at 0x%08x\n",
         h.size, out.code_base, out.state_bytes, out.state_base, static_cast<unsigned>(out.stubs.size()),
         out.exit_site);
    return true;
}

bool install_wpad_log(WpadLogHook& hook, std::string& why) {
    if (!hook.active) return false;
    const rt_wlog_header& h = header();
    rt_wlog_context* ctx = reinterpret_cast<rt_wlog_context*>(hook.code_base + h.context_offset);

    // The exit site, checked after the patches: something else may have
    // taken it. The replay is the instruction itself and a branch back.
    const std::uint32_t exit_word = load_word(hook.exit_site);
    std::uint32_t back = 0, to_exit = 0;
    if (!is_li_zero(exit_word) ||
        !encode_branch(hook.code_base + h.replay_exit + 4, hook.exit_site + 4, back) ||
        !encode_branch(hook.exit_site, hook.code_base + h.hook_exit, to_exit)) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "the HOME Menu exit at 0x%08x (0x%08x) cannot be hooked", hook.exit_site,
                      exit_word);
        why = buf;
        hook.active = false;
        return false;
    }
    store_word(hook.code_base + h.replay_exit, exit_word);
    store_word(hook.code_base + h.replay_exit + 4, back);

    // The print functions: each one's first word becomes a branch to the
    // blob, which returns to the caller itself (the stub does nothing else).
    std::vector<std::uint32_t> branches;
    unsigned hooked = 0;
    for (const std::uint32_t stub : hook.stubs) {
        std::uint32_t branch = 0;
        if (load_word(stub) != kPrintStubFirst || !encode_branch(stub, hook.code_base + h.hook_print, branch)) {
            branches.push_back(0);
            continue;
        }
        branches.push_back(branch);
        ++hooked;
    }
    if (hooked == 0) {
        why = "no print function could be hooked";
        hook.active = false;
        return false;
    }
    ctx->stubs = hooked;
    sync_code(hook.code_base, hook.code_bytes);
    for (std::size_t i = 0; i < hook.stubs.size(); ++i) {
        if (branches[i] == 0) continue;
        store_word(hook.stubs[i], branches[i]);
        sync_code(hook.stubs[i] & ~31u, 32);
    }
    store_word(hook.exit_site, to_exit);
    sync_code(hook.exit_site & ~31u, 32);
    logf("Wii Remote log: hooked %u print function(s) and the HOME Menu exit; saved when the player picks "
         "Wii Menu\n", hooked);
    return true;
}

}  // namespace riftwii::wii
