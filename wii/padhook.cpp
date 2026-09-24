// SPDX-License-Identifier: GPL-3.0-or-later
#include "padhook.hpp"

#include <gccore.h>
#include <ogc/cache.h>
#include <ogc/ipc.h>
#include <ogc/machine/processor.h>
#include <ogc/usb.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "log.hpp"
#include "pad_hook.h"
#include "riftwii/hook.hpp"
#include "riftwii/symsearch.hpp"
#include "riftwii_pad_bin.h"
#include "rtgcad.h"

namespace riftwii::wii {
namespace {

constexpr std::uint32_t kMem2ArenaEndField = 0x80003128;
constexpr std::uint32_t kBusClockField = 0x800000F8;  // the time base runs at a quarter of it
constexpr unsigned kScratchRegister = 12;             // pad_entry.S's jump back
constexpr std::uint32_t kNop = 0x60000000;
// v5 serves 16 handles, picked by the open mode: libogc's USB takes 0 and
// fakemote (a cIOS module) 15, so this one is ours. v4 ignores it.
constexpr int kHidHandle = 1;

char g_hid_path[] ATTRIBUTE_ALIGN(32) = "/dev/usb/hid";
std::uint32_t g_version_out[8] ATTRIBUTE_ALIGN(32);

std::uint32_t align_up(std::uint32_t v) { return (v + 31) & ~31u; }

const rt_pad_header& header() { return *reinterpret_cast<const rt_pad_header*>(riftwii_pad_bin); }

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

// v4's GetVersion answers 0x40001 itself, v5's writes 0x50001 into its
// output (wiibrew; v4 is asked first, as libogc does).
bool open_usb_hid(std::int32_t& fd, std::uint32_t& version, std::string& why) {
    fd = IOS_Open(g_hid_path, kHidHandle);
    if (fd < 0) {
        why = "/dev/usb/hid did not open (" + std::to_string(fd) + "): this IOS has no USB HID";
        return false;
    }
    if (IOS_Ioctl(fd, GCAD_V4_GET_VERSION, nullptr, 0, nullptr, 0) == static_cast<s32>(GCAD_V4_VERSION)) {
        version = 4;
        return true;
    }
    std::memset(g_version_out, 0, sizeof(g_version_out));
    if (IOS_Ioctl(fd, GCAD_V5_GET_VERSION, nullptr, 0, g_version_out, sizeof(g_version_out)) == 0 &&
        g_version_out[0] == GCAD_V5_VERSION) {
        version = 5;
        return true;
    }
    IOS_Close(fd);
    fd = -1;
    why = "/dev/usb/hid answers neither as v4 nor as v5";
    return false;
}

bool usb_hid_present() {
    const s32 fd = IOS_Open(g_hid_path, kHidHandle);
    if (fd < 0) return false;
    IOS_Close(fd);
    return true;
}

bool plan_pad_hook(const DolHeader& dol, std::uint32_t arena1_hi, std::uint32_t mem1_floor, std::uint32_t arena2_lo,
                   std::uint32_t ioctl_async, std::uint32_t ioctlv_async,
                   const std::vector<MemoryPatch>& patches, bool demo, PadHook& out, std::string& why) {
    out = PadHook{};
    out.demo = demo;
    const rt_pad_header& h = header();
    if (riftwii_pad_bin_size < sizeof(rt_pad_header) || h.magic != RT_PAD_MAGIC || h.version != RT_PAD_VERSION ||
        h.size > riftwii_pad_bin_size || h.context_offset + RT_PAD_CONTEXT_BYTES > h.size) {
        why = "the embedded pad blob is damaged";
        return false;
    }

    // 1. The game's PAD functions and its IOS calls.
    std::vector<CodeRange> text;
    for (std::size_t i = 0; i < kDolTextSections; ++i) {
        const DolSection& s = dol.sections[i];
        if (s.used()) text.push_back({s.address, reinterpret_cast<const std::uint8_t*>(s.address), s.size});
    }
    PadSymbols pad;
    std::string error;
    if (!find_pad_symbols(text, pad, error)) {
        why = "the PAD search is unsure: " + error;
        return false;
    }
    if (pad.read == 0) {
        why = "this game has no GameCube controller support (no PADRead)";
        return false;
    }
    if (ioctl_async == 0) {
        IpcSymbols ipc;
        if (!find_ipc_symbols(text, ipc, error)) {
            why = "the game's IOS_IoctlAsync was not found: " + error;
            return false;
        }
        ioctl_async = ipc.ioctl_async;
        ioctlv_async = ipc.ioctlv_async;
        if (ioctlv_async == 0) {
            IpcApi api;
            if (find_ipc_api(text, ipc, api, error)) ioctlv_async = api.async[kIpcIoctlvCmd];
        }
    }

    // 2. The adapter's device, with the IOS the game will run. The menu's
    // USB storage left libogc's device-change requests pending: v5 takes
    // one at a time (Dolphin refuses a second), and their answers would
    // reach the game. libogc cancels its own; the Shutdown on ours clears
    // one it re-armed while going.
    USB_Deinitialize();
    usleep(50000);
    if (!open_usb_hid(out.fd, out.version, why)) {
        if (!demo) return false;
        logf("GameCube adapter: %s; demo mode goes on without it\n", why.c_str());
        out.fd = -1;
        out.version = 5;
    } else if (out.version == 5) {
        IOS_Ioctl(out.fd, GCAD_V5_SHUTDOWN, nullptr, 0, nullptr, 0);  // an error when nothing was pending
    }
    if (out.version == 5 && ioctlv_async == 0) {
        why = "/dev/usb/hid v5 needs the game's IOS_IoctlvAsync, which was not found";
        if (out.fd >= 0) IOS_Close(out.fd);
        return false;
    }

    // 3. Memory: the blob below the MEM1 arena's top, the state at the
    //    bottom of the MEM2 arena. A pack's patch there turns it off.
    out.code_bytes = align_up(h.size);
    out.code_base = (arena1_hi - out.code_bytes) & ~31u;
    out.state_bytes = align_up(sizeof(gcad));
    out.state_base = align_up(arena2_lo);
    const std::uint32_t arena2_end = read32(kMem2ArenaEndField);
    if (arena1_hi < out.code_bytes || out.code_base < mem1_floor || out.state_base + out.state_bytes > arena2_end) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "no room (MEM1 arena top 0x%08x, floor 0x%08x; MEM2 arena 0x%08x-0x%08x)",
                      arena1_hi, mem1_floor, arena2_lo, arena2_end);
        why = buf;
        if (out.fd >= 0) IOS_Close(out.fd);
        return false;
    }
    for (const MemoryPatch& p : patches) {
        if (overlaps(p, out.code_base, out.code_bytes) || overlaps(p, out.state_base, out.state_bytes)) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "a pack's memory patch at 0x%08x uses the memory it needs",
                          static_cast<unsigned>(p.offset));
            why = buf;
            if (out.fd >= 0) IOS_Close(out.fd);
            return false;
        }
    }
    out.new_arena1_hi = out.code_base;
    out.new_arena2_lo = out.state_base + out.state_bytes;
    out.read = pad.read;
    out.motor = pad.control_motor;

    // 4. The blob and its context; the hooks come after the patches.
    std::memset(reinterpret_cast<void*>(out.code_base), 0, out.code_bytes);
    std::memcpy(reinterpret_cast<void*>(out.code_base), riftwii_pad_bin, h.size);
    rt_pad_context* ctx = reinterpret_cast<rt_pad_context*>(out.code_base + h.context_offset);
    if (ctx->magic != RT_PAD_CONTEXT_MAGIC) {
        why = "the pad blob's context is not where its header says";
        if (out.fd >= 0) IOS_Close(out.fd);
        return false;
    }
    ctx->state = out.state_base;
    ctx->ioctl_async = ioctl_async;
    ctx->ioctlv_async = ioctlv_async;
    ctx->complete_entry = out.code_base + h.complete;
    ctx->fd = out.fd;
    ctx->version = out.version;
    ctx->ticks_per_ms = read32(kBusClockField) / 4000u;
    ctx->inited = 0;
    ctx->flags = demo ? RT_PAD_FLAG_DEMO : 0u;
    out.active = true;
    logf("GameCube adapter: PADRead at 0x%08x (%u error stores), PADControlMotor at 0x%08x; /dev/usb/hid v%u fd %d; "
         "blob %u bytes at 0x%08x, state %u bytes at 0x%08x%s\n",
         pad.read, pad.read_sites, pad.control_motor, out.version, out.fd, h.size, out.code_base, out.state_bytes,
         out.state_base, demo ? " (demo)" : "");
    return true;
}

bool install_pad_hook(PadHook& hook, std::string& why) {
    if (!hook.active) return false;
    const rt_pad_header& h = header();
    struct Site {
        std::uint32_t function, hook, replay, resume;
        const char* name;
    } sites[2] = {{hook.read, h.hook_read, h.replay_read, h.continue_read, "PADRead"},
                  {hook.motor, h.hook_motor, h.replay_motor, h.continue_motor, "PADControlMotor"}};
    // Checked now, after the packs' patches: an instruction a pack
    // replaced with a branch of its own cannot be moved.
    for (Site& s : sites) {
        if (s.function == 0) continue;
        const std::uint32_t first = *reinterpret_cast<const std::uint32_t*>(s.function);
        std::string reason;
        if (!displaceable(first, kScratchRegister, reason)) {
            if (&s == &sites[0]) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "PADRead's first instruction (0x%08x)", first);
                why = std::string(buf) + " cannot be moved: " + reason + "; a pack or code has hooked it";
                hook.active = false;
                return false;
            }
            logf("GameCube adapter: PADControlMotor cannot be hooked (%s); no rumble\n", reason.c_str());
            s.function = 0;
            continue;
        }
        const std::uint32_t replay[4] = {first, kNop, kNop, kNop};
        store_words(hook.code_base + s.replay, replay, 4);
        const auto resume = encode_absolute_jump(kScratchRegister, s.function + 4);
        store_words(hook.code_base + s.resume, resume.data(), 4);
    }
    sync_code(hook.code_base, hook.code_bytes);
    for (const Site& s : sites) {
        if (s.function == 0) continue;
        std::uint32_t branch = 0;
        if (!encode_branch(s.function, hook.code_base + s.hook, branch)) {
            why = std::string(s.name) + " is out of a branch's reach";
            if (&s == &sites[0]) {
                hook.active = false;
                return false;
            }
            continue;
        }
        store_words(s.function, &branch, 1);
        sync_code(s.function & ~31u, 32);
    }
    logf("GameCube adapter: hooked%s\n", sites[1].function == 0 ? " (PADRead only)" : "");
    return true;
}

}  // namespace riftwii::wii
